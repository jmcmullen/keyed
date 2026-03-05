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
import expo.modules.interfaces.permissions.PermissionsStatus
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import kotlin.math.abs
import kotlin.math.sqrt

class EngineModule : Module() {
	companion object {
		private const val TAG = "EngineModule"
		private const val SAMPLE_RATE = 44100           // Native sample rate
		private const val BPM_SAMPLE_RATE = 22050       // BPM pipeline
		private const val KEY_SAMPLE_RATE = 44100       // Key detection
		private const val STATE_EMIT_INTERVAL_NS = 50_000_000L      // 20Hz
		private const val WAVEFORM_EMIT_INTERVAL_NS = 83_333_333L   // 12Hz

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
	private external fun nativeProcessAudio(samples: FloatArray, count: Int): Array<FrameResult>?
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
	@Volatile private var waveformWriteIndex = 0
	@Volatile private var waveformSamplesAccumulated = 0
	@Volatile private var recordingStartTimeNs = 0L
	@Volatile private var lastStateEmitNs = 0L
	@Volatile private var lastWaveformEmitNs = 0L

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

		AsyncFunction("loadModel") { promise: Promise ->
			val context = appContext.reactContext ?: run {
				Log.e(TAG, "Context not available for loading model")
				promise.resolve(false)
				return@AsyncFunction
			}

			val modelPath = copyAssetToCache(context, "beatnet.onnx")
			if (modelPath == null) {
				promise.resolve(false)
				return@AsyncFunction
			}

			val loaded = nativeLoadModel(modelPath)
			if (loaded) {
				debugLog("BeatNet loaded, running warm-up inference...")
				val warmedUp = nativeWarmUp()
				debugLog("BeatNet warm-up complete: $warmedUp")
			}
			promise.resolve(loaded)
		}

		Function("isReady") { nativeIsReady() }
		Function("getBpm") { nativeGetBpm().toDouble() }
		Function("getFrameCount") { nativeGetFrameCount().toInt() }

		// =====================================================================
		// Key Detection (MusicalKeyCNN)
		// =====================================================================

		AsyncFunction("loadKeyModel") { promise: Promise ->
			val context = appContext.reactContext ?: run {
				Log.e(TAG, "Context not available for loading key model")
				promise.resolve(false)
				return@AsyncFunction
			}

			val modelPath = copyAssetToCache(context, "keynet.onnx")
			if (modelPath == null) {
				promise.resolve(false)
				return@AsyncFunction
			}

			val loaded = nativeLoadKeyModel(modelPath)
			if (loaded) {
				debugLog("MusicalKeyCNN loaded, running warm-up inference...")
				val warmedUp = nativeWarmUpKey()
				debugLog("MusicalKeyCNN warm-up complete: $warmedUp")
			}
			promise.resolve(loaded)
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
			val results = nativeProcessAudio(floatSamples, floatSamples.size)
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
			val manager = appContext.permissions ?: run {
				promise.resolve(false)
				return@AsyncFunction
			}
			if (manager.hasGrantedPermissions(Manifest.permission.RECORD_AUDIO)) {
				promise.resolve(true)
				return@AsyncFunction
			}
			try {
				manager.askForPermissions(
					{ result ->
						val response = result[Manifest.permission.RECORD_AUDIO]
						promise.resolve(response?.status == PermissionsStatus.GRANTED)
					},
					Manifest.permission.RECORD_AUDIO
				)
			} catch (e: Exception) {
				Log.e(TAG, "Failed to request microphone permission: ${e.message}")
				promise.resolve(false)
			}
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
			if (isRecordingAudio.get()) {
				promise.resolve(true)
				return@AsyncFunction
			}

			val context = appContext.reactContext
			if (context == null) {
				Log.e(TAG, "Context not available")
				promise.resolve(false)
				return@AsyncFunction
			}

			val permission = ContextCompat.checkSelfPermission(
				context,
				Manifest.permission.RECORD_AUDIO
			)
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
		lastStateEmitNs = 0L
		lastWaveformEmitNs = 0L

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
					processAudioSamples(buffer, read, enableWaveform)
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
		lastStateEmitNs = 0L
		lastWaveformEmitNs = 0L

		debugLog("Audio recording stopped")
	}

	private fun processAudioSamples(samples: FloatArray, count: Int, enableWaveform: Boolean) {
		val results = nativeProcessAudio(samples, count)
		val nowNs = System.nanoTime()
		val timestampSeconds = if (recordingStartTimeNs > 0L) {
			(nowNs - recordingStartTimeNs).toDouble() / 1_000_000_000.0
		} else {
			0.0
		}

		if (results != null && nowNs - lastStateEmitNs >= STATE_EMIT_INTERVAL_NS) {
			val result = results.lastOrNull()
			if (result != null) {
				lastStateEmitNs = nowNs
				sendEvent(
					"onState",
					mapOf(
						"beatActivation" to result.beatActivation.toDouble(),
						"downbeatActivation" to result.downbeatActivation.toDouble(),
						"timestamp" to timestampSeconds
					)
				)
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

		if (enableWaveform) {
			for (idx in 0 until count) {
				waveformInputBuffer[waveformWriteIndex] = samples[idx]
				waveformWriteIndex = (waveformWriteIndex + 1) % waveformInputSize
				waveformSamplesAccumulated++
			}

			val waveformThreshold = waveformInputSize * 2
			if (waveformSamplesAccumulated >= waveformThreshold) {
				waveformSamplesAccumulated = 0
				if (nowNs - lastWaveformEmitNs < WAVEFORM_EMIT_INTERVAL_NS) {
					return
				}
				lastWaveformEmitNs = nowNs

				var peak = 0f
				var sumSquares = 0f
				var sumAbs = 0f
				var sumDiff = 0f
				var last = 0f
				var hasLast = false
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
						sumAbs += absVal
						if (hasLast) {
							sumDiff += abs(value - last)
						}
						last = value
						hasLast = true
						sumSquares += value * value
					}
					downsampledPoints[i] = (sum / samplesPerPoint).toDouble()
				}
				val rms = sqrt(sumSquares / waveformInputSize)
				val gain = 1f / maxOf(peak, 0.01f)
				val avgAbs = (sumAbs / waveformInputSize) * gain
				val avgDiff = (sumDiff / (waveformInputSize - 1)) * gain
				val high = (avgDiff * 5f).coerceIn(0f, 1f)
				val low = ((avgAbs - avgDiff * 0.5f) * 1.5f).coerceIn(0f, 1f)
				val mid = (avgAbs * 1.2f).coerceIn(0f, 1f)

				sendEvent(
					"onWaveform",
					mapOf(
						"samples" to downsampledPoints.toList(),
						"peak" to peak.toDouble(),
						"rms" to rms.toDouble(),
						"low" to low.toDouble(),
						"mid" to mid.toDouble(),
						"high" to high.toDouble()
					)
				)
			}
		}
	}

	private fun copyAssetToCache(context: android.content.Context, name: String): String? {
		val target = File(context.cacheDir, name)
		val temp = File(context.cacheDir, "$name.tmp")
		try {
			context.assets.open(name).use { input ->
				FileOutputStream(temp).use { output ->
					input.copyTo(output)
				}
			}
			if (target.exists() && !target.delete()) {
				Log.e(TAG, "Failed to delete cached model: ${target.absolutePath}")
				temp.delete()
				return null
			}
			if (!temp.renameTo(target)) {
				Log.e(TAG, "Failed to update model cache: ${target.absolutePath}")
				temp.delete()
				return null
			}
			debugLog("Copied model to: ${target.absolutePath}")
			return target.absolutePath
		} catch (e: Exception) {
			Log.e(TAG, "Failed to copy model from assets ($name): ${e.message}")
			temp.delete()
			return null
		}
	}

	private fun debugLog(message: String) {
		if (BuildConfig.DEBUG) {
			Log.d(TAG, message)
		}
	}
}
