/**
 * AutocorrBpm unit tests
 *
 * Tests the autocorrelation-based BPM estimation algorithm.
 */

#include "catch_amalgamated.hpp"
#include "AutocorrBpm.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

TEST_CASE("AutocorrBpmEstimator constants", "[bpm][constants]") {
	REQUIRE(AutocorrBpmEstimator::FPS == 50.0f);
	REQUIRE(AutocorrBpmEstimator::MIN_BPM == 60.0f);
	REQUIRE(AutocorrBpmEstimator::MAX_BPM == 180.0f);
	REQUIRE(AutocorrBpmEstimator::DJ_MIN_BPM == 75.0f);
	REQUIRE(AutocorrBpmEstimator::DJ_MAX_BPM == 165.0f);
}

TEST_CASE("ActivationBuffer constants", "[bpm][buffer][constants]") {
	REQUIRE(ActivationBuffer::DEFAULT_MAX_FRAMES == 512);
	REQUIRE(ActivationBuffer::MIN_FRAMES_FOR_BPM == 100);
}

/**
 * Generate synthetic beat activations at a specific BPM
 * Creates impulse-like activations at beat positions
 */
std::pair<std::vector<float>, std::vector<float>> generateBeatPattern(
	float bpm, size_t numFrames, float fps = 50.0f) {

	std::vector<float> beatActivations(numFrames, 0.0f);
	std::vector<float> downbeatActivations(numFrames, 0.0f);

	float framesPerBeat = fps * 60.0f / bpm;

	for (size_t i = 0; i < numFrames; i++) {
		float beatPhase = std::fmod(static_cast<float>(i), framesPerBeat);

		// Create smooth activation peak at beat position
		if (beatPhase < framesPerBeat * 0.1f) {
			float t = beatPhase / (framesPerBeat * 0.1f);
			beatActivations[i] = 1.0f - t;  // Decay from 1.0
		}

		// Downbeat every 4 beats
		float downbeatPhase = std::fmod(static_cast<float>(i), framesPerBeat * 4.0f);
		if (downbeatPhase < framesPerBeat * 0.1f) {
			float t = downbeatPhase / (framesPerBeat * 0.1f);
			downbeatActivations[i] = 1.0f - t;
		}
	}

	return {beatActivations, downbeatActivations};
}

TEST_CASE("AutocorrBpmEstimator insufficient data", "[bpm][edge]") {
	std::vector<float> beats(10, 0.5f);
	std::vector<float> downbeats(10, 0.2f);

	// Less than 1 second of data (50 frames) should return 0
	float bpm = AutocorrBpmEstimator::estimate(
		beats.data(), downbeats.data(), beats.size());

	REQUIRE(bpm == 0.0f);
}

TEST_CASE("AutocorrBpmEstimator detects 120 BPM", "[bpm][accuracy]") {
	const float targetBpm = 120.0f;
	const size_t numFrames = 300;  // 6 seconds

	auto [beats, downbeats] = generateBeatPattern(targetBpm, numFrames);

	float detectedBpm = AutocorrBpmEstimator::estimate(
		beats.data(), downbeats.data(), numFrames, false);

	INFO("Target BPM: " << targetBpm);
	INFO("Detected BPM: " << detectedBpm);

	// Should be within 2 BPM of target
	REQUIRE(std::abs(detectedBpm - targetBpm) <= 2.0f);
}

TEST_CASE("AutocorrBpmEstimator detects various tempos", "[bpm][accuracy]") {
	const size_t numFrames = 400;  // 8 seconds

	// Test tempos in DJ range (octave correction won't change them)
	std::vector<float> testBpms = {80.0f, 100.0f, 120.0f, 128.0f, 140.0f};

	for (float targetBpm : testBpms) {
		DYNAMIC_SECTION("detects " << targetBpm << " BPM") {
			auto [beats, downbeats] = generateBeatPattern(targetBpm, numFrames);

			// Test WITH octave correction (more realistic)
			float detectedBpm = AutocorrBpmEstimator::estimate(
				beats.data(), downbeats.data(), numFrames, true);

			INFO("Target BPM: " << targetBpm);
			INFO("Detected BPM: " << detectedBpm);

			// Should be within 2 BPM of target (with octave correction)
			REQUIRE(std::abs(detectedBpm - targetBpm) <= 2.0f);
		}
	}
}

TEST_CASE("AutocorrBpmEstimator octave correction", "[bpm][octave]") {
	const size_t numFrames = 400;

	SECTION("doubles 60 BPM to DJ range") {
		auto [beats, downbeats] = generateBeatPattern(60.0f, numFrames);

		float withCorrection = AutocorrBpmEstimator::estimate(
			beats.data(), downbeats.data(), numFrames, true);

		float withoutCorrection = AutocorrBpmEstimator::estimate(
			beats.data(), downbeats.data(), numFrames, false);

		INFO("With correction: " << withCorrection);
		INFO("Without correction: " << withoutCorrection);

		// With correction should double to ~120 BPM (in DJ range)
		REQUIRE(withCorrection == Approx(withoutCorrection * 2.0f).margin(2.0f));
	}

	SECTION("halves 180 BPM to DJ range") {
		auto [beats, downbeats] = generateBeatPattern(180.0f, numFrames);

		float withCorrection = AutocorrBpmEstimator::estimate(
			beats.data(), downbeats.data(), numFrames, true);

		float withoutCorrection = AutocorrBpmEstimator::estimate(
			beats.data(), downbeats.data(), numFrames, false);

		INFO("With correction: " << withCorrection);
		INFO("Without correction: " << withoutCorrection);

		// With correction should halve to ~90 BPM (in DJ range)
		REQUIRE(withCorrection == Approx(withoutCorrection / 2.0f).margin(2.0f));
	}

	SECTION("120 BPM unchanged (already in range)") {
		auto [beats, downbeats] = generateBeatPattern(120.0f, numFrames);

		float withCorrection = AutocorrBpmEstimator::estimate(
			beats.data(), downbeats.data(), numFrames, true);

		float withoutCorrection = AutocorrBpmEstimator::estimate(
			beats.data(), downbeats.data(), numFrames, false);

		// Should be the same
		REQUIRE(withCorrection == Approx(withoutCorrection).margin(1.0f));
	}
}

TEST_CASE("ActivationBuffer basic operations", "[bpm][buffer]") {
	ActivationBuffer buffer;

	SECTION("starts empty") {
		REQUIRE(buffer.size() == 0);
		REQUIRE(buffer.getCachedBpm() == 0.0f);
	}

	SECTION("push increases size") {
		buffer.push(0.5f, 0.2f);
		REQUIRE(buffer.size() == 1);

		buffer.push(0.6f, 0.3f);
		REQUIRE(buffer.size() == 2);
	}

	SECTION("clear resets state") {
		for (int i = 0; i < 10; i++) {
			buffer.push(0.5f, 0.2f);
		}
		REQUIRE(buffer.size() == 10);

		buffer.clear();
		REQUIRE(buffer.size() == 0);
		REQUIRE(buffer.getCachedBpm() == 0.0f);
	}
}

TEST_CASE("ActivationBuffer respects max size", "[bpm][buffer]") {
	ActivationBuffer buffer(100);  // Small buffer for testing

	// Fill beyond capacity
	for (int i = 0; i < 150; i++) {
		buffer.push(0.5f, 0.2f);
	}

	// Should not exceed max size
	REQUIRE(buffer.size() == 100);
}

TEST_CASE("ActivationBuffer estimates BPM", "[bpm][buffer][accuracy]") {
	ActivationBuffer buffer;

	const float targetBpm = 120.0f;
	const size_t numFrames = 200;

	auto [beats, downbeats] = generateBeatPattern(targetBpm, numFrames);

	for (size_t i = 0; i < numFrames; i++) {
		buffer.push(beats[i], downbeats[i]);
	}

	float bpm = buffer.estimateBpm();

	INFO("Target BPM: " << targetBpm);
	INFO("Estimated BPM: " << bpm);

	REQUIRE(std::abs(bpm - targetBpm) <= 2.0f);
}

TEST_CASE("ActivationBuffer auto-computes BPM", "[bpm][buffer][auto]") {
	ActivationBuffer buffer;

	const float targetBpm = 120.0f;
	auto [beats, downbeats] = generateBeatPattern(targetBpm, 200);

	// Push frames one at a time
	for (size_t i = 0; i < 200; i++) {
		buffer.push(beats[i], downbeats[i]);
	}

	// After enough frames, cached BPM should be computed automatically
	float cachedBpm = buffer.getCachedBpm();

	INFO("Cached BPM: " << cachedBpm);

	// Should have a valid BPM cached
	REQUIRE(cachedBpm > 0.0f);
	REQUIRE(std::abs(cachedBpm - targetBpm) <= 2.0f);
}

TEST_CASE("ActivationBuffer ring buffer ordering", "[bpm][buffer][ring]") {
	// Use buffer that's larger than MIN_FRAMES_FOR_BPM (100)
	ActivationBuffer buffer(150);

	// Generate 120 BPM pattern with enough frames to wrap around
	auto [beats, downbeats] = generateBeatPattern(120.0f, 300);

	// Push all frames (will wrap around twice)
	for (size_t i = 0; i < 300; i++) {
		buffer.push(beats[i], downbeats[i]);
	}

	REQUIRE(buffer.size() == 150);

	// Should still detect correct BPM from most recent frames
	float bpm = buffer.estimateBpm();
	INFO("BPM after wrap-around: " << bpm);

	// Should detect ~120 BPM
	REQUIRE(bpm > 100.0f);
	REQUIRE(bpm < 140.0f);
}
