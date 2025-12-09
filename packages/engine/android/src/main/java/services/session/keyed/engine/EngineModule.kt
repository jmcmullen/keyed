package services.session.keyed.engine

import android.Manifest
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import androidx.core.content.ContextCompat
import expo.modules.kotlin.Promise
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition
import expo.modules.interfaces.permissions.Permissions
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import kotlin.math.abs
import kotlin.math.sqrt

class EngineModule : Module() {
    companion object {
        private const val TAG = "EngineModule"
        private const val SAMPLE_RATE = 22050

        init {
            System.loadLibrary("engine")
        }
    }

    // Native method declarations
    private external fun nativeInit()
    private external fun nativeReset()
    private external fun nativeDestroy()
    private external fun nativeLoadModel(modelPath: String): Boolean
    private external fun nativeIsReady(): Boolean
    private external fun nativeWarmUp(): Boolean
    private external fun nativeProcessAudio(samples: FloatArray): Array<FrameResult>?
    private external fun nativeGetBpm(): Float
    private external fun nativeGetFrameCount(): Long

    private var audioRecord: AudioRecord? = null
    private val isRecordingAudio = AtomicBoolean(false)
    private var recordingThread: Thread? = null
    private val waveformBufferSize = 512
    private val waveformRingBuffer = FloatArray(waveformBufferSize)
    @Volatile private var waveformWriteIndex = 0
    @Volatile private var waveformSamplesAccumulated = 0

    override fun definition() = ModuleDefinition {
        Name("Engine")

        OnCreate { nativeInit() }
        OnDestroy {
            stopAudioRecording()
            nativeDestroy()
        }

        Constants(
            "SAMPLE_RATE" to SAMPLE_RATE,
            "FPS" to 50
        )

        Events("onState", "onWaveform")

        Function("reset") { nativeReset() }
        Function("loadModel") {
            val context = appContext.reactContext ?: run {
                Log.e(TAG, "Context not available for loading model")
                return@Function false
            }

            // Copy model from assets to cache directory (ONNX Runtime needs a file path)
            val modelFile = File(context.cacheDir, "beatnet.onnx")
            if (!modelFile.exists()) {
                try {
                    context.assets.open("beatnet.onnx").use { input ->
                        FileOutputStream(modelFile).use { output ->
                            input.copyTo(output)
                        }
                    }
                    Log.i(TAG, "Copied model to: ${modelFile.absolutePath}")
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to copy model from assets: ${e.message}")
                    return@Function false
                }
            }

            val loaded = nativeLoadModel(modelFile.absolutePath)
            if (loaded) {
                Log.i(TAG, "Model loaded, running warm-up inference...")
                val warmedUp = nativeWarmUp()
                Log.i(TAG, "Warm-up complete: $warmedUp")
            }
            loaded
        }
        Function("isReady") { nativeIsReady() }

        Function("processAudio") { samples: List<Double> ->
            val floatSamples = FloatArray(samples.size) { samples[it].toFloat() }
            val results = nativeProcessAudio(floatSamples)
            results?.map { result ->
                mapOf(
                    "beatActivation" to result.beatActivation.toDouble(),
                    "downbeatActivation" to result.downbeatActivation.toDouble()
                )
            }
        }

        Function("getBpm") { nativeGetBpm().toDouble() }
        Function("getFrameCount") { nativeGetFrameCount().toInt() }

        // Permissions
        AsyncFunction("requestPermission") { promise: Promise ->
            Permissions.askForPermissionsWithPermissionsManager(
                appContext.permissions,
                promise,
                Manifest.permission.RECORD_AUDIO
            )
        }

        Function("getPermissionStatus") {
            val context = appContext.reactContext ?: return@Function "undetermined"
            when (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO)) {
                PackageManager.PERMISSION_GRANTED -> "granted"
                PackageManager.PERMISSION_DENIED -> "denied"
                else -> "undetermined"
            }
        }

        AsyncFunction("startRecording") { enableWaveform: Boolean, promise: Promise ->
            val context = appContext.reactContext
            if (context == null) {
                Log.e(TAG, "Context not available")
                promise.resolve(false)
                return@AsyncFunction
            }

            val permission = ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO)
            if (permission != PackageManager.PERMISSION_GRANTED) {
                Log.e(TAG, "Microphone permission not granted")
                promise.resolve(false)
                return@AsyncFunction
            }

            try {
                startAudioRecording(enableWaveform)
                promise.resolve(true)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to start recording: ${e.message}")
                promise.resolve(false)
            }
        }

        Function("stopRecording") { stopAudioRecording() }
        Function("isRecording") { isRecordingAudio.get() }
    }

    private fun startAudioRecording(enableWaveform: Boolean) {
        if (isRecordingAudio.get()) {
            return
        }

        val bufferSize = AudioRecord.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_FLOAT
        ).coerceAtLeast(441 * 4)

        audioRecord = AudioRecord(
            MediaRecorder.AudioSource.MIC,
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_FLOAT,
            bufferSize
        )

        if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
            Log.e(TAG, "AudioRecord failed to initialize")
            audioRecord?.release()
            audioRecord = null
            throw Exception("Failed to initialize AudioRecord")
        }

        nativeReset()
        waveformWriteIndex = 0
        waveformSamplesAccumulated = 0

        isRecordingAudio.set(true)
        audioRecord?.startRecording()

        recordingThread = thread(start = true) {
            val buffer = FloatArray(441)
            while (isRecordingAudio.get()) {
                val recorder = audioRecord ?: break
                val read = try {
                    recorder.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING)
                } catch (e: IllegalStateException) {
                    // AudioRecord was stopped/released
                    break
                }

                if (read > 0 && isRecordingAudio.get()) {
                    processAudioSamples(buffer.copyOf(read), enableWaveform)
                } else if (read < 0) {
                    // Error or stopped
                    break
                }
            }
        }

        Log.i(TAG, "Audio recording started at ${SAMPLE_RATE}Hz")
    }

    private fun stopAudioRecording() {
        isRecordingAudio.set(false)

        // Stop AudioRecord first to unblock the blocking read() call
        try {
            audioRecord?.stop()
        } catch (e: IllegalStateException) {
            Log.w(TAG, "AudioRecord.stop() failed: ${e.message}")
        }

        // Wait for thread to terminate with retries
        val thread = recordingThread
        if (thread != null) {
            var totalWait = 0L
            val maxWait = 2000L
            val joinInterval = 200L

            while (thread.isAlive && totalWait < maxWait) {
                try {
                    thread.join(joinInterval)
                    totalWait += joinInterval
                } catch (e: InterruptedException) {
                    Thread.currentThread().interrupt()
                    break
                }
            }

            if (thread.isAlive) {
                Log.w(TAG, "Recording thread did not terminate in time")
            }
        }
        recordingThread = null

        // Release only after thread has terminated
        audioRecord?.release()
        audioRecord = null

        Log.i(TAG, "Audio recording stopped")
    }

    private fun processAudioSamples(samples: FloatArray, enableWaveform: Boolean) {
        val results = nativeProcessAudio(samples) ?: return

        for (result in results) {
            sendEvent("onState", mapOf(
                "beatActivation" to result.beatActivation.toDouble(),
                "downbeatActivation" to result.downbeatActivation.toDouble()
            ))
        }

        if (enableWaveform) {
            for (sample in samples) {
                waveformRingBuffer[waveformWriteIndex] = sample
                waveformWriteIndex = (waveformWriteIndex + 1) % waveformBufferSize
                waveformSamplesAccumulated++
            }

            if (waveformSamplesAccumulated >= waveformBufferSize) {
                waveformSamplesAccumulated = 0

                var peak = 0f
                var sumSquares = 0f
                for (i in 0 until waveformBufferSize) {
                    val absVal = abs(waveformRingBuffer[i])
                    if (absVal > peak) peak = absVal
                    sumSquares += waveformRingBuffer[i] * waveformRingBuffer[i]
                }
                val rms = sqrt(sumSquares / waveformBufferSize)

                val orderedSamples = DoubleArray(waveformBufferSize) { i ->
                    val readIndex = (waveformWriteIndex + i) % waveformBufferSize
                    waveformRingBuffer[readIndex].toDouble()
                }

                sendEvent("onWaveform", mapOf(
                    "samples" to orderedSamples.toList(),
                    "peak" to peak.toDouble(),
                    "rms" to rms.toDouble()
                ))
            }
        }
    }
}
