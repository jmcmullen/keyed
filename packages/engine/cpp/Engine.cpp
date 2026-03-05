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
	// Pre-allocate 4-minute rolling CQT ring buffer (1200 frames at 5 FPS)
	cqtBuffer_.resize(CqtConfig::N_BINS * KEY_MAX_FRAMES, 0.0f);
	cqtScratch_.resize(CqtConfig::N_BINS * MAX_CQT_FRAMES_PER_PUSH, 0.0f);
	cqtInferenceBuffer_.resize(CqtConfig::N_BINS * KEY_MAX_FRAMES, 0.0f);

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
	cqtHead_ = 0;
	cqtFrameCount_ = 0;
	cqtWindowFrameCount_ = 0;
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
	if (!isKeyReady() || cqtFrameCount_ < KEY_MIN_FRAMES || cqtWindowFrameCount_ == 0) {
		return;
	}

	const int bins = CqtConfig::N_BINS;
	const int frames = static_cast<int>(cqtWindowFrameCount_);
	const float* input = cqtBuffer_.data();

	if (cqtWindowFrameCount_ == KEY_MAX_FRAMES) {
		for (size_t i = 0; i < KEY_MAX_FRAMES; i++) {
			size_t src = (cqtHead_ + i) % KEY_MAX_FRAMES;
			const float* srcFrame = &cqtBuffer_[src * bins];
			float* dstFrame = &cqtInferenceBuffer_[i * bins];
			std::copy(srcFrame, srcFrame + bins, dstFrame);
		}
		input = cqtInferenceBuffer_.data();
	}

	KeyOutput output;
	if (keyModel_->inferVariable(input, frames, output)) {
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
		int cqtProduced = cqtExtractor_->push(samples, numSamples,
		                                       cqtScratch_.data(), MAX_CQT_FRAMES_PER_PUSH);

		// Append CQT frames into a fixed 4-minute rolling ring window.
		const size_t bins = static_cast<size_t>(CqtConfig::N_BINS);
		for (int i = 0; i < cqtProduced; i++) {
			const float* src = &cqtScratch_[i * CqtConfig::N_BINS];
			if (cqtWindowFrameCount_ < KEY_MAX_FRAMES) {
				float* dst = &cqtBuffer_[cqtWindowFrameCount_ * CqtConfig::N_BINS];
				std::copy(src, src + CqtConfig::N_BINS, dst);
				cqtWindowFrameCount_++;
				cqtHead_ = cqtWindowFrameCount_ % KEY_MAX_FRAMES;
			} else {
				float* dst = &cqtBuffer_[cqtHead_ * bins];
				std::copy(src, src + CqtConfig::N_BINS, dst);
				cqtHead_ = (cqtHead_ + 1) % KEY_MAX_FRAMES;
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
	if (numSamples <= 0) {
		return 0;
	}

	static constexpr int MAX_FRAMES = 64;
	static constexpr int MAX_CHUNK_SAMPLES = MelConfig::HOP_LENGTH * 32;
	float features[MAX_FRAMES * FEATURE_DIM];
	int resultsProduced = 0;
	int totalProduced = 0;

	for (int offset = 0; offset < numSamples; offset += MAX_CHUNK_SAMPLES) {
		const int chunk = std::min(MAX_CHUNK_SAMPLES, numSamples - offset);
		const int numFrames = melExtractor_->push(
			samples + offset, chunk, features, MAX_FRAMES
		);
		if (numFrames == 0) {
			continue;
		}

		for (int i = 0; i < numFrames; i++) {
			float* frameFeatures = &features[i * FEATURE_DIM];
			ModelOutput modelOutput;
			if (!beatnetModel_->infer(frameFeatures, modelOutput)) {
				continue;
			}

			const float currBeatAct = modelOutput.beatActivation;
			const float currDownAct = modelOutput.downbeatActivation;
			activationBuffer_.push(currBeatAct, currDownAct);
			totalProduced++;

			if (outResults && resultsProduced < maxResults) {
				FrameResult& result = outResults[resultsProduced];
				result.beatActivation = currBeatAct;
				result.downbeatActivation = currDownAct;
				resultsProduced++;
			}
		}
	}

	if (outResults) {
		return resultsProduced;
	}
	return totalProduced;
}

} // namespace engine
