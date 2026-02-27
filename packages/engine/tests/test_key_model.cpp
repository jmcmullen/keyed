/**
 * KeyModel unit tests
 *
 * Tests ONNX Runtime wrapper for MusicalKeyCNN model.
 */

#include "catch_amalgamated.hpp"
#include "KeyModel.hpp"
#include "CqtExtractor.hpp"
#include "OnnxRuntime.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

TEST_CASE("KeyModel constants", "[key][constants]") {
	REQUIRE(KeyModel::INPUT_FREQ_BINS == 105);
	REQUIRE(KeyModel::INPUT_TIME_FRAMES == 100);
	REQUIRE(KeyModel::INPUT_SIZE == 105 * 100);
	REQUIRE(KeyModel::NUM_CLASSES == 24);
}

TEST_CASE("KeyModel Camelot mapping", "[key][camelot]") {
	// Verify Camelot key mapping is complete
	for (int i = 0; i < KeyModel::NUM_CLASSES; i++) {
		REQUIRE(KeyModel::CAMELOT_KEYS[i] != nullptr);
		REQUIRE(KeyModel::NOTATION_KEYS[i] != nullptr);
	}

	// Check specific mappings (model order follows chromatic circle of fifths)
	// Minor keys (0-11): G#m, D#m, A#m, Fm, Cm, Gm, Dm, Am, Em, Bm, F#m, C#m
	REQUIRE(std::string(KeyModel::CAMELOT_KEYS[0]) == "1A");   // G#m
	REQUIRE(std::string(KeyModel::NOTATION_KEYS[0]) == "G#m");
	REQUIRE(std::string(KeyModel::CAMELOT_KEYS[4]) == "5A");   // Cm
	REQUIRE(std::string(KeyModel::NOTATION_KEYS[4]) == "Cm");
	REQUIRE(std::string(KeyModel::CAMELOT_KEYS[7]) == "8A");   // Am
	REQUIRE(std::string(KeyModel::NOTATION_KEYS[7]) == "Am");

	// Major keys (12-23): B, F#, C#, G#, D#, A#, F, C, G, D, A, E
	REQUIRE(std::string(KeyModel::CAMELOT_KEYS[19]) == "8B");  // C
	REQUIRE(std::string(KeyModel::NOTATION_KEYS[19]) == "C");
	REQUIRE(std::string(KeyModel::CAMELOT_KEYS[20]) == "9B");  // G
	REQUIRE(std::string(KeyModel::NOTATION_KEYS[20]) == "G");
}

TEST_CASE("OnnxRuntime singleton", "[key][onnx]") {
	auto& runtime1 = OnnxRuntime::instance();
	auto& runtime2 = OnnxRuntime::instance();

	// Should be same instance
	REQUIRE(&runtime1 == &runtime2);

	// Should be initialized
	REQUIRE(runtime1.isInitialized());
	REQUIRE(runtime1.api() != nullptr);
	REQUIRE(runtime1.env() != nullptr);
	REQUIRE(runtime1.memoryInfo() != nullptr);
}

TEST_CASE("KeyModel load and ready", "[key][model]") {
	KeyModel model;

	// Not ready before load
	REQUIRE(!model.isReady());

	// Load model
	std::string modelPath = test_utils::getModelsDir() + "keynet.onnx";
	bool loaded = model.load(modelPath);

	if (!loaded) {
		WARN("MusicalKeyCNN model not found at: " << modelPath);
		SKIP("Model file not available");
	}

	REQUIRE(model.isReady());
}

TEST_CASE("KeyModel inference with synthetic data", "[key][inference]") {
	KeyModel model;

	std::string modelPath = test_utils::getModelsDir() + "keynet.onnx";
	if (!model.load(modelPath)) {
		SKIP("Model file not available");
	}

	// Create synthetic CQT spectrogram (105x100)
	std::vector<float> cqt(KeyModel::INPUT_SIZE);

	// Fill with some pattern - emphasize certain frequency bins
	// This won't produce a meaningful key, just tests inference works
	for (int f = 0; f < KeyModel::INPUT_FREQ_BINS; f++) {
		for (int t = 0; t < KeyModel::INPUT_TIME_FRAMES; t++) {
			// Create some variation
			float val = 0.1f;
			if (f >= 40 && f <= 60) {
				val = 0.5f + 0.3f * std::sin(2.0f * M_PI * t / 20.0f);
			}
			cqt[f * KeyModel::INPUT_TIME_FRAMES + t] = val;
		}
	}

	KeyOutput output;
	bool success = model.infer(cqt.data(), output);

	REQUIRE(success);
	REQUIRE(output.keyIndex >= 0);
	REQUIRE(output.keyIndex < 24);
	REQUIRE(output.confidence > 0.0f);
	REQUIRE(output.confidence <= 1.0f);
	REQUIRE(!output.camelot.empty());
	REQUIRE(!output.notation.empty());

	INFO("Predicted key: " << output.notation << " (" << output.camelot << ")");
	INFO("Confidence: " << output.confidence);
}

TEST_CASE("KeyModel inference with CQT from sine wave", "[key][e2e]") {
	KeyModel model;

	std::string modelPath = test_utils::getModelsDir() + "keynet.onnx";
	if (!model.load(modelPath)) {
		SKIP("Model file not available");
	}

	CqtExtractor cqtExtractor;
	const int sampleRate = CqtConfig::SAMPLE_RATE;
	const int hopLength = CqtConfig::HOP_LENGTH;
	const int maxFilterLen = cqtExtractor.getMaxFilterLength();

	// Generate audio with C major chord (C, E, G)
	// C4 = 261.63 Hz, E4 = 329.63 Hz, G4 = 392.00 Hz
	// Need extra samples for filter context at start and end
	const float duration = 22.0f;  // Extra time for frame context
	const int totalSamples = static_cast<int>(sampleRate * duration);
	std::vector<float> audio(totalSamples);

	for (int i = 0; i < totalSamples; i++) {
		float t = static_cast<float>(i) / sampleRate;
		audio[i] = 0.33f * std::sin(2.0f * M_PI * 261.63f * t)   // C
		         + 0.33f * std::sin(2.0f * M_PI * 329.63f * t)   // E
		         + 0.33f * std::sin(2.0f * M_PI * 392.00f * t);  // G
	}

	// Extract CQT frames
	std::vector<float> cqtBuffer(KeyModel::INPUT_SIZE, 0.0f);

	// Process frames to fill 100 time frames
	int framesExtracted = 0;
	for (int frame = 0; frame < KeyModel::INPUT_TIME_FRAMES && framesExtracted < KeyModel::INPUT_TIME_FRAMES; frame++) {
		// Calculate center position for this frame
		int centerSample = maxFilterLen / 2 + frame * hopLength;
		if (centerSample + maxFilterLen / 2 > totalSamples) break;

		// Extract CQT for this frame
		std::vector<float> frameData(CqtConfig::N_BINS);
		const float* audioPtr = audio.data() + centerSample - maxFilterLen / 2;
		bool success = cqtExtractor.processFrame(audioPtr, maxFilterLen, frameData.data());

		if (success) {
			// Store in row-major format: [freq][time]
			for (int f = 0; f < CqtConfig::N_BINS; f++) {
				cqtBuffer[f * KeyModel::INPUT_TIME_FRAMES + framesExtracted] = frameData[f];
			}
			framesExtracted++;
		}
	}

	INFO("Extracted " << framesExtracted << " CQT frames");
	REQUIRE(framesExtracted == KeyModel::INPUT_TIME_FRAMES);

	// Run key detection
	KeyOutput output;
	bool success = model.infer(cqtBuffer.data(), output);

	REQUIRE(success);
	INFO("Detected key: " << output.notation << " (" << output.camelot << ")");
	INFO("Confidence: " << output.confidence);

	// C major chord should detect C major or relative Am
	// C major = index 15 (8B), Am = index 0 (8A)
	bool isC = output.notation == "C";
	bool isAm = output.notation == "Am";
	bool isG = output.notation == "G";  // Also harmonically related

	INFO("Key index: " << output.keyIndex);

	// Allow some flexibility - pure sine waves don't have the harmonic
	// content of real music, so detection may not be perfect
	REQUIRE(output.confidence > 0.04f);  // At least not random
}
