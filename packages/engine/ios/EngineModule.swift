import ExpoModulesCore
import AVFoundation
import Accelerate

public class EngineModule: Module {
    private let bridge = EngineBridge.shared()
    private var audioEngine: AVAudioEngine?
    private var isRecordingAudio = false
    private var enableWaveformEvents = false

    private let waveformBufferSize = 128
    private let waveformInputSize = 256
    private lazy var waveformInputBuffer: [Float] = [Float](repeating: 0, count: waveformInputSize)
    private var waveformWriteIndex = 0
    private var waveformSamplesAccumulated = 0
    private var waveformEmitCount = 0

    // FFT setup for frequency analysis
    private let fftSize = 256
    private lazy var fftSetup: vDSP_DFT_Setup? = vDSP_DFT_zop_CreateSetup(nil, vDSP_Length(fftSize), .FORWARD)
    private lazy var fftWindow: [Float] = {
        var window = [Float](repeating: 0, count: fftSize)
        vDSP_hann_window(&window, vDSP_Length(fftSize), Int32(vDSP_HANN_NORM))
        return window
    }()

    public func definition() -> ModuleDefinition {
        Name("Engine")

        Constants([
            "SAMPLE_RATE": 22050,
            "FPS": 50
        ])

        Events("onState", "onWaveform")

        Function("reset") { self.bridge.reset() }
        Function("loadModel") { () -> Bool in
            let bundle = Bundle(for: type(of: self))
            guard let modelPath = bundle.path(forResource: "beatnet", ofType: "onnx") else {
                print("[EngineModule] Failed to find bundled beatnet.onnx model")
                return false
            }

            print("[EngineModule] Loading model from: \(modelPath)")
            let loaded = self.bridge.loadModel(modelPath)
            if loaded {
                print("[EngineModule] Model loaded, running warm-up inference...")
                let warmedUp = self.bridge.warmUp()
                print("[EngineModule] Warm-up complete: \(warmedUp)")
            }
            return loaded
        }
        Function("isReady") { self.bridge.isReady() }

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

        Function("getBpm") { Double(self.bridge.getBpm()) }
        Function("getFrameCount") { Int(self.bridge.getFrameCount()) }

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
            let permission = AVAudioSession.sharedInstance().recordPermission
            if permission != .granted {
                print("[EngineModule] Microphone permission not granted")
                promise.resolve(false)
                return
            }

            self.enableWaveformEvents = enableWaveform

            do {
                try self.setupAudioSession()
                try self.startAudioEngine()
                promise.resolve(true)
            } catch {
                print("[EngineModule] Failed to start recording: \(error)")
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
        try session.setPreferredSampleRate(22050)
        try session.setActive(true)
    }

    private func startAudioEngine() throws {
        audioEngine = AVAudioEngine()
        guard let audioEngine = audioEngine else { return }

        let inputNode = audioEngine.inputNode
        let inputFormat = inputNode.outputFormat(forBus: 0)

        guard let targetFormat = AVAudioFormat(
            commonFormat: .pcmFormatFloat32,
            sampleRate: 22050,
            channels: 1,
            interleaved: false
        ) else {
            throw NSError(domain: "EngineModule", code: 1, userInfo: [NSLocalizedDescriptionKey: "Failed to create audio format"])
        }

        let converter = AVAudioConverter(from: inputFormat, to: targetFormat)
        let bufferSize = AVAudioFrameCount(inputFormat.sampleRate / 50.0)
        inputNode.installTap(onBus: 0, bufferSize: bufferSize, format: inputFormat) { [weak self] buffer, time in
            guard let self = self else { return }

            var samples: [Float]
            if inputFormat.sampleRate == 22050 {
                samples = self.extractSamples(from: buffer)
            } else if let converter = converter {
                samples = self.convertAndExtract(buffer: buffer, converter: converter, targetFormat: targetFormat)
            } else {
                return
            }
            self.processAudioSamples(samples)
        }

        audioEngine.prepare()
        try audioEngine.start()
        isRecordingAudio = true
        bridge.reset()
        waveformWriteIndex = 0
        waveformSamplesAccumulated = 0
        waveformEmitCount = 0
        processCallCount = 0
        recordingStartTime = Date().timeIntervalSince1970
        print("[EngineModule] Audio engine started at \(inputFormat.sampleRate)Hz, buffer: \(bufferSize), waveform: \(enableWaveformEvents)")
    }

    private func stopAudioEngine() {
        audioEngine?.inputNode.removeTap(onBus: 0)
        audioEngine?.stop()
        audioEngine = nil
        isRecordingAudio = false

        try? AVAudioSession.sharedInstance().setCategory(.playback, mode: .default)
        print("[EngineModule] Audio engine stopped")
    }

    private func extractSamples(from buffer: AVAudioPCMBuffer) -> [Float] {
        guard let channelData = buffer.floatChannelData?[0] else { return [] }
        return Array(UnsafeBufferPointer(start: channelData, count: Int(buffer.frameLength)))
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

    private func convertAndExtract(buffer: AVAudioPCMBuffer, converter: AVAudioConverter, targetFormat: AVAudioFormat) -> [Float] {
        let ratio = targetFormat.sampleRate / buffer.format.sampleRate
        let outputFrameCount = AVAudioFrameCount(Double(buffer.frameLength) * ratio)

        guard let outputBuffer = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: outputFrameCount) else {
            return []
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

        converter.convert(to: outputBuffer, error: &error, withInputFrom: inputBlock)
        if let error = error {
            print("[EngineModule] Conversion error: \(error)")
            return []
        }

        return extractSamples(from: outputBuffer)
    }

    private var processCallCount = 0
    private var recordingStartTime: TimeInterval = 0

    private func processAudioSamples(_ samples: [Float]) {
        processCallCount += 1

        if samples.isEmpty {
            print("[EngineModule] Empty samples received")
            return
        }

        if processCallCount == 1 {
            print("[EngineModule] First audio callback - processing \(samples.count) samples...")
        }

        let nsSamples = samples.map { NSNumber(value: $0) }
        guard let results = bridge.processAudio(nsSamples) else {
            if processCallCount <= 5 {
                print("[EngineModule] No results from bridge (call #\(processCallCount), samples: \(samples.count))")
            }
            return
        }

        if processCallCount == 1 {
            print("[EngineModule] First results received: \(results.count) frames")
        }

        let timestamp = Date().timeIntervalSince1970 - recordingStartTime
        for result in results {
            sendEvent("onState", [
                "beatActivation": Double(result.beatActivation),
                "downbeatActivation": Double(result.downbeatActivation),
                "timestamp": timestamp
            ])
        }

        if enableWaveformEvents {
            for sample in samples {
                waveformInputBuffer[waveformWriteIndex] = sample
                waveformWriteIndex = (waveformWriteIndex + 1) % waveformInputSize
                waveformSamplesAccumulated += 1
            }

            if waveformSamplesAccumulated >= waveformInputSize {
                waveformSamplesAccumulated = 0

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
                    print("[EngineModule] First waveform emit - peak: \(peak), rms: \(rms), bands: \(bands)")
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
