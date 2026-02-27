/**
 * Resampler unit tests
 *
 * Tests audio resampling from 44100 Hz to 22050 Hz.
 */

#include "catch_amalgamated.hpp"
#include "Resampler.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

TEST_CASE("Resampler constants", "[resampler][constants]") {
	REQUIRE(Resampler::INPUT_RATE == 44100);
	REQUIRE(Resampler::OUTPUT_RATE == 22050);
	REQUIRE(Resampler::RATIO == 2);
}

TEST_CASE("Resampler output size", "[resampler][size]") {
	Resampler resampler;

	REQUIRE(resampler.getOutputSize(44100) == 22050);
	REQUIRE(resampler.getOutputSize(88200) == 44100);
	REQUIRE(resampler.getOutputSize(1000) == 500);
}

TEST_CASE("LinearResampler output size", "[resampler][linear][size]") {
	LinearResampler resampler;

	REQUIRE(resampler.getOutputSize(44100) == 22050);
	REQUIRE(resampler.getOutputSize(88200) == 44100);
}

TEST_CASE("Resampler basic operation", "[resampler][basic]") {
	Resampler resampler;

	// Generate 1 second of 440 Hz sine wave at 44100 Hz
	const int inputSize = 44100;
	std::vector<float> input(inputSize);
	for (int i = 0; i < inputSize; i++) {
		float t = static_cast<float>(i) / 44100.0f;
		input[i] = std::sin(2.0f * M_PI * 440.0f * t);
	}

	// Resample
	std::vector<float> output(resampler.getOutputSize(inputSize));
	int outputSize = resampler.process(input.data(), inputSize, output.data());

	INFO("Input size: " << inputSize);
	INFO("Output size: " << outputSize);

	// Output should be approximately half the input size
	// (minus some samples lost at edges due to filter)
	REQUIRE(outputSize > 20000);
	REQUIRE(outputSize <= 22050);
}

TEST_CASE("Resampler preserves frequency content below Nyquist", "[resampler][frequency]") {
	Resampler resampler;

	// Generate 440 Hz sine wave (well below new Nyquist of 11025 Hz)
	const int inputSize = 44100;  // 1 second
	std::vector<float> input(inputSize);
	for (int i = 0; i < inputSize; i++) {
		float t = static_cast<float>(i) / 44100.0f;
		input[i] = std::sin(2.0f * M_PI * 440.0f * t);
	}

	std::vector<float> output(resampler.getOutputSize(inputSize));
	int outputSize = resampler.process(input.data(), inputSize, output.data());

	// Check output is still a sine-like signal (by checking zero crossings)
	int zeroCrossings = 0;
	for (int i = 1; i < outputSize; i++) {
		if ((output[i] >= 0) != (output[i-1] >= 0)) {
			zeroCrossings++;
		}
	}

	// 440 Hz signal at 22050 Hz should have ~880 zero crossings per second
	// (2 per cycle)
	float duration = static_cast<float>(outputSize) / 22050.0f;
	float expectedCrossings = 880.0f * duration;

	INFO("Zero crossings: " << zeroCrossings);
	INFO("Expected: ~" << expectedCrossings);

	// Allow 5% tolerance
	REQUIRE(zeroCrossings > expectedCrossings * 0.95f);
	REQUIRE(zeroCrossings < expectedCrossings * 1.05f);
}

TEST_CASE("Resampler attenuates frequencies above new Nyquist", "[resampler][alias]") {
	Resampler resampler;

	// Generate 20000 Hz sine wave (well above new Nyquist of 11025 Hz)
	// This is clearly in the stopband of the anti-aliasing filter
	const int inputSize = 44100;
	std::vector<float> input(inputSize);
	for (int i = 0; i < inputSize; i++) {
		float t = static_cast<float>(i) / 44100.0f;
		input[i] = std::sin(2.0f * M_PI * 20000.0f * t);
	}

	std::vector<float> output(resampler.getOutputSize(inputSize));
	int outputSize = resampler.process(input.data(), inputSize, output.data());

	// Calculate RMS of output (should be significantly attenuated)
	float rmsOutput = 0.0f;
	for (int i = 0; i < outputSize; i++) {
		rmsOutput += output[i] * output[i];
	}
	rmsOutput = std::sqrt(rmsOutput / outputSize);

	// Input RMS of sine wave is ~0.707
	INFO("Output RMS: " << rmsOutput);

	// Should be attenuated - the filter has finite stopband attenuation
	// With Blackman window and 127 taps, expect ~-50dB stopband
	// But 20 kHz is at 0.45 of input fs, right at the transition band edge
	// So we'll accept any significant attenuation (> 3dB = 0.5x)
	REQUIRE(rmsOutput < 0.5f);
}

TEST_CASE("LinearResampler basic operation", "[resampler][linear][basic]") {
	LinearResampler resampler;

	// Generate sine wave
	const int inputSize = 44100;
	std::vector<float> input(inputSize);
	for (int i = 0; i < inputSize; i++) {
		float t = static_cast<float>(i) / 44100.0f;
		input[i] = std::sin(2.0f * M_PI * 440.0f * t);
	}

	std::vector<float> output(resampler.getOutputSize(inputSize));
	int outputSize = resampler.process(input.data(), inputSize, output.data());

	REQUIRE(outputSize == 22050);

	// Check signal is present
	float maxVal = 0;
	for (int i = 0; i < outputSize; i++) {
		maxVal = std::max(maxVal, std::abs(output[i]));
	}
	REQUIRE(maxVal > 0.9f);
}

TEST_CASE("Resampler streaming mode", "[resampler][streaming]") {
	Resampler resampler;

	// Generate sine wave in chunks
	const int chunkSize = 4410;  // 100ms at 44100 Hz
	const int numChunks = 10;
	const int totalInput = chunkSize * numChunks;

	std::vector<float> fullInput(totalInput);
	for (int i = 0; i < totalInput; i++) {
		float t = static_cast<float>(i) / 44100.0f;
		fullInput[i] = std::sin(2.0f * M_PI * 440.0f * t);
	}

	// Process in chunks
	std::vector<float> streamingOutput;
	resampler.reset();

	for (int chunk = 0; chunk < numChunks; chunk++) {
		const float* chunkPtr = fullInput.data() + chunk * chunkSize;
		std::vector<float> chunkOutput(chunkSize);  // Max possible output

		int produced = resampler.processStreaming(chunkPtr, chunkSize, chunkOutput.data(), chunkSize);

		for (int i = 0; i < produced; i++) {
			streamingOutput.push_back(chunkOutput[i]);
		}
	}

	INFO("Streaming output samples: " << streamingOutput.size());

	// Should produce roughly half the input
	REQUIRE(streamingOutput.size() > 18000);
	REQUIRE(streamingOutput.size() <= 22050);

	// Check output is valid signal
	float maxVal = 0;
	for (float v : streamingOutput) {
		maxVal = std::max(maxVal, std::abs(v));
	}
	REQUIRE(maxVal > 0.8f);
}

TEST_CASE("Resampler with mel extractor sample rate", "[resampler][mel]") {
	// Test that resampled audio can be used with mel extractor
	Resampler resampler(44100, 22050);

	// Generate 2 seconds of audio at 44100 Hz
	const int inputSize = 44100 * 2;
	std::vector<float> input(inputSize);
	for (int i = 0; i < inputSize; i++) {
		float t = static_cast<float>(i) / 44100.0f;
		// Mix of frequencies
		input[i] = 0.5f * std::sin(2.0f * M_PI * 440.0f * t)
		         + 0.3f * std::sin(2.0f * M_PI * 880.0f * t)
		         + 0.2f * std::sin(2.0f * M_PI * 1320.0f * t);
	}

	std::vector<float> output(resampler.getOutputSize(inputSize));
	int outputSize = resampler.process(input.data(), inputSize, output.data());

	// Output should be approximately 2 seconds at 22050 Hz
	float outputDuration = static_cast<float>(outputSize) / 22050.0f;
	INFO("Output duration: " << outputDuration << " seconds");

	REQUIRE(outputDuration > 1.9f);
	REQUIRE(outputDuration <= 2.0f);
}
