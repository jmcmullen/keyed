#pragma once

#include "MelExtractor.hpp"
#include "CqtExtractor.hpp"
#include "OnnxModel.hpp"
#include "KeyModel.hpp"
#include "Resampler.hpp"
#include "AutocorrBpm.hpp"
#include <memory>
#include <string>
#include <vector>

namespace engine {

/**
 * Engine - Audio processing for BPM and key detection
 *
 * Usage:
 *   1. loadModels() - load both ONNX models (BeatNet + MusicalKeyCNN)
 *   2. processAudio() - feed audio samples at 44100Hz
 *   3. getBpm() - get detected BPM (after ~2 seconds of audio)
 *   4. getKey() - get detected key (after ~20 seconds of audio)
 *
 * Audio Pipeline:
 *   44100 Hz audio ─┬─> CQT extractor ─> KeyModel ─> Key detection
 *                   │
 *                   └─> Resample (2:1) ─> Mel extractor ─> BeatNet ─> BPM detection
 */
class Engine {
public:
	Engine();
	~Engine();

	// Non-copyable
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

	/**
	 * Reset all processing state
	 */
	void reset();

	// =========================================================================
	// BPM Detection (BeatNet)
	// =========================================================================

	/**
	 * Load BeatNet ONNX model
	 * @param modelPath Path to beatnet.onnx model file
	 * @return true if loaded successfully
	 */
	bool loadModel(const std::string& modelPath);

	/**
	 * Check if BeatNet model is loaded and ready
	 */
	bool isReady() const;

	/**
	 * Warm-up BeatNet inference to pre-compile model
	 */
	bool warmUp();

	/**
	 * Result from processing one audio frame (BPM)
	 */
	struct FrameResult {
		float beatActivation;      // 0-1, beat likelihood
		float downbeatActivation;  // 0-1, downbeat likelihood
	};

	/**
	 * Get detected BPM (0 if not enough data yet, ~2 seconds needed)
	 */
	float getBpm() const;

	/**
	 * Get number of BPM frames processed
	 */
	size_t getFrameCount() const;

	// =========================================================================
	// Key Detection (MusicalKeyCNN)
	// =========================================================================

	/**
	 * Load MusicalKeyCNN ONNX model
	 * @param modelPath Path to keynet.onnx model file
	 * @return true if loaded successfully
	 */
	bool loadKeyModel(const std::string& modelPath);

	/**
	 * Check if MusicalKeyCNN model is loaded and ready
	 */
	bool isKeyReady() const;

	/**
	 * Warm-up MusicalKeyCNN inference
	 */
	bool warmUpKey();

	/**
	 * Key detection result
	 */
	struct KeyResult {
		std::string camelot;   // Camelot notation: "1A" - "12B"
		std::string notation;  // Musical notation: "Am", "C", etc.
		float confidence;      // 0-1, softmax probability
		bool valid;            // true if key has been detected
	};

	/**
	 * Get detected key (invalid if not enough data yet, ~20 seconds needed)
	 */
	KeyResult getKey() const;

	/**
	 * Get number of CQT frames accumulated
	 */
	size_t getKeyFrameCount() const;

	// =========================================================================
	// Audio Processing
	// =========================================================================

	/**
	 * Process audio at 44100 Hz (native sample rate)
	 * Handles both BPM detection (via resampling) and key detection
	 *
	 * @param samples Audio samples at 44100Hz
	 * @param numSamples Number of samples
	 * @param outResults Output buffer for BPM frame results (optional, can be nullptr)
	 * @param maxResults Maximum BPM results to output
	 * @return Number of BPM results produced
	 */
	int processAudio(const float* samples, int numSamples,
	                 FrameResult* outResults, int maxResults);

	/**
	 * Process audio at 22050 Hz for BPM only (legacy compatibility)
	 * Does NOT process key detection
	 */
	int processAudioForBpm(const float* samples, int numSamples,
	                       FrameResult* outResults, int maxResults);

	// Constants
	static constexpr int SAMPLE_RATE = 44100;           // Native sample rate
	static constexpr int BPM_SAMPLE_RATE = MelConfig::SAMPLE_RATE;  // 22050 Hz
	static constexpr int KEY_SAMPLE_RATE = CqtConfig::SAMPLE_RATE;  // 44100 Hz
	static constexpr int HOP_LENGTH = MelConfig::HOP_LENGTH;
	static constexpr int FEATURE_DIM = MelConfig::MODEL_INPUT_DIM;
	static constexpr float BPM_FPS = 50.0f;
	static constexpr float KEY_FPS = 5.0f;
	static constexpr int KEY_MIN_FRAMES = 100;         // Minimum frames for first inference (~20 sec)
	static constexpr int KEY_INFERENCE_INTERVAL = 25;  // Run inference every N new frames (~5 sec)

private:
	// Run key inference on accumulated CQT frames
	void runKeyInference();

	// BPM detection
	std::unique_ptr<StreamingMelExtractor> melExtractor_;
	std::unique_ptr<OnnxModel> beatnetModel_;
	std::unique_ptr<Resampler> resampler_;
	ActivationBuffer activationBuffer_;

	// Key detection
	std::unique_ptr<StreamingCqtExtractor> cqtExtractor_;
	std::unique_ptr<KeyModel> keyModel_;
	std::vector<float> cqtBuffer_;           // CQT frame buffer (grows with recording)
	size_t cqtFrameCount_ = 0;               // Total frames accumulated
	size_t cqtFramesSinceInference_ = 0;     // Frames since last inference
	KeyResult currentKey_;                    // Latest key detection result
	int keyInferenceCount_ = 0;              // Number of inferences performed

	// Resampling buffer
	std::vector<float> resampleBuffer_;
};

} // namespace engine
