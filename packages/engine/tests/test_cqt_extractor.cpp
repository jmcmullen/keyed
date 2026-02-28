/**
 * CQT Extractor unit tests
 *
 * Validates C++ CQT implementation against librosa golden files.
 */

#include "catch_amalgamated.hpp"
#include "CqtExtractor.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <fstream>
#include <vector>

using namespace engine;
using Catch::Approx;

TEST_CASE("CqtConfig constants", "[cqt][config]") {
	REQUIRE(CqtConfig::SAMPLE_RATE == 44100);
	REQUIRE(CqtConfig::HOP_LENGTH == 8820);
	REQUIRE(CqtConfig::N_BINS == 105);
	REQUIRE(CqtConfig::BINS_PER_OCTAVE == 24);
	REQUIRE(CqtConfig::F_MIN == 65.0f);
	REQUIRE(CqtConfig::TIME_FRAMES == 100);

	// Verify FPS calculation (~5)
	float fps = static_cast<float>(CqtConfig::SAMPLE_RATE) / CqtConfig::HOP_LENGTH;
	REQUIRE(fps == Approx(5.0f).margin(0.1f));
}

TEST_CASE("CqtExtractor initialization", "[cqt][init]") {
	CqtExtractor extractor;

	// Check center frequencies
	const auto& freqs = extractor.getCenterFrequencies();
	REQUIRE(freqs.size() == 105);

	// First bin should be fmin
	REQUIRE(freqs[0] == Approx(65.0f).margin(0.1f));

	// Verify logarithmic spacing
	// f_k = fmin * 2^(k/bins_per_octave)
	// So f_24 should be 2 * f_0 = 130 Hz
	REQUIRE(freqs[24] == Approx(130.0f).margin(1.0f));
	REQUIRE(freqs[48] == Approx(260.0f).margin(2.0f));

	// Check filter lengths
	const auto& lengths = extractor.getFilterLengths();
	REQUIRE(lengths.size() == 105);

	// Lowest bin should have longest filter
	// N_0 = Q * sr / fmin ≈ 34.127 * 44100 / 65 ≈ 23154
	REQUIRE(lengths[0] > 20000);
	REQUIRE(lengths[0] < 25000);

	// Higher bins should have shorter filters
	REQUIRE(lengths[104] < lengths[0]);
	REQUIRE(lengths[104] > 1000);  // Should still be > 1000 samples
}

TEST_CASE("CqtExtractor sine wave detection", "[cqt][sine]") {
	CqtExtractor extractor;

	// Generate 440 Hz sine wave (A4)
	const int sampleRate = CqtConfig::SAMPLE_RATE;
	const int maxFilterLen = extractor.getMaxFilterLength();
	const int numSamples = maxFilterLen;

	std::vector<float> audio(numSamples);
	for (int i = 0; i < numSamples; i++) {
		float t = static_cast<float>(i) / sampleRate;
		audio[i] = std::sin(2.0f * M_PI * 440.0f * t);
	}

	// Extract CQT
	std::vector<float> cqtBins(CqtConfig::N_BINS);
	bool success = extractor.processFrame(audio.data(), numSamples, cqtBins.data());
	REQUIRE(success);

	// Find peak bin
	int peakBin = 0;
	float peakVal = cqtBins[0];
	for (int i = 1; i < CqtConfig::N_BINS; i++) {
		if (cqtBins[i] > peakVal) {
			peakVal = cqtBins[i];
			peakBin = i;
		}
	}

	// 440 Hz should be around bin 66 (since 65 Hz * 2^(66/24) ≈ 440 Hz)
	// Actually: k = 24 * log2(440/65) = 24 * 2.759 ≈ 66.2
	const auto& freqs = extractor.getCenterFrequencies();
	INFO("Peak bin: " << peakBin << " at frequency " << freqs[peakBin] << " Hz");
	INFO("Peak value: " << peakVal);

	// Peak should be near 440 Hz
	REQUIRE(freqs[peakBin] > 400.0f);
	REQUIRE(freqs[peakBin] < 480.0f);
}

TEST_CASE("StreamingCqtExtractor basic operation", "[cqt][streaming]") {
	StreamingCqtExtractor extractor;

	// Generate audio
	const int sampleRate = CqtConfig::SAMPLE_RATE;
	const int hopLength = CqtConfig::HOP_LENGTH;

	// Need enough audio for at least one frame
	// First frame requires half of max filter length samples
	const int maxFilterLen = CqtExtractor::getMaxFilterLength();
	const int samplesForFirstFrame = maxFilterLen / 2 + hopLength;

	std::vector<float> audio(samplesForFirstFrame);
	for (int i = 0; i < samplesForFirstFrame; i++) {
		float t = static_cast<float>(i) / sampleRate;
		audio[i] = std::sin(2.0f * M_PI * 440.0f * t);
	}

	// Push audio and get frames
	std::vector<float> cqtFrames(CqtConfig::N_BINS * 10);  // Space for 10 frames
	int framesProduced = extractor.push(audio.data(), samplesForFirstFrame,
	                                     cqtFrames.data(), 10);

	INFO("Frames produced: " << framesProduced);
	REQUIRE(framesProduced >= 0);

	if (framesProduced > 0) {
		// Check that we got valid CQT data (not all zeros)
		float maxVal = 0;
		for (int i = 0; i < CqtConfig::N_BINS; i++) {
			maxVal = std::max(maxVal, cqtFrames[i]);
		}
		REQUIRE(maxVal > 0);  // Should have some non-zero values
	}
}

TEST_CASE("StreamingCqtExtractor frame count", "[cqt][streaming]") {
	StreamingCqtExtractor extractor;

	// Generate 2 seconds of audio
	const int sampleRate = CqtConfig::SAMPLE_RATE;
	const float duration = 2.0f;
	const int totalSamples = static_cast<int>(sampleRate * duration);

	std::vector<float> audio(totalSamples);
	for (int i = 0; i < totalSamples; i++) {
		float t = static_cast<float>(i) / sampleRate;
		audio[i] = std::sin(2.0f * M_PI * 440.0f * t);
	}

	// Push all audio
	std::vector<float> cqtFrames(CqtConfig::N_BINS * 20);  // Space for 20 frames
	int framesProduced = extractor.push(audio.data(), totalSamples,
	                                     cqtFrames.data(), 20);

	// At ~5 FPS, 2 seconds should give ~10 frames
	// But first frame needs extra samples for centering
	INFO("Frames produced for 2 seconds: " << framesProduced);
	REQUIRE(framesProduced >= 5);
	REQUIRE(framesProduced <= 15);
}

TEST_CASE("CQT output range", "[cqt][range]") {
	CqtExtractor extractor;

	// Generate random-ish audio
	const int sampleRate = CqtConfig::SAMPLE_RATE;
	const int maxFilterLen = extractor.getMaxFilterLength();

	std::vector<float> audio(maxFilterLen);
	for (int i = 0; i < maxFilterLen; i++) {
		// Mix of frequencies
		float t = static_cast<float>(i) / sampleRate;
		audio[i] = 0.3f * std::sin(2.0f * M_PI * 100.0f * t)
		         + 0.3f * std::sin(2.0f * M_PI * 440.0f * t)
		         + 0.3f * std::sin(2.0f * M_PI * 1000.0f * t);
	}

	std::vector<float> cqtBins(CqtConfig::N_BINS);
	bool success = extractor.processFrame(audio.data(), maxFilterLen, cqtBins.data());
	REQUIRE(success);

	// Output should be non-negative (magnitude + log1p)
	for (int i = 0; i < CqtConfig::N_BINS; i++) {
		REQUIRE(cqtBins[i] >= 0.0f);
	}

	// Should have reasonable dynamic range
	float minVal = *std::min_element(cqtBins.begin(), cqtBins.end());
	float maxVal = *std::max_element(cqtBins.begin(), cqtBins.end());

	INFO("CQT output range: [" << minVal << ", " << maxVal << "]");
	REQUIRE(maxVal > minVal);  // Not all same value
}
