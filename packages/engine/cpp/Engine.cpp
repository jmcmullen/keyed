/**
 * Engine - Audio processing for BPM detection
 */

#include "Engine.hpp"
#include <cmath>

namespace engine {

Engine::Engine()
    : melExtractor_(std::make_unique<StreamingMelExtractor>()),
      onnxModel_(std::make_unique<OnnxModel>()) {
}

Engine::~Engine() = default;

void Engine::reset() {
    melExtractor_->reset();
    if (onnxModel_) {
        onnxModel_->resetState();
    }
    activationBuffer_.clear();
}

bool Engine::loadModel(const std::string& modelPath) {
    if (!onnxModel_) {
        onnxModel_ = std::make_unique<OnnxModel>();
    }
    return onnxModel_->load(modelPath);
}

bool Engine::isReady() const {
    return onnxModel_ && onnxModel_->isReady();
}

bool Engine::warmUp() {
    if (!isReady()) {
        return false;
    }

    // Run a few dummy inferences to trigger CoreML/NNAPI compilation
    float dummyFeatures[FEATURE_DIM] = {0};
    ModelOutput output;

    for (int i = 0; i < 5; i++) {
        if (!onnxModel_->infer(dummyFeatures, output)) {
            return false;
        }
    }

    // Reset LSTM state after warm-up since we fed garbage
    onnxModel_->resetState();

    return true;
}

int Engine::processAudio(const float* samples, int numSamples,
                         FrameResult* outResults, int maxResults) {
    if (!isReady()) {
        return 0;
    }

    // Extract mel features
    static constexpr int MAX_FRAMES = 64;
    float features[MAX_FRAMES * FEATURE_DIM];
    int numFrames = melExtractor_->push(samples, numSamples, features, MAX_FRAMES);

    if (numFrames == 0) {
        return 0;
    }

    int resultsProduced = 0;

    // Process each frame through ONNX
    for (int i = 0; i < numFrames && resultsProduced < maxResults; i++) {
        float* frameFeatures = &features[i * FEATURE_DIM];

        // Run ONNX inference
        ModelOutput modelOutput;
        if (!onnxModel_->infer(frameFeatures, modelOutput)) {
            continue;
        }

        float currBeatAct = modelOutput.beatActivation;
        float currDownAct = modelOutput.downbeatActivation;

        // Collect activations for autocorrelation BPM
        activationBuffer_.push(currBeatAct, currDownAct);

        // Fill result
        FrameResult& result = outResults[resultsProduced];
        result.beatActivation = currBeatAct;
        result.downbeatActivation = currDownAct;

        resultsProduced++;
    }

    return resultsProduced;
}

float Engine::getBpm() const {
    return activationBuffer_.getCachedBpm();
}

size_t Engine::getFrameCount() const {
    return activationBuffer_.size();
}

} // namespace engine
