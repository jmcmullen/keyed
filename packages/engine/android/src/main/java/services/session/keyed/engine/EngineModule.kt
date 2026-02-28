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
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

class EngineModule : Module() {
	companion object {
		private const val TAG = "EngineModule"
		private const val SAMPLE_RATE = 44100           // Native sample rate
		private const val BPM_SAMPLE_RATE = 22050       // BPM pipeline
		private const val KEY_SAMPLE_RATE = 44100       // Key detection

		init {
			System.loadLibrary("engine")
		}
	}

	// Native method declarations - BPM
	private external fun nativeInit()
	private external fun nativeReset()
	private external fun nativeDestroy()
	private external fun nativeLoadModel(modelPath: String): Boolean
	private external fun nativeIsReady(): Boolean
	private external fun nativeWarmUp(): Boolean
	private external fun nativeProcessAudio(samples: FloatArray): Array<FrameResult>?
	private external fun nativeProcessAudioForBpm(samples: FloatArray): Array<FrameResult>?
	private external fun nativeGetBpm(): Float
	private external fun nativeGetFrameCount(): Long

	// Native method declarations - Key
	private external fun nativeLoadKeyModel(modelPath: String): Boolean
	private external fun nativeIsKeyReady(): Boolean
	private external fun nativeWarmUpKey(): Boolean
	private external fun nativeGetKey(): KeyResult?
	private external fun nativeGetKeyFrameCount(): Long

	private var audioRecord: AudioRecord? = null
	private val isRecordingAudio = AtomicBoolean(false)
	private var recordingThread: Thread? = null
	private val waveformBufferSize = 128
	private val waveformInputSize = 256
	private val waveformInputBuffer = FloatArray(waveformInputSize)
	private val fftSize = 256
	private val fftWindow = FloatArray(fftSize) { index ->
		(0.5 * (1.0 - cos((2.0 * PI * index) / (fftSize - 1)))).toFloat()
	}
	@Volatile private var waveformWriteIndex = 0
	@Volatile private var waveformSamplesAccumulated = 0
	@Volatile private var recordingStartTimeNs = 0L

	// Key detection state
	@Volatile private var lastKeyNotation: String = ""
	@Volatile private var lastKeyCamelot: String = ""
	@Volatile private var lastKeyConfidence: Float = 0f

	override fun definition() = ModuleDefinition {
		Name("Engine")

		OnCreate { nativeInit() }
		OnDestroy {
			stopAudioRecording()
			nativeDestroy()
		}

		Constants(
			"SAMPLE_RATE" to SAMPLE_RATE,
			"BPM_SAMPLE_RATE" to BPM_SAMPLE_RATE,
			"KEY_SAMPLE_RATE" to KEY_SAMPLE_RATE,
			"BPM_FPS" to 50,
			"KEY_FPS" to 5
		)

		Events("onState", "onWaveform", "onKey")

		// =====================================================================
		// Engine Control
		// =====================================================================

		Function("reset") { nativeReset() }

		// =====================================================================
		// BPM Detection (BeatNet)
		// =====================================================================

		Function("loadModel") {
			val context = appContext.reactContext ?: run {
				Log.e(TAG, "Context not available for loading model")
				return@Function false
			}

			// Copy model from assets to cache directory
			val modelFile = File(context.cacheDir, "beatnet.onnx")
			if (!modelFile.exists()) {
				try {
					context.assets.open("beatnet.onnx").use { input ->
						FileOutputStream(modelFile).use { output ->
							input.copyTo(output)
						}
					}
					debugLog("Copied BeatNet to: ${modelFile.absolutePath}")
				} catch (e: Exception) {
					Log.e(TAG, "Failed to copy BeatNet from assets: ${e.message}")
					return@Function false
				}
			}

			val loaded = nativeLoadModel(modelFile.absolutePath)
			if (loaded) {
				debugLog("BeatNet loaded, running warm-up inference...")
				val warmedUp = nativeWarmUp()
				debugLog("BeatNet warm-up complete: $warmedUp")
			}
			loaded
		}

		Function("isReady") { nativeIsReady() }
		Function("getBpm") { nativeGetBpm().toDouble() }
		Function("getFrameCount") { nativeGetFrameCount().toInt() }

		// =====================================================================
		// Key Detection (MusicalKeyCNN)
		// =====================================================================

		Function("loadKeyModel") {
			val context = appContext.reactContext ?: run {
				Log.e(TAG, "Context not available for loading key model")
				return@Function false
			}

			// Copy model from assets to cache directory
			val modelFile = File(context.cacheDir, "keynet.onnx")
			if (!modelFile.exists()) {
				try {
					context.assets.open("keynet.onnx").use { input ->
						FileOutputStream(modelFile).use { output ->
							input.copyTo(output)
						}
					}
					debugLog("Copied MusicalKeyCNN to: ${modelFile.absolutePath}")
				} catch (e: Exception) {
					Log.e(TAG, "Failed to copy MusicalKeyCNN from assets: ${e.message}")
					return@Function false
				}
			}

			val loaded = nativeLoadKeyModel(modelFile.absolutePath)
			if (loaded) {
				debugLog("MusicalKeyCNN loaded, running warm-up inference...")
				val warmedUp = nativeWarmUpKey()
				debugLog("MusicalKeyCNN warm-up complete: $warmedUp")
			}
			loaded
		}

		Function("isKeyReady") { nativeIsKeyReady() }

		Function("getKey") {
			val result = nativeGetKey() ?: return@Function null
			if (!result.valid) return@Function null
			mapOf(
				"camelot" to result.camelot,
				"notation" to result.notation,
				"confidence" to result.confidence.toDouble()
			)
		}

		Function("getKeyFrameCount") { nativeGetKeyFrameCount().toInt() }

		// =====================================================================
		// Audio Processing
		// =====================================================================

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

		// =====================================================================
		// Permissions
		// =====================================================================

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

		// =====================================================================
		// Audio Recording
		// =====================================================================

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
		).coerceAtLeast(882 * 4)  // ~20ms at 44100Hz

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
		lastKeyNotation = ""
		lastKeyCamelot = ""
		lastKeyConfidence = 0f
		waveformWriteIndex = 0
		waveformSamplesAccumulated = 0
		recordingStartTimeNs = System.nanoTime()

		isRecordingAudio.set(true)
		audioRecord?.startRecording()

		recordingThread = thread(start = true) {
			val buffer = FloatArray(882)  // ~20ms at 44100Hz
			while (isRecordingAudio.get()) {
				val recorder = audioRecord ?: break
				val read = try {
					recorder.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING)
				} catch (e: IllegalStateException) {
					Log.w(TAG, "AudioRecord.read() failed: ${e.message}", e)
					break
				}

				if (read > 0 && isRecordingAudio.get()) {
					processAudioSamples(buffer.copyOf(read), enableWaveform)
				} else if (read < 0) {
					break
				}
			}
		}

		debugLog("Audio recording started at ${SAMPLE_RATE}Hz")
	}

	private fun stopAudioRecording() {
		isRecordingAudio.set(false)

		try {
			audioRecord?.stop()
		} catch (e: IllegalStateException) {
			Log.w(TAG, "AudioRecord.stop() failed: ${e.message}")
		}

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

		audioRecord?.release()
		audioRecord = null
		recordingStartTimeNs = 0L

		debugLog("Audio recording stopped")
	}

	private fun processAudioSamples(samples: FloatArray, enableWaveform: Boolean) {
		val results = nativeProcessAudio(samples)
		val timestampSeconds = if (recordingStartTimeNs > 0L) {
			(System.nanoTime() - recordingStartTimeNs).toDouble() / 1_000_000_000.0
		} else {
			0.0
		}

		// Emit BPM state events
		if (results != null) {
			for (result in results) {
				sendEvent("onState", mapOf(
					"beatActivation" to result.beatActivation.toDouble(),
					"downbeatActivation" to result.downbeatActivation.toDouble(),
					"timestamp" to timestampSeconds
				))
			}
		}

		// Check for key detection updates (emit on key change OR significant confidence change)
		val keyResult = nativeGetKey()
		if (keyResult != null && keyResult.valid) {
			val keyChanged = keyResult.notation != lastKeyNotation || keyResult.camelot != lastKeyCamelot
			val confidenceChanged = kotlin.math.abs(keyResult.confidence - lastKeyConfidence) > 0.01f
			if (keyChanged || confidenceChanged) {
				lastKeyNotation = keyResult.notation
				lastKeyCamelot = keyResult.camelot
				lastKeyConfidence = keyResult.confidence
				debugLog("Key detected: ${keyResult.notation} (${keyResult.camelot}) confidence: ${keyResult.confidence}")
				sendEvent("onKey", mapOf(
					"camelot" to keyResult.camelot,
					"notation" to keyResult.notation,
					"confidence" to keyResult.confidence.toDouble(),
					"timestamp" to timestampSeconds
				))
			}
		}

		// Waveform processing
		if (enableWaveform) {
			for (sample in samples) {
				waveformInputBuffer[waveformWriteIndex] = sample
				waveformWriteIndex = (waveformWriteIndex + 1) % waveformInputSize
				waveformSamplesAccumulated++
			}

			// Emit waveform at similar rate as before (accounting for 2x sample rate)
			val waveformThreshold = waveformInputSize * 2
			if (waveformSamplesAccumulated >= waveformThreshold) {
				waveformSamplesAccumulated = 0

				var peak = 0f
				var sumSquares = 0f
				val samplesPerPoint = waveformInputSize / waveformBufferSize
				val downsampledPoints = DoubleArray(waveformBufferSize)

				for (i in 0 until waveformBufferSize) {
					var sum = 0f
					for (j in 0 until samplesPerPoint) {
						val readIndex = (waveformWriteIndex + i * samplesPerPoint + j) % waveformInputSize
						val value = waveformInputBuffer[readIndex]
						sum += value
						val absVal = abs(value)
						if (absVal > peak) peak = absVal
						sumSquares += value * value
					}
					downsampledPoints[i] = (sum / samplesPerPoint).toDouble()
				}
				val rms = sqrt(sumSquares / waveformInputSize)

				val orderedSamples = FloatArray(waveformInputSize) { i ->
					val readIndex = (waveformWriteIndex + i) % waveformInputSize
					waveformInputBuffer[readIndex]
				}
				val frequencyBands = computeFrequencyBands(orderedSamples)

				sendEvent("onWaveform", mapOf(
					"samples" to downsampledPoints.toList(),
					"peak" to peak.toDouble(),
					"rms" to rms.toDouble(),
					"low" to frequencyBands.first,
					"mid" to frequencyBands.second,
					"high" to frequencyBands.third
				))
			}
		}
	}

	private fun computeFrequencyBands(samples: FloatArray): Triple<Double, Double, Double> {
		if (samples.size < fftSize) {
			return Triple(0.33, 0.33, 0.34)
		}

		val windowed = FloatArray(fftSize)
		for (i in 0 until fftSize) {
			windowed[i] = samples[i] * fftWindow[i]
		}

		val halfSize = fftSize / 2
		var lowEnergy = 0.0
		var midEnergy = 0.0
		var highEnergy = 0.0

		for (k in 1 until halfSize) {
			var real = 0.0
			var imaginary = 0.0
			for (n in 0 until fftSize) {
				val angle = 2.0 * PI * k * n / fftSize
				val sample = windowed[n].toDouble()
				real += sample * cos(angle)
				imaginary -= sample * sin(angle)
			}

			val magnitude = sqrt((real * real) + (imaginary * imaginary))
			when {
				k < 3 -> lowEnergy += magnitude
				k < 29 -> midEnergy += magnitude
				else -> highEnergy += magnitude
			}
		}

		val totalEnergy = lowEnergy + midEnergy + highEnergy
		if (totalEnergy <= 0.0) {
			return Triple(0.33, 0.33, 0.34)
		}

		return Triple(
			lowEnergy / totalEnergy,
			midEnergy / totalEnergy,
			highEnergy / totalEnergy
		)
	}

	private fun debugLog(message: String) {
		if (BuildConfig.DEBUG) {
			Log.d(TAG, message)
		}
	}
}
