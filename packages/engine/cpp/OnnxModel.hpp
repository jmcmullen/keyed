#pragma once

#ifdef ONNX_ENABLED

#include <memory>
#include <string>
#include <vector>

// Forward declare ONNX Runtime types to avoid header pollution
struct OrtApi;
struct OrtEnv;
struct OrtSession;
struct OrtSessionOptions;
struct OrtMemoryInfo;
struct OrtValue;

namespace engine {

/**
 * BeatNet model output
 */
struct ModelOutput {
    float beatActivation;
    float downbeatActivation;
};

/**
 * ONNX Runtime wrapper for BeatNet CRNN model
 *
 * Model Architecture:
 * - Input: [1, 1, 272] mel features (batch=1, seq=1, features=272)
 * - Hidden/Cell: [2, 1, 150] LSTM state
 * - Output: [1, 1, 3] probabilities [beat, downbeat, non-beat]
 *
 * Maintains LSTM hidden state between inference calls for streaming.
 */
class OnnxModel {
public:
    OnnxModel();
    ~OnnxModel();

    // Non-copyable
    OnnxModel(const OnnxModel&) = delete;
    OnnxModel& operator=(const OnnxModel&) = delete;

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
     * Reset LSTM hidden state (call when starting new audio stream)
     */
    void resetState();

    /**
     * Run inference on a single frame of mel features
     * @param features 272-dimensional mel feature vector
     * @param output Output beat/downbeat activations
     * @return true if inference succeeded
     */
    bool infer(const float* features, ModelOutput& output);

    // Constants matching the model architecture
    static constexpr int INPUT_DIM = 272;
    static constexpr int HIDDEN_DIM = 150;
    static constexpr int NUM_LAYERS = 2;
    static constexpr int OUTPUT_CLASSES = 3;

private:
    void cleanup();
    void initializeLstmState();

    const OrtApi* api_ = nullptr;
    OrtEnv* env_ = nullptr;
    OrtSession* session_ = nullptr;
    OrtSessionOptions* sessionOptions_ = nullptr;
    OrtMemoryInfo* memoryInfo_ = nullptr;

    // LSTM hidden state (persisted between inference calls)
    std::vector<float> hidden_;
    std::vector<float> cell_;

    // Input/output names
    std::vector<const char*> inputNames_;
    std::vector<const char*> outputNames_;

    bool isLoaded_ = false;
};

} // namespace engine

#else // !ONNX_ENABLED

// Stub implementation when ONNX is not available
namespace engine {

struct ModelOutput {
    float beatActivation = 0.0f;
    float downbeatActivation = 0.0f;
};

class OnnxModel {
public:
    OnnxModel() = default;
    ~OnnxModel() = default;

    bool load(const std::string&) { return false; }
    bool isReady() const { return false; }
    void resetState() {}
    bool infer(const float*, ModelOutput&) { return false; }

    static constexpr int INPUT_DIM = 272;
    static constexpr int HIDDEN_DIM = 150;
    static constexpr int NUM_LAYERS = 2;
    static constexpr int OUTPUT_CLASSES = 3;
};

} // namespace engine

#endif // ONNX_ENABLED
