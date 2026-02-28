/**
 * Engine - Audio processing for BPM and key detection
 */

#include "Engine.hpp"
#include <cmath>
#include <algorithm>

namespace engine {

Engine::Engine()
	: melExtractor_(std::make_unique<StreamingMelExtractor>())
	, beatnetModel_(std::make_unique<OnnxModel>())
	, resampler_(std::make_unique<Resampler>(SAMPLE_RATE, BPM_SAMPLE_RATE))
	, cqtExtractor_(std::make_unique<StreamingCqtExtractor>())
	, keyModel_(std::make_unique<KeyModel>())
{
	// Pre-allocate CQT buffer for ~2 minutes of audio (600 frames at 5 FPS)
	// Buffer grows dynamically if needed
	cqtBuffer_.reserve(CqtConfig::N_BINS * 600);

	// Pre-allocate resample buffer (generous size for typical audio chunks)
	resampleBuffer_.resize(44100);

	// Initialize key result
	currentKey_ = {"", "", 0.0f, false};
}

Engine::~Engine() = default;

void Engine::reset() {
	// Reset BPM detection
	melExtractor_->reset();
	if (beatnetModel_) {
		beatnetModel_->resetState();
	}
	activationBuffer_.clear();
	resampler_->reset();

	// Reset key detection
	cqtExtractor_->reset();
	cqtBuffer_.clear();
	cqtFrameCount_ = 0;
	cqtFramesSinceInference_ = 0;
	keyInferenceCount_ = 0;
	currentKey_ = {"", "", 0.0f, false};
}

// =============================================================================
// BPM Detection (BeatNet)
// =============================================================================

bool Engine::loadModel(const std::string& modelPath) {
	if (!beatnetModel_) {
		beatnetModel_ = std::make_unique<OnnxModel>();
	}
	return beatnetModel_->load(modelPath);
}

bool Engine::isReady() const {
	return beatnetModel_ && beatnetModel_->isReady();
}

bool Engine::warmUp() {
	if (!isReady()) {
		return false;
	}

	// Run a few dummy inferences to trigger CoreML/NNAPI compilation
	float dummyFeatures[FEATURE_DIM] = {0};
	ModelOutput output;

	for (int i = 0; i < 5; i++) {
		if (!beatnetModel_->infer(dummyFeatures, output)) {
			return false;
		}
	}

	// Reset LSTM state after warm-up since we fed garbage
	beatnetModel_->resetState();

	return true;
}

float Engine::getBpm() const {
	return activationBuffer_.getCachedBpm();
}

size_t Engine::getFrameCount() const {
	return activationBuffer_.size();
}

// =============================================================================
// Key Detection (MusicalKeyCNN)
// =============================================================================

bool Engine::loadKeyModel(const std::string& modelPath) {
	if (!keyModel_) {
		keyModel_ = std::make_unique<KeyModel>();
	}
	return keyModel_->load(modelPath);
}

bool Engine::isKeyReady() const {
	return keyModel_ && keyModel_->isReady();
}

bool Engine::warmUpKey() {
	if (!isKeyReady()) {
		return false;
	}

	// Run dummy inference to trigger CoreML/NNAPI compilation
	std::vector<float> dummyCqt(KeyModel::INPUT_SIZE, 0.0f);
	KeyOutput output;

	if (!keyModel_->infer(dummyCqt.data(), output)) {
		return false;
	}

	return true;
}

Engine::KeyResult Engine::getKey() const {
	return currentKey_;
}

size_t Engine::getKeyFrameCount() const {
	return cqtFrameCount_;
}

void Engine::runKeyInference() {
	if (!isKeyReady() || cqtFrameCount_ < KEY_MIN_FRAMES) {
		return;
	}

	KeyOutput output;
	if (keyModel_->inferVariable(cqtBuffer_.data(), static_cast<int>(cqtFrameCount_), output)) {
		keyInferenceCount_++;
		cqtFramesSinceInference_ = 0;
		currentKey_.camelot = output.camelot;
		currentKey_.notation = output.notation;
		currentKey_.confidence = output.confidence;
		currentKey_.valid = true;
	}
}

// =============================================================================
// Audio Processing
// =============================================================================

int Engine::processAudio(const float* samples, int numSamples,
                         FrameResult* outResults, int maxResults) {
	// -------------------------------------------------------------------------
	// Key Detection Pipeline (44100 Hz)
	// -------------------------------------------------------------------------
	if (isKeyReady()) {
		// Extract CQT frames from input audio
		static constexpr int MAX_CQT_FRAMES = 20;
		std::vector<float> cqtFrames(CqtConfig::N_BINS * MAX_CQT_FRAMES);

		int cqtProduced = cqtExtractor_->push(samples, numSamples,
		                                       cqtFrames.data(), MAX_CQT_FRAMES);

			// Accumulate CQT frames into growing buffer (row-major: [time][freq])
		for (int i = 0; i < cqtProduced; i++) {
			// Ensure buffer has space for new frame
			size_t newSize = (cqtFrameCount_ + 1) * CqtConfig::N_BINS;
			if (cqtBuffer_.size() < newSize) {
				cqtBuffer_.resize(newSize);
			}

			// Append frame data for each frequency bin
			for (int f = 0; f < CqtConfig::N_BINS; f++) {
				cqtBuffer_[cqtFrameCount_ * CqtConfig::N_BINS + f] =
					cqtFrames[i * CqtConfig::N_BINS + f];
			}
			cqtFrameCount_++;
			cqtFramesSinceInference_++;
		}

		// Run inference when we have minimum frames, then every N new frames
		bool hasMinFrames = cqtFrameCount_ >= KEY_MIN_FRAMES;
		bool shouldRunInference = hasMinFrames &&
			(keyInferenceCount_ == 0 || cqtFramesSinceInference_ >= KEY_INFERENCE_INTERVAL);

		if (shouldRunInference) {
			runKeyInference();
		}
	}

	// -------------------------------------------------------------------------
	// BPM Detection Pipeline (resample 44100 -> 22050 Hz)
	// -------------------------------------------------------------------------
	if (!isReady()) {
		return 0;
	}

	// Resample audio using streaming mode (maintains history between calls)
	int maxOutput = resampler_->getOutputSize(numSamples) + 64;  // Extra buffer for filter overlap
	if (maxOutput > static_cast<int>(resampleBuffer_.size())) {
		resampleBuffer_.resize(maxOutput);
	}

	int actualResampled = resampler_->processStreaming(samples, numSamples,
	                                                    resampleBuffer_.data(), maxOutput);

	// Process resampled audio through BPM pipeline
	return processAudioForBpm(resampleBuffer_.data(), actualResampled, outResults, maxResults);
}

int Engine::processAudioForBpm(const float* samples, int numSamples,
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
	int totalProduced = 0;

	// Process each frame through ONNX
	for (int i = 0; i < numFrames; i++) {
		float* frameFeatures = &features[i * FEATURE_DIM];

		// Run ONNX inference
		ModelOutput modelOutput;
		if (!beatnetModel_->infer(frameFeatures, modelOutput)) {
			continue;
		}

		float currBeatAct = modelOutput.beatActivation;
		float currDownAct = modelOutput.downbeatActivation;

		// Collect activations for autocorrelation BPM
		activationBuffer_.push(currBeatAct, currDownAct);
		totalProduced++;

		// Fill result if output buffer provided
		if (outResults && resultsProduced < maxResults) {
			FrameResult& result = outResults[resultsProduced];
			result.beatActivation = currBeatAct;
			result.downbeatActivation = currDownAct;
			resultsProduced++;
		}
	}

	return outResults ? resultsProduced : totalProduced;
}

} // namespace engine
