import ExpoModulesCore
import AVFoundation
import Accelerate

public class EngineModule: Module {
	private lazy var bridge: EngineBridge = EngineBridge.shared()
	private var audioEngine: AVAudioEngine?
	private var isRecordingAudio = false
	private var graphConnected = false
	private var sinkNode: AVAudioSinkNode?
	private let stateQ = DispatchQueue(label: "services.session.keyed.engine.state")
	private var enableWaveformEvents = false
	private let procQ = DispatchQueue(label: "services.session.keyed.engine.proc")
	private let targetSampleRate = 44100.0
	private lazy var inputBuffer = EngineAudioBuffer(capacity: 48000 * 10, targetSampleRate: targetSampleRate)
	private let processBufferSize = 1024
	private lazy var processBuffer = [Float](repeating: 0, count: processBufferSize)
	private let drainInterval: TimeInterval = 0.005

	private let waveformBufferSize = 128
	private let waveformInputSize = 256
	private lazy var waveformInputBuffer: [Float] = [Float](repeating: 0, count: waveformInputSize)
	private var waveformWriteIndex = 0
	private var waveformSamplesAccumulated = 0
	private var waveformEmitCount = 0
	private let stateEmitInterval: TimeInterval = 1.0 / 20.0
	private let visualEmitInterval: TimeInterval = 1.0 / 60.0
	private let waveformEmitInterval: TimeInterval = 1.0 / 12.0
	private var lastStateEmitTime: TimeInterval = 0
	private var lastVisualEmitTime: TimeInterval = 0
	private var lastWaveformEmitTime: TimeInterval = 0
	private var inputLogCount = 0
	private var processedChunkCount = 0
	private var stateEmitCount = 0
	private var visualEmitCount = 0
	private var lastAudioStatsLogTime: TimeInterval = 0

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

	private func routeDescription(_ session: AVAudioSession) -> String {
		let inputs = session.currentRoute.inputs
			.map { "\($0.portName):\($0.portType.rawValue)" }
			.joined(separator: ",")
		let outputs = session.currentRoute.outputs
			.map { "\($0.portName):\($0.portType.rawValue)" }
			.joined(separator: ",")
		return "inputs=[\(inputs)] outputs=[\(outputs)]"
	}

	private func sampleStats(_ samples: UnsafePointer<Float>, count: Int) -> (peak: Float, rms: Float) {
		var peak: Float = 0
		var sumSquares: Float = 0
		for i in 0..<count {
			let sample = samples[i]
			let absVal = abs(sample)
			if absVal > peak { peak = absVal }
			sumSquares += sample * sample
		}
		return (peak, sqrt(sumSquares / Float(count)))
	}

	private func keepSpeakerRoute(_ session: AVAudioSession) {
		let builtIn = session.currentRoute.outputs.contains {
			$0.portType == .builtInReceiver || $0.portType == .builtInSpeaker
		}
		if !builtIn {
			return
		}
		do {
			try session.overrideOutputAudioPort(.speaker)
			debugLog("Audio output forced to built-in speaker")
		} catch {
			debugLog("Failed to force speaker route: \(error)")
		}
	}

	private func isRecording() -> Bool {
		stateQ.sync { isRecordingAudio }
	}

	private func setRecording(_ next: Bool) {
		stateQ.sync {
			isRecordingAudio = next
		}
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

		Events("onState", "onWaveform", "onKey", "onVisual")

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
		Function("getBpmConfidence") { Double(self.bridge.getBpmConfidence()) }
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
			if self.isRecording() {
				self.debugLog("startRecording called while already recording")
				promise.resolve(true)
				return
			}

			let permission = AVAudioSession.sharedInstance().recordPermission
			self.debugLog("startRecording requested - permission: \(permission.rawValue), waveform: \(enableWaveform)")
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
				try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
				self.debugLog("Failed to start recording: \(error)")
				promise.resolve(false)
			}
		}

		Function("stopRecording") { self.stopAudioEngine() }
		Function("isRecording") { self.isRecording() }
		Function("getDebugState") { () -> [String: Any] in
			let session = AVAudioSession.sharedInstance()
			return [
				"recording": self.isRecording(),
				"engineRunning": self.audioEngine?.isRunning ?? false,
				"graphConnected": self.graphConnected,
				"waveform": self.enableWaveformEvents,
				"tapCallbacks": self.inputBuffer.writes,
				"tapDrops": self.inputBuffer.unsupportedBuffers,
				"resampledBuffers": self.inputBuffer.resampleReads,
				"lastTapFrames": self.inputBuffer.lastInputFrames,
				"lastTapRate": self.inputBuffer.sourceSampleRate,
				"lastTapFormat": self.inputBuffer.formatSummary,
				"inputLogs": self.inputLogCount,
				"processedChunks": self.processedChunkCount,
				"queuedChunks": self.inputBuffer.queuedFrames,
				"drops": self.inputBuffer.droppedFrames,
				"inputPeak": self.inputBuffer.peak,
				"inputRms": self.inputBuffer.rms,
				"writtenFrames": self.inputBuffer.writtenFrames,
				"outputFrames": self.inputBuffer.outputFrames,
				"stateEmits": self.stateEmitCount,
				"visualEmits": self.visualEmitCount,
				"waveformEmits": self.waveformEmitCount,
				"frameCount": Int(self.bridge.getFrameCount()),
				"bpm": Double(self.bridge.getBpm()),
				"bpmConfidence": Double(self.bridge.getBpmConfidence()),
				"sampleRate": session.sampleRate,
				"inputAvailable": session.isInputAvailable,
				"route": self.routeDescription(session)
			]
		}
		}

	// MARK: - Audio Engine

	private func setupAudioSession() throws {
		let session = AVAudioSession.sharedInstance()
		let opts: AVAudioSession.CategoryOptions = [
			.defaultToSpeaker,
			.allowBluetoothA2DP,
			.mixWithOthers
		]
		try session.setCategory(.playAndRecord, mode: .default, options: opts)
		try session.setPreferredSampleRate(48000)
		try session.setPreferredIOBufferDuration(0.01)
		try session.setActive(true)
		keepSpeakerRoute(session)
		debugLog("Audio session active with mixWithOthers - sampleRate: \(session.sampleRate), inputAvailable: \(session.isInputAvailable), route: \(routeDescription(session))")
	}

	private func startAudioEngine() throws {
		if isRecording() {
			return
		}

		audioEngine = AVAudioEngine()
		guard let audioEngine = audioEngine else { return }

		let inputNode = audioEngine.inputNode
		let inputFormat = inputNode.outputFormat(forBus: 0)

		bridge.reset()
		inputBuffer.configure(with: inputFormat)
		graphConnected = false
		lastKeyNotation = ""
		lastKeyCamelot = ""
		lastKeyConfidence = 0
		waveformWriteIndex = 0
		waveformSamplesAccumulated = 0
		waveformEmitCount = 0
		inputLogCount = 0
		processedChunkCount = 0
		stateEmitCount = 0
		visualEmitCount = 0
		recordingStartTime = Date().timeIntervalSince1970
		lastStateEmitTime = 0
		lastVisualEmitTime = 0
		lastWaveformEmitTime = 0
		lastAudioStatsLogTime = 0

		let sink = AVAudioSinkNode { [weak self] _, frames, data in
			_ = self?.inputBuffer.write(data, frameCount: frames)
			return noErr
		}
		sinkNode = sink
		audioEngine.attach(sink)
		audioEngine.connect(inputNode, to: sink, format: inputFormat)
		graphConnected = true

		audioEngine.prepare()
		try audioEngine.start()
		keepSpeakerRoute(AVAudioSession.sharedInstance())
		setRecording(true)
		procQ.async { [weak self] in
			self?.drainAudio()
		}
		debugLog("Audio engine started - inputFormat: \(inputFormat), targetRate: \(targetSampleRate), waveform: \(enableWaveformEvents)")
	}

	private func stopAudioEngine() {
		setRecording(false)
		audioEngine?.stop()
		if let sinkNode {
			audioEngine?.detach(sinkNode)
		}
		sinkNode = nil
		audioEngine = nil
		graphConnected = false
		procQ.sync {}
		inputBuffer.reset()
		lastStateEmitTime = 0
		lastVisualEmitTime = 0
		lastWaveformEmitTime = 0

		try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
		debugLog("Audio engine stopped")
	}

	private func logInputBuffer(_ samples: UnsafePointer<Float>, count: Int, sampleRate: Double) {
		if inputLogCount >= 3 {
			return
		}
		inputLogCount += 1
		let stats = sampleStats(samples, count: count)
		debugLog("Input buffer \(inputLogCount) - sampleRate: \(sampleRate), count: \(count), peak: \(stats.peak), rms: \(stats.rms)")
	}

	private func drainAudio() {
		while isRecording() {
			let count = processBuffer.withUnsafeMutableBufferPointer { ptr -> Int in
				guard let base = ptr.baseAddress else { return 0 }
				return Int(inputBuffer.readSamples(base, capacity: UInt(ptr.count)))
			}

			if count == 0 {
				Thread.sleep(forTimeInterval: drainInterval)
				continue
			}

			processBuffer.withUnsafeBufferPointer { ptr in
				guard let base = ptr.baseAddress else { return }
				logInputBuffer(base, count: count, sampleRate: targetSampleRate)
				processAudioSamples(base, count: count)
			}
		}
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

	private var recordingStartTime: TimeInterval = 0

	private func processAudioSamples(_ samples: UnsafePointer<Float>, count: Int) {
		guard count > 0 else { return }

		var beatActivation: Float = 0
		var downbeatActivation: Float = 0
		let hasState = bridge.processAudioBuffer(
			samples,
			sampleCount: UInt(count),
			beatActivation: &beatActivation,
			downbeatActivation: &downbeatActivation
		)

		let now = Date().timeIntervalSince1970
		let timestamp = now - recordingStartTime
		processedChunkCount += 1
		if processedChunkCount <= 3 || now - lastAudioStatsLogTime >= 1 {
			lastAudioStatsLogTime = now
			let stats = sampleStats(samples, count: count)
			debugLog("Processed audio - chunks: \(processedChunkCount), count: \(count), peak: \(stats.peak), rms: \(stats.rms), hasState: \(hasState), frames: \(bridge.getFrameCount()), bpm: \(bridge.getBpm()), beat: \(beatActivation), downbeat: \(downbeatActivation)")
		}

		if hasState {
			if now - lastVisualEmitTime >= visualEmitInterval {
				lastVisualEmitTime = now
				visualEmitCount += 1
				sendEvent("onVisual", [
					"beatActivation": Double(beatActivation),
					"downbeatActivation": Double(downbeatActivation),
					"timestamp": timestamp
				])
			}

			if now - lastStateEmitTime >= stateEmitInterval {
				lastStateEmitTime = now
				stateEmitCount += 1
				if stateEmitCount <= 3 {
					debugLog("State emit \(stateEmitCount) - beat: \(beatActivation), downbeat: \(downbeatActivation), timestamp: \(timestamp)")
				}
				sendEvent("onState", [
					"beatActivation": Double(beatActivation),
					"downbeatActivation": Double(downbeatActivation),
					"timestamp": timestamp
				])
			}
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

			// Wait for two ring-buffer passes so waveform bands have stable data.
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
				if waveformEmitCount <= 3 || waveformEmitCount % 12 == 0 {
					debugLog("Waveform emit \(waveformEmitCount) - peak: \(peak), rms: \(rms), bands: \(bands), samples: \(downsampledPoints.count)")
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
