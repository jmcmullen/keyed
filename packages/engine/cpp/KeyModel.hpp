#pragma once

#ifdef ONNX_ENABLED

#include <memory>
#include <string>
#include <vector>

// Forward declare ONNX Runtime types
struct OrtApi;
struct OrtSession;
struct OrtSessionOptions;

namespace engine {

/**
 * Key detection output
 */
struct KeyOutput {
	int keyIndex;            // 0-23 (0-11 minor, 12-23 major)
	float confidence;        // 0-1, softmax probability of predicted key
	std::string camelot;     // Camelot notation: "1A" - "12B"
	std::string notation;    // Musical notation: "Am", "C", etc.
};

/**
 * ONNX Runtime wrapper for MusicalKeyCNN model
 *
 * Model Architecture:
 * - Input: [1, 1, 105, N] CQT spectrogram (batch=1, channel=1, freq=105, time=variable)
 * - Output: [1, 24] class logits (12 minor + 12 major keys)
 *
 * The model uses AdaptiveAvgPool2d so it accepts variable-length input.
 * Longer audio = more context = higher accuracy.
 */
class KeyModel {
public:
	KeyModel();
	~KeyModel();

	// Non-copyable
	KeyModel(const KeyModel&) = delete;
	KeyModel& operator=(const KeyModel&) = delete;

	/**
	 * Load model from file path
	 * @param modelPath Path to .onnx model file
	 * @return true if loaded successfully
	 */
	bool load(const std::string& modelPath);

	/**
	 * Check if model is loaded and ready
	 */
	bool isReady() const;

	/**
	 * Run inference on CQT spectrogram (fixed 100 frames)
	 * @param cqtSpectrogram 105x100 CQT features (log1p magnitude), row-major [freq][time]
	 * @param output Output key detection result
	 * @param probabilities Optional output buffer for all class probabilities (24 floats)
	 * @return true if inference succeeded
	 */
	bool infer(const float* cqtSpectrogram, KeyOutput& output, float* probabilities = nullptr);

	/**
	 * Run inference on CQT spectrogram (variable length)
	 * Model uses AdaptiveAvgPool2d so accepts any number of time frames >= 1
	 * More frames = more context = higher accuracy
	 *
	 * @param cqtSpectrogram CQT features, row-major [freq][time] (105 x numFrames)
	 * @param numFrames Number of time frames (should be >= 100 for good accuracy)
	 * @param output Output key detection result
	 * @return true if inference succeeded
	 */
	bool inferVariable(const float* cqtSpectrogram, int numFrames, KeyOutput& output);

	// Constants matching the model architecture
	static constexpr int INPUT_FREQ_BINS = 105;
	static constexpr int INPUT_TIME_FRAMES = 100;  // Minimum recommended
	static constexpr int INPUT_SIZE = INPUT_FREQ_BINS * INPUT_TIME_FRAMES;
	static constexpr int NUM_CLASSES = 24;

	// Key mapping tables
	static const char* const CAMELOT_KEYS[NUM_CLASSES];
	static const char* const NOTATION_KEYS[NUM_CLASSES];

private:
	void cleanup();
	static void softmax(float* logits, int size);

	const OrtApi* api_ = nullptr;
	OrtSession* session_ = nullptr;
	OrtSessionOptions* sessionOptions_ = nullptr;

	std::vector<const char*> inputNames_;
	std::vector<const char*> outputNames_;

	bool isLoaded_ = false;
};

} // namespace engine

#else // !ONNX_ENABLED

// Stub implementation when ONNX is not available
namespace engine {

struct KeyOutput {
	int keyIndex = -1;
	float confidence = 0.0f;
	std::string camelot;
	std::string notation;
};

class KeyModel {
public:
	KeyModel() = default;
	~KeyModel() = default;

	bool load(const std::string&) { return false; }
	bool isReady() const { return false; }
	bool infer(const float*, KeyOutput&, float* = nullptr) { return false; }
	bool inferVariable(const float*, int, KeyOutput&) { return false; }

	static constexpr int INPUT_FREQ_BINS = 105;
	static constexpr int INPUT_TIME_FRAMES = 100;
	static constexpr int INPUT_SIZE = INPUT_FREQ_BINS * INPUT_TIME_FRAMES;
	static constexpr int NUM_CLASSES = 24;
};

} // namespace engine

#endif // ONNX_ENABLED
