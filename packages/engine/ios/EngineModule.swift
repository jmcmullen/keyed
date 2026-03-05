import ExpoModulesCore
import AVFoundation
import Accelerate

public class EngineModule: Module {
	private lazy var bridge: EngineBridge = EngineBridge.shared()
	private var audioEngine: AVAudioEngine?
	private var isRecordingAudio = false
	private var enableWaveformEvents = false

	private let waveformBufferSize = 128
	private let waveformInputSize = 256
	private lazy var waveformInputBuffer: [Float] = [Float](repeating: 0, count: waveformInputSize)
	private var waveformWriteIndex = 0
	private var waveformSamplesAccumulated = 0
	private var waveformEmitCount = 0
	private let stateEmitInterval: TimeInterval = 1.0 / 20.0
	private let waveformEmitInterval: TimeInterval = 1.0 / 12.0
	private var lastStateEmitTime: TimeInterval = 0
	private var lastWaveformEmitTime: TimeInterval = 0

	// FFT setup for frequency analysis
	private let fftSize = 256
	private lazy var fftSetup: vDSP_DFT_Setup? = vDSP_DFT_zop_CreateSetup(nil, vDSP_Length(fftSize), .FORWARD)
	private lazy var fftWindow: [Float] = {
		var window = [Float](repeating: 0, count: fftSize)
		vDSP_hann_window(&window, vDSP_Length(fftSize), Int32(vDSP_HANN_NORM))
		return window
	}()

	// Key detection state
	private var lastKeyNotation: String = ""
	private var lastKeyCamelot: String = ""
	private var lastKeyConfidence: Float = 0

	private func debugLog(_ message: String) {
		#if DEBUG
		print("[EngineModule] \(message)")
		#endif
	}

	/// Find a resource in the EngineResources bundle (handles both dev and release builds)
	private func findResourcePath(name: String, ext: String) -> String? {
		let frameworkBundle = Bundle(for: type(of: self))

		// Try resource_bundles location first (CocoaPods with resource_bundles)
		if let resourceBundlePath = frameworkBundle.path(forResource: "EngineResources", ofType: "bundle"),
		   let resourceBundle = Bundle(path: resourceBundlePath),
		   let modelPath = resourceBundle.path(forResource: name, ofType: ext) {
			debugLog("Found \(name).\(ext) in EngineResources bundle: \(modelPath)")
			return modelPath
		}

		// Fallback: direct resource in framework bundle (older CocoaPods setup)
		if let modelPath = frameworkBundle.path(forResource: name, ofType: ext) {
			debugLog("Found \(name).\(ext) in framework bundle: \(modelPath)")
			return modelPath
		}

		// Fallback: main app bundle (dev builds)
		if let modelPath = Bundle.main.path(forResource: name, ofType: ext) {
			debugLog("Found \(name).\(ext) in main bundle: \(modelPath)")
			return modelPath
		}

		debugLog("Failed to find \(name).\(ext) in any bundle")
		debugLog("Framework bundle path: \(frameworkBundle.bundlePath)")
		debugLog("Framework bundle resources: \(frameworkBundle.paths(forResourcesOfType: "bundle", inDirectory: nil))")
		return nil
	}

	public func definition() -> ModuleDefinition {
		Name("Engine")

		// Constants exposed to JavaScript
		Constant("SAMPLE_RATE") { 44100 }         // Native sample rate
		Constant("BPM_SAMPLE_RATE") { 22050 }     // BPM pipeline
		Constant("KEY_SAMPLE_RATE") { 44100 }     // Key detection
		Constant("BPM_FPS") { 50 }
		Constant("KEY_FPS") { 5 }

		Events("onState", "onWaveform", "onKey")

		// MARK: - Engine Control

		Function("reset") { self.bridge.reset() }

		// MARK: - BPM Detection (BeatNet)

		AsyncFunction("loadModel") { (promise: Promise) in
			guard let modelPath = self.findResourcePath(name: "beatnet", ext: "onnx") else {
				promise.resolve(false)
				return
			}

			self.debugLog("Loading BeatNet model from: \(modelPath)")
			let loaded = self.bridge.loadModel(modelPath)
			if loaded {
				self.debugLog("BeatNet loaded, running warm-up inference...")
				let warmedUp = self.bridge.warmUp()
				self.debugLog("BeatNet warm-up complete: \(warmedUp)")
			}
			promise.resolve(loaded)
		}

		Function("isReady") { self.bridge.isReady() }
		Function("getBpm") { Double(self.bridge.getBpm()) }
		Function("getFrameCount") { Int(self.bridge.getFrameCount()) }

		// MARK: - Key Detection (MusicalKeyCNN)

		AsyncFunction("loadKeyModel") { (promise: Promise) in
			guard let modelPath = self.findResourcePath(name: "keynet", ext: "onnx") else {
				promise.resolve(false)
				return
			}

			self.debugLog("Loading MusicalKeyCNN model from: \(modelPath)")
			let loaded = self.bridge.loadKeyModel(modelPath)
			if loaded {
				self.debugLog("MusicalKeyCNN loaded, running warm-up inference...")
				let warmedUp = self.bridge.warmUpKey()
				self.debugLog("MusicalKeyCNN warm-up complete: \(warmedUp)")
			}
			promise.resolve(loaded)
		}

		Function("isKeyReady") { self.bridge.isKeyReady() }

		Function("getKey") { () -> [String: Any]? in
			let result = self.bridge.getKey()
			guard result.valid else { return nil }
			return [
				"camelot": result.camelot,
				"notation": result.notation,
				"confidence": Double(result.confidence)
			]
		}

		Function("getKeyFrameCount") { Int(self.bridge.getKeyFrameCount()) }

		// MARK: - Audio Processing

		Function("processAudio") { (samples: [Double]) -> [[String: Any]]? in
			let floatSamples = samples.map { NSNumber(value: Float($0)) }
			guard let results = self.bridge.processAudio(floatSamples) else {
				return nil
			}
			return results.map { result in
				[
					"beatActivation": Double(result.beatActivation),
					"downbeatActivation": Double(result.downbeatActivation)
				]
			}
		}

		// MARK: - Audio Capture

		AsyncFunction("requestPermission") { (promise: Promise) in
			AVAudioSession.sharedInstance().requestRecordPermission { granted in
				promise.resolve(granted)
			}
		}

		Function("getPermissionStatus") {
			switch AVAudioSession.sharedInstance().recordPermission {
			case .granted:
				return "granted"
			case .denied:
				return "denied"
			case .undetermined:
				return "undetermined"
			@unknown default:
				return "undetermined"
			}
		}

		AsyncFunction("startRecording") { (enableWaveform: Bool, promise: Promise) in
			if self.isRecordingAudio {
				promise.resolve(true)
				return
			}

			let permission = AVAudioSession.sharedInstance().recordPermission
			if permission != .granted {
				self.debugLog("Microphone permission not granted")
				promise.resolve(false)
				return
			}

			self.enableWaveformEvents = enableWaveform

			do {
				try self.setupAudioSession()
				try self.startAudioEngine()
				promise.resolve(true)
			} catch {
				self.debugLog("Failed to start recording: \(error)")
				promise.resolve(false)
			}
		}

		Function("stopRecording") { self.stopAudioEngine() }
		Function("isRecording") { self.isRecordingAudio }
	}

	// MARK: - Audio Engine

	private func setupAudioSession() throws {
		let session = AVAudioSession.sharedInstance()
		try session.setCategory(.playAndRecord, mode: .measurement, options: [.defaultToSpeaker, .allowBluetoothA2DP])
		// Use 44100 Hz for native sample rate (key detection + resampled BPM)
		try session.setPreferredSampleRate(44100)
		try session.setActive(true)
	}

	private func startAudioEngine() throws {
		if isRecordingAudio {
			return
		}

		audioEngine = AVAudioEngine()
		guard let audioEngine = audioEngine else { return }

		let inputNode = audioEngine.inputNode
		let inputFormat = inputNode.outputFormat(forBus: 0)

		// Target format: 44100 Hz mono (native for both BPM via resampling and key detection)
		guard let targetFormat = AVAudioFormat(
			commonFormat: .pcmFormatFloat32,
			sampleRate: 44100,
			channels: 1,
			interleaved: false
		) else {
			throw NSError(domain: "EngineModule", code: 1, userInfo: [NSLocalizedDescriptionKey: "Failed to create audio format"])
		}

		let converter = AVAudioConverter(from: inputFormat, to: targetFormat)
		// Buffer size for ~20ms at input sample rate (good for real-time processing)
		let bufferSize = AVAudioFrameCount(inputFormat.sampleRate / 50.0)
		let conversionCapacity = AVAudioFrameCount(ceil(Double(bufferSize) * targetFormat.sampleRate / inputFormat.sampleRate)) + 32
		let conversionBuffer = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: conversionCapacity)

		bridge.reset()
		lastKeyNotation = ""
		lastKeyCamelot = ""
		lastKeyConfidence = 0
		waveformWriteIndex = 0
		waveformSamplesAccumulated = 0
		waveformEmitCount = 0
		processCallCount = 0
		recordingStartTime = Date().timeIntervalSince1970
		lastStateEmitTime = 0
		lastWaveformEmitTime = 0

		inputNode.installTap(onBus: 0, bufferSize: bufferSize, format: inputFormat) { [weak self] buffer, _ in
			guard let self = self else { return }

			if inputFormat.sampleRate == 44100 {
				guard let data = buffer.floatChannelData?[0] else { return }
				self.processAudioSamples(data, count: Int(buffer.frameLength))
				return
			}
			guard let converter = converter, let conversionBuffer = conversionBuffer else {
				return
			}
			let sampleCount = self.convert(buffer: buffer, converter: converter, outputBuffer: conversionBuffer)
			if sampleCount <= 0 {
				return
			}
			guard let data = conversionBuffer.floatChannelData?[0] else { return }
			self.processAudioSamples(data, count: sampleCount)
		}

		audioEngine.prepare()
		try audioEngine.start()
		isRecordingAudio = true
		debugLog("Audio engine started at \(inputFormat.sampleRate)Hz -> 44100Hz, buffer: \(bufferSize), waveform: \(enableWaveformEvents)")
	}

	private func stopAudioEngine() {
		audioEngine?.inputNode.removeTap(onBus: 0)
		audioEngine?.stop()
		audioEngine = nil
		isRecordingAudio = false
		lastStateEmitTime = 0
		lastWaveformEmitTime = 0

		try? AVAudioSession.sharedInstance().setCategory(.playback, mode: .default)
		debugLog("Audio engine stopped")
	}

	private func computeFrequencyBands(_ samples: [Float]) -> (low: Float, mid: Float, high: Float) {
		guard samples.count >= fftSize, let setup = fftSetup else {
			return (0, 0, 0)
		}

		var windowedSamples = [Float](repeating: 0, count: fftSize)
		vDSP_vmul(samples, 1, fftWindow, 1, &windowedSamples, 1, vDSP_Length(fftSize))

		var realIn = [Float](repeating: 0, count: fftSize)
		var imagIn = [Float](repeating: 0, count: fftSize)
		var realOut = [Float](repeating: 0, count: fftSize)
		var imagOut = [Float](repeating: 0, count: fftSize)

		realIn = windowedSamples

		vDSP_DFT_Execute(setup, &realIn, &imagIn, &realOut, &imagOut)

		let halfSize = fftSize / 2
		var magnitudes = [Float](repeating: 0, count: halfSize)
		for i in 0..<halfSize {
			magnitudes[i] = sqrt(realOut[i] * realOut[i] + imagOut[i] * imagOut[i])
		}

		let lowEnd = 3
		let midEnd = 29

		var lowEnergy: Float = 0
		var midEnergy: Float = 0
		var highEnergy: Float = 0

		for i in 1..<halfSize {
			if i < lowEnd {
				lowEnergy += magnitudes[i]
			} else if i < midEnd {
				midEnergy += magnitudes[i]
			} else {
				highEnergy += magnitudes[i]
			}
		}

		let total = lowEnergy + midEnergy + highEnergy
		if total > 0 {
			return (lowEnergy / total, midEnergy / total, highEnergy / total)
		}
		return (0.33, 0.33, 0.34)
	}

	private func convert(buffer: AVAudioPCMBuffer, converter: AVAudioConverter, outputBuffer: AVAudioPCMBuffer) -> Int {
		let ratio = outputBuffer.format.sampleRate / buffer.format.sampleRate
		let outputFrameCount = AVAudioFrameCount(Double(buffer.frameLength) * ratio)
		if outputFrameCount > outputBuffer.frameCapacity {
			return 0
		}
		outputBuffer.frameLength = outputFrameCount
		var error: NSError?
		var inputConsumed = false
		let inputBlock: AVAudioConverterInputBlock = { _, outStatus in
			if inputConsumed {
				outStatus.pointee = .noDataNow
				return nil
			}
			inputConsumed = true
			outStatus.pointee = .haveData
			return buffer
		}

		let status = converter.convert(to: outputBuffer, error: &error, withInputFrom: inputBlock)
		if let error = error {
			debugLog("Conversion error: \(error)")
			return 0
		}
		if status == .error {
			return 0
		}
		return Int(outputBuffer.frameLength)
	}

	private var processCallCount = 0
	private var recordingStartTime: TimeInterval = 0

	private func processAudioSamples(_ samples: UnsafePointer<Float>, count: Int) {
		processCallCount += 1
		guard count > 0 else { return }

		var beatActivation: Float = 0
		var downbeatActivation: Float = 0
		let hasState = bridge.processAudioBuffer(
			samples,
			sampleCount: count,
			beatActivation: &beatActivation,
			downbeatActivation: &downbeatActivation
		)

		let now = Date().timeIntervalSince1970
		let timestamp = now - recordingStartTime

		// Emit BPM state events (if available)
		if hasState && now - lastStateEmitTime >= stateEmitInterval {
			lastStateEmitTime = now
			sendEvent("onState", [
				"beatActivation": Double(beatActivation),
				"downbeatActivation": Double(downbeatActivation),
				"timestamp": timestamp
			])
		}

		// Check for key detection updates (emit on key change OR significant confidence change)
		let keyResult = bridge.getKey()
		let confidenceChanged = abs(keyResult.confidence - lastKeyConfidence) > 0.01
		let keyChanged = keyResult.notation != lastKeyNotation || keyResult.camelot != lastKeyCamelot

		if keyResult.valid && (keyChanged || confidenceChanged) {
			lastKeyNotation = keyResult.notation
			lastKeyCamelot = keyResult.camelot
			lastKeyConfidence = keyResult.confidence
			sendEvent("onKey", [
				"camelot": keyResult.camelot,
				"notation": keyResult.notation,
				"confidence": Double(keyResult.confidence),
				"timestamp": timestamp
			])
		}

		// Waveform processing
		if enableWaveformEvents {
			for i in 0..<count {
				let sample = samples[i]
				waveformInputBuffer[waveformWriteIndex] = sample
				waveformWriteIndex = (waveformWriteIndex + 1) % waveformInputSize
				waveformSamplesAccumulated += 1
			}

			// Emit waveform more frequently at 44100 Hz (adjust threshold)
			let waveformThreshold = waveformInputSize * 2  // Account for higher sample rate
			if waveformSamplesAccumulated >= waveformThreshold {
				waveformSamplesAccumulated = 0
				if now - lastWaveformEmitTime < waveformEmitInterval {
					return
				}
				lastWaveformEmitTime = now

				var peak: Float = 0
				var sumSquares: Float = 0
				let samplesPerPoint = waveformInputSize / waveformBufferSize
				var downsampledPoints = [Double](repeating: 0, count: waveformBufferSize)

				for i in 0..<waveformBufferSize {
					var sum: Float = 0
					for j in 0..<samplesPerPoint {
						let readIndex = (waveformWriteIndex + i * samplesPerPoint + j) % waveformInputSize
						let sample = waveformInputBuffer[readIndex]
						sum += sample
						let absVal = abs(sample)
						if absVal > peak { peak = absVal }
						sumSquares += sample * sample
					}
					downsampledPoints[i] = Double(sum / Float(samplesPerPoint))
				}

				let rms = sqrt(sumSquares / Float(waveformInputSize))

				var orderedSamples = [Float](repeating: 0, count: waveformInputSize)
				for i in 0..<waveformInputSize {
					let readIndex = (waveformWriteIndex + i) % waveformInputSize
					orderedSamples[i] = waveformInputBuffer[readIndex]
				}
				let bands = computeFrequencyBands(orderedSamples)

				waveformEmitCount += 1
				if waveformEmitCount == 1 {
					debugLog("First waveform emit - peak: \(peak), rms: \(rms), bands: \(bands)")
				}
				sendEvent("onWaveform", [
					"samples": downsampledPoints,
					"peak": Double(peak),
					"rms": Double(rms),
					"low": Double(bands.low),
					"mid": Double(bands.mid),
					"high": Double(bands.high)
				])
			}
		}
	}
}
