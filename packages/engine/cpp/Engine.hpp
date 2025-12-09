#pragma once

#include "MelExtractor.hpp"
#include "OnnxModel.hpp"
#include "AutocorrBpm.hpp"
#include <memory>
#include <string>
#include <vector>

namespace engine {

/**
 * Engine - Audio processing for BPM detection
 *
 * Usage:
 *   1. loadModel() - load ONNX model
 *   2. processAudio() - feed audio samples, get beat activations
 *   3. getBpm() - get detected BPM (after ~2 seconds of audio)
 */
class Engine {
public:
    Engine();
    ~Engine();

    /**
     * Reset all processing state
     */
    void reset();

    /**
     * Load ONNX model
     * @param modelPath Path to .onnx model file
     * @return true if loaded successfully
     */
    bool loadModel(const std::string& modelPath);

    /**
     * Check if model is loaded and ready
     */
    bool isReady() const;

    /**
     * Warm-up inference to pre-compile model
     * Call after loadModel() to avoid latency during real-time processing.
     */
    bool warmUp();

    /**
     * Result from processing one audio frame
     */
    struct FrameResult {
        float beatActivation;      // 0-1, beat likelihood
        float downbeatActivation;  // 0-1, downbeat likelihood
    };

    /**
     * Process audio samples
     * @param samples Audio samples at 22050Hz
     * @param numSamples Number of samples
     * @param outResults Output buffer for results
     * @param maxResults Maximum results to output
     * @return Number of results produced
     */
    int processAudio(const float* samples, int numSamples,
                     FrameResult* outResults, int maxResults);

    /**
     * Get detected BPM (0 if not enough data yet, ~2 seconds needed)
     */
    float getBpm() const;

    /**
     * Get number of frames processed (100 frames ≈ 2 seconds)
     */
    size_t getFrameCount() const;

    // Constants
    static constexpr int SAMPLE_RATE = MelConfig::SAMPLE_RATE;
    static constexpr int HOP_LENGTH = MelConfig::HOP_LENGTH;
    static constexpr int FEATURE_DIM = MelConfig::MODEL_INPUT_DIM;
    static constexpr float FPS = 50.0f;

private:
    std::unique_ptr<StreamingMelExtractor> melExtractor_;
    std::unique_ptr<OnnxModel> onnxModel_;
    ActivationBuffer activationBuffer_;
};

} // namespace engine
