/**
 * Error handling and edge case tests
 *
 * Tests behavior with invalid inputs, edge cases, and error conditions.
 */

#include "catch_amalgamated.hpp"
#include "Engine.hpp"
#include "MelExtractor.hpp"
#include "CqtExtractor.hpp"
#include "Resampler.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

// ============================================================================
// Model Loading Error Handling
// ============================================================================

#ifdef ONNX_ENABLED

TEST_CASE("OnnxModel handles invalid path", "[error][onnx]") {
	OnnxModel model;

	bool loaded = model.load("/nonexistent/path/to/model.onnx");

	REQUIRE_FALSE(loaded);
	REQUIRE_FALSE(model.isReady());
}

TEST_CASE("KeyModel handles invalid path", "[error][key]") {
	KeyModel model;

	bool loaded = model.load("/nonexistent/path/to/keymodel.onnx");

	REQUIRE_FALSE(loaded);
	REQUIRE_FALSE(model.isReady());
}

TEST_CASE("Engine handles invalid model paths", "[error][engine]") {
	Engine engine;

	SECTION("invalid BPM model path") {
		bool loaded = engine.loadModel("/nonexistent/beatnet.onnx");
		REQUIRE_FALSE(loaded);
		REQUIRE_FALSE(engine.isReady());
	}

	SECTION("invalid key model path") {
		bool loaded = engine.loadKeyModel("/nonexistent/keynet.onnx");
		REQUIRE_FALSE(loaded);
		REQUIRE_FALSE(engine.isKeyReady());
	}
}

TEST_CASE("Engine processes audio without loaded model", "[error][engine]") {
	Engine engine;

	// Don't load model
	REQUIRE_FALSE(engine.isReady());

	// Try to process audio
	auto audio = test_utils::generateSineWave(440.0f, 44100.0f, 44100);
	std::vector<Engine::FrameResult> results(100);

	int numResults = engine.processAudio(audio.data(), audio.size(),
	                                      results.data(), results.size());

	// Should return 0 results (no model loaded)
	REQUIRE(numResults == 0);
}

#endif // ONNX_ENABLED

// ============================================================================
// Edge Cases - Empty and Zero Input
// ============================================================================

TEST_CASE("MelExtractor handles empty input", "[error][mel]") {
	StreamingMelExtractor extractor;

	std::vector<float> features(272 * 10);

	// Zero-length input
	int frames = extractor.push(nullptr, 0, features.data(), 10);

	REQUIRE(frames == 0);
}

TEST_CASE("CqtExtractor handles empty input", "[error][cqt]") {
	StreamingCqtExtractor extractor;

	std::vector<float> cqtFrames(CqtConfig::N_BINS * 10);

	// Zero-length input
	int frames = extractor.push(nullptr, 0, cqtFrames.data(), 10);

	REQUIRE(frames == 0);
}

TEST_CASE("Resampler handles empty input", "[error][resampler]") {
	Resampler resampler;

	std::vector<float> output(100);

	int outputSize = resampler.process(nullptr, 0, output.data());

	REQUIRE(outputSize == 0);
}

// ============================================================================
// Edge Cases - Very Short Input
// ============================================================================

TEST_CASE("MelExtractor handles very short input", "[error][mel]") {
	StreamingMelExtractor extractor;

	// Input shorter than window length
	std::vector<float> shortAudio(100, 0.1f);
	std::vector<float> features(272 * 10);

	int frames = extractor.push(shortAudio.data(), shortAudio.size(),
	                            features.data(), 10);

	// Should return 0 frames (not enough data for a window)
	REQUIRE(frames == 0);
}

TEST_CASE("CqtExtractor handles very short input", "[error][cqt]") {
	StreamingCqtExtractor extractor;

	// Input shorter than minimum filter length
	std::vector<float> shortAudio(100, 0.1f);
	std::vector<float> cqtFrames(CqtConfig::N_BINS * 10);

	int frames = extractor.push(shortAudio.data(), shortAudio.size(),
	                            cqtFrames.data(), 10);

	// Should return 0 frames (not enough data)
	REQUIRE(frames == 0);
}

// ============================================================================
// Edge Cases - Silence
// ============================================================================

TEST_CASE("MelExtractor handles silence", "[error][mel]") {
	StreamingMelExtractor extractor;

	// Generate silent audio
	std::vector<float> silence(22050, 0.0f);  // 1 second of silence
	std::vector<float> features(272 * 100);

	int frames = extractor.push(silence.data(), silence.size(),
	                            features.data(), 100);

	REQUIRE(frames > 0);

	// Features should be finite (log(1+0) = 0, not -inf)
	for (int f = 0; f < frames; f++) {
		for (int i = 0; i < 272; i++) {
			float val = features[f * 272 + i];
			REQUIRE(std::isfinite(val));
		}
	}
}

TEST_CASE("CqtExtractor handles silence", "[error][cqt]") {
	CqtExtractor extractor;

	// Generate silent audio
	int maxFilterLen = extractor.getMaxFilterLength();
	std::vector<float> silence(maxFilterLen, 0.0f);
	std::vector<float> cqtBins(CqtConfig::N_BINS);

	bool success = extractor.processFrame(silence.data(), maxFilterLen, cqtBins.data());

	REQUIRE(success);

	// All bins should be finite (log1p(0) = 0)
	for (int i = 0; i < CqtConfig::N_BINS; i++) {
		REQUIRE(std::isfinite(cqtBins[i]));
		REQUIRE(cqtBins[i] >= 0.0f);
	}
}

// ============================================================================
// Edge Cases - DC Offset
// ============================================================================

TEST_CASE("MelExtractor handles DC offset", "[error][mel]") {
	StreamingMelExtractor extractor;

	// Audio with DC offset
	std::vector<float> dcAudio(22050);
	for (size_t i = 0; i < dcAudio.size(); i++) {
		dcAudio[i] = 0.5f;  // Constant DC
	}

	std::vector<float> features(272 * 100);

	int frames = extractor.push(dcAudio.data(), dcAudio.size(),
	                            features.data(), 100);

	REQUIRE(frames > 0);

	// Features should be finite
	for (int f = 0; f < frames; f++) {
		for (int i = 0; i < 272; i++) {
			REQUIRE(std::isfinite(features[f * 272 + i]));
		}
	}
}

// ============================================================================
// Edge Cases - Extreme Values
// ============================================================================

TEST_CASE("MelExtractor handles clipping", "[error][mel]") {
	StreamingMelExtractor extractor;

	// Clipped audio (values at +/- 1.0)
	std::vector<float> clippedAudio(22050);
	for (size_t i = 0; i < clippedAudio.size(); i++) {
		clippedAudio[i] = (i % 2 == 0) ? 1.0f : -1.0f;
	}

	std::vector<float> features(272 * 100);

	int frames = extractor.push(clippedAudio.data(), clippedAudio.size(),
	                            features.data(), 100);

	REQUIRE(frames > 0);

	// Features should be finite (no overflow)
	for (int f = 0; f < frames; f++) {
		for (int i = 0; i < 272; i++) {
			REQUIRE(std::isfinite(features[f * 272 + i]));
		}
	}
}

TEST_CASE("Resampler handles extreme values", "[error][resampler]") {
	Resampler resampler;

	// Very loud audio
	std::vector<float> loud(4410);
	for (size_t i = 0; i < loud.size(); i++) {
		loud[i] = 10.0f * std::sin(2.0f * M_PI * 440.0f * i / 44100.0f);
	}

	std::vector<float> output(resampler.getOutputSize(loud.size()));
	int outputSize = resampler.process(loud.data(), loud.size(), output.data());

	REQUIRE(outputSize > 0);

	// Output should be finite
	for (int i = 0; i < outputSize; i++) {
		REQUIRE(std::isfinite(output[i]));
	}
}

// ============================================================================
// Repeated Operations
// ============================================================================

TEST_CASE("MelExtractor handles repeated reset", "[error][mel]") {
	StreamingMelExtractor extractor;

	auto audio = test_utils::generateSineWave(440.0f, 22050.0f, 22050);
	std::vector<float> features(272 * 100);

	// Process, reset, process cycle
	for (int cycle = 0; cycle < 5; cycle++) {
		int frames = extractor.push(audio.data(), audio.size(),
		                            features.data(), 100);
		REQUIRE(frames > 0);

		extractor.reset();
	}
}

TEST_CASE("Resampler handles repeated reset", "[error][resampler]") {
	Resampler resampler;

	auto audio = test_utils::generateSineWave(440.0f, 44100.0f, 44100);
	std::vector<float> output(resampler.getOutputSize(audio.size()));

	// Process, reset, process cycle
	for (int cycle = 0; cycle < 5; cycle++) {
		int outputSize = resampler.process(audio.data(), audio.size(), output.data());
		REQUIRE(outputSize > 0);

		resampler.reset();
	}
}

#ifdef ONNX_ENABLED

TEST_CASE("Engine handles repeated reset", "[error][engine]") {
	Engine engine;

	std::string modelPath = test_utils::getModelPath();
	if (!engine.loadModel(modelPath)) {
		SKIP("Model file not available");
	}

	auto audio = test_utils::generateClickTrack(120.0f, 44100.0f, 2.0f);
	std::vector<Engine::FrameResult> results(200);

	// Process, reset, process cycle
	for (int cycle = 0; cycle < 3; cycle++) {
		engine.processAudio(audio.data(), audio.size(), results.data(), results.size());
		REQUIRE(engine.getFrameCount() > 0);

		engine.reset();
		REQUIRE(engine.getFrameCount() == 0);
	}
}

#endif // ONNX_ENABLED
