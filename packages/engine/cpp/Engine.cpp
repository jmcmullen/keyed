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
	cqtInferenceBuffer_.resize(CqtConfig::N_BINS * KEY_STABLE_FRAMES, 0.0f);

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
	keySmoother_.reset();
	currentKey_ = {"", "", 0.0f, false};
}

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

float Engine::getBpmConfidence() const {
	return activationBuffer_.getBpmConfidence();
}

size_t Engine::getFrameCount() const {
	return activationBuffer_.size();
}

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

bool Engine::copyLatestCqtFrames(int frames) {
	if (frames <= 0 || cqtWindowFrameCount_ < static_cast<size_t>(frames)) {
		return false;
	}
	const int bins = CqtConfig::N_BINS;

	if (cqtWindowFrameCount_ < KEY_MAX_FRAMES) {
		const size_t start = cqtWindowFrameCount_ - static_cast<size_t>(frames);
		const float* src = &cqtBuffer_[start * bins];
		std::copy(src, src + static_cast<size_t>(frames) * bins, cqtInferenceBuffer_.data());
		return true;
	}

	const size_t start = (cqtHead_ + KEY_MAX_FRAMES - static_cast<size_t>(frames)) % KEY_MAX_FRAMES;
	for (int i = 0; i < frames; i++) {
		const size_t src = (start + static_cast<size_t>(i)) % KEY_MAX_FRAMES;
		const float* srcFrame = &cqtBuffer_[src * bins];
		float* dstFrame = &cqtInferenceBuffer_[static_cast<size_t>(i) * bins];
		std::copy(srcFrame, srcFrame + bins, dstFrame);
	}
	return true;
}

void Engine::runKeyInference(int frames, KeyWindow window) {
	if (!isKeyReady() || !copyLatestCqtFrames(frames)) {
		return;
	}

	KeyOutput output;
	if (keyModel_->inferVariable(cqtInferenceBuffer_.data(), frames, output)) {
		KeySmootherInput input;
		input.keyIndex = output.keyIndex;
		input.confidence = output.confidence;
		input.margin = output.margin;
		input.camelot = output.camelot;
		input.notation = output.notation;
		input.window = window;

		const auto result = keySmoother_.push(input, cqtFrameCount_);
		if (result.valid) {
			currentKey_.camelot = result.camelot;
			currentKey_.notation = result.notation;
			currentKey_.confidence = result.confidence;
			currentKey_.valid = true;
		}
	}
}

void Engine::runKeyInferences() {
	if (!isKeyReady() || cqtFrameCount_ < KEY_MIN_FRAMES || cqtWindowFrameCount_ == 0) {
		return;
	}

	runKeyInference(KEY_FAST_FRAMES, KeyWindow::Fast);
	if (cqtWindowFrameCount_ >= KEY_LIVE_FRAMES) {
		runKeyInference(KEY_LIVE_FRAMES, KeyWindow::Live);
	}
	if (cqtWindowFrameCount_ >= KEY_STABLE_FRAMES) {
		runKeyInference(KEY_STABLE_FRAMES, KeyWindow::Stable);
	}

	keyInferenceCount_++;
	cqtFramesSinceInference_ = 0;
}

int Engine::processAudio(const float* samples, int numSamples,
                         FrameResult* outResults, int maxResults) {
	if (samples == nullptr || numSamples <= 0) {
		return 0;
	}

	// Key Detection Pipeline (44100 Hz)
	if (isKeyReady()) {
		// Append CQT frames into a fixed 4-minute rolling ring window.
		const size_t bins = static_cast<size_t>(CqtConfig::N_BINS);
		for (int offset = 0; offset < numSamples; offset += MAX_CQT_SAMPLES_PER_PUSH) {
			const int chunk = std::min(MAX_CQT_SAMPLES_PER_PUSH, numSamples - offset);
			const int cqtProduced = cqtExtractor_->push(
				samples + offset, chunk, cqtScratch_.data(), MAX_CQT_FRAMES_PER_PUSH
			);

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
		}

		// Run a fast provisional inference first, then refresh on a slower cadence
		// so key CNN work does not interrupt live beat visuals.
		const bool hasMinFrames = cqtFrameCount_ >= KEY_MIN_FRAMES;
		const bool shouldRunInference = hasMinFrames &&
			(keyInferenceCount_ == 0 || cqtFramesSinceInference_ >= KEY_INFERENCE_INTERVAL);

		if (shouldRunInference) {
			runKeyInferences();
		}
	}

	// BPM Detection Pipeline (resample 44100 -> 22050 Hz)
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
