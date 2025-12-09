/**
 * ONNX Model Tests
 *
 * Tests the native ONNX inference against Python golden files.
 * These tests verify that:
 * 1. The model loads correctly
 * 2. Inference produces expected activations
 * 3. LSTM state is maintained correctly across frames
 */

#include "catch_amalgamated.hpp"
#include "OnnxModel.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

// Skip all tests if ONNX is not enabled
#ifdef ONNX_ENABLED

TEST_CASE("OnnxModel initialization", "[onnx]") {
    OnnxModel model;

    SECTION("model is not ready before loading") {
        REQUIRE_FALSE(model.isReady());
    }
}

TEST_CASE("OnnxModel loads model file", "[onnx]") {
    OnnxModel model;

    std::string modelPath = test_utils::getModelPath();
    INFO("Model path: " << modelPath);

    bool loaded = model.load(modelPath);

    if (!loaded) {
        WARN("Model file not found at: " << modelPath);
        SKIP("Model file not available for testing");
    }

    REQUIRE(model.isReady());
}

TEST_CASE("OnnxModel inference produces valid outputs", "[onnx]") {
    OnnxModel model;

    std::string modelPath = test_utils::getModelPath();
    if (!model.load(modelPath)) {
        SKIP("Model file not available");
    }

    SECTION("outputs are probabilities (0-1 range)") {
        // Create test input (272 features)
        std::vector<float> features(OnnxModel::INPUT_DIM, 0.5f);

        ModelOutput output;
        bool success = model.infer(features.data(), output);

        REQUIRE(success);
        REQUIRE(output.beatActivation >= 0.0f);
        REQUIRE(output.beatActivation <= 1.0f);
        REQUIRE(output.downbeatActivation >= 0.0f);
        REQUIRE(output.downbeatActivation <= 1.0f);
    }

    SECTION("outputs are finite (no NaN/Inf)") {
        std::vector<float> features(OnnxModel::INPUT_DIM);
        // Random-ish but deterministic input
        for (int i = 0; i < OnnxModel::INPUT_DIM; ++i) {
            features[i] = std::sin(static_cast<float>(i) * 0.1f) * 2.0f;
        }

        ModelOutput output;
        bool success = model.infer(features.data(), output);

        REQUIRE(success);
        REQUIRE(std::isfinite(output.beatActivation));
        REQUIRE(std::isfinite(output.downbeatActivation));
    }
}

TEST_CASE("OnnxModel reset clears LSTM state", "[onnx]") {
    OnnxModel model;

    std::string modelPath = test_utils::getModelPath();
    if (!model.load(modelPath)) {
        SKIP("Model file not available");
    }

    // Run some frames to build up LSTM state
    std::vector<float> features(OnnxModel::INPUT_DIM, 0.5f);
    ModelOutput output1, output2;

    for (int i = 0; i < 10; ++i) {
        model.infer(features.data(), output1);
    }

    // Reset and run same frames again
    model.resetState();

    for (int i = 0; i < 10; ++i) {
        model.infer(features.data(), output2);
    }

    // After same sequence, outputs should be identical
    REQUIRE(output1.beatActivation == Approx(output2.beatActivation).margin(1e-5));
    REQUIRE(output1.downbeatActivation == Approx(output2.downbeatActivation).margin(1e-5));
}

TEST_CASE("OnnxModel matches Python golden files", "[onnx][golden]") {
    OnnxModel model;

    std::string modelPath = test_utils::getModelPath();
    if (!model.load(modelPath)) {
        SKIP("Model file not available");
    }

    // Test against each BPM golden file
    std::vector<std::string> bpmLevels = {"116", "120", "125", "126", "131", "132", "134", "140"};

    for (const auto& bpm : bpmLevels) {
        DYNAMIC_SECTION("matches golden for " << bpm << " BPM") {
            std::string melPath = test_utils::getGoldenDir() + "mel_golden_" + bpm + "bpm.bin";
            std::string actPath = test_utils::getGoldenDir() + "onnx_activations_" + bpm + "bpm.bin";

            // Load mel features
            test_utils::BinaryFeatures melData;
            try {
                melData = test_utils::loadBinaryFeatures(melPath);
            } catch (const std::exception& e) {
                WARN("Mel golden file not found: " << melPath);
                continue;
            }

            // Load expected activations
            test_utils::BinaryActivations actData;
            try {
                actData = test_utils::loadBinaryActivations(actPath);
            } catch (const std::exception& e) {
                WARN("Activation golden file not found: " << actPath);
                continue;
            }

            REQUIRE(melData.numFrames == actData.numFrames);
            REQUIRE(melData.featureDim == OnnxModel::INPUT_DIM);

            INFO("Testing " << melData.numFrames << " frames");

            // Reset model state
            model.resetState();

            // Run inference frame by frame
            float maxBeatError = 0.0f;
            float maxDownbeatError = 0.0f;
            float totalBeatError = 0.0f;
            float totalDownbeatError = 0.0f;

            for (int i = 0; i < melData.numFrames; ++i) {
                const float* frameFeatures = &melData.features[i * melData.featureDim];

                ModelOutput output;
                bool success = model.infer(frameFeatures, output);
                REQUIRE(success);

                float beatError = std::abs(output.beatActivation - actData.beatActivations[i]);
                float downbeatError = std::abs(output.downbeatActivation - actData.downbeatActivations[i]);

                maxBeatError = std::max(maxBeatError, beatError);
                maxDownbeatError = std::max(maxDownbeatError, downbeatError);
                totalBeatError += beatError;
                totalDownbeatError += downbeatError;

                // Per-frame check with tolerance for floating point differences
                // ONNX Runtime implementations can vary slightly across platforms
                if (beatError > 0.01f) {
                    INFO("Frame " << i << ": beat activation mismatch");
                    INFO("  Expected: " << actData.beatActivations[i]);
                    INFO("  Got: " << output.beatActivation);
                }
                REQUIRE(beatError < 0.05f);  // Allow 5% tolerance for platform differences

                if (downbeatError > 0.01f) {
                    INFO("Frame " << i << ": downbeat activation mismatch");
                    INFO("  Expected: " << actData.downbeatActivations[i]);
                    INFO("  Got: " << output.downbeatActivation);
                }
                REQUIRE(downbeatError < 0.05f);
            }

            float meanBeatError = totalBeatError / melData.numFrames;
            float meanDownbeatError = totalDownbeatError / melData.numFrames;

            INFO("Max beat error: " << maxBeatError);
            INFO("Max downbeat error: " << maxDownbeatError);
            INFO("Mean beat error: " << meanBeatError);
            INFO("Mean downbeat error: " << meanDownbeatError);

            // Overall accuracy requirements
            REQUIRE(meanBeatError < 0.01f);
            REQUIRE(meanDownbeatError < 0.01f);
        }
    }
}

TEST_CASE("OnnxModel LSTM state persistence", "[onnx]") {
    OnnxModel model;

    std::string modelPath = test_utils::getModelPath();
    if (!model.load(modelPath)) {
        SKIP("Model file not available");
    }

    // LSTM should produce different outputs for same input
    // after processing different history
    std::vector<float> features(OnnxModel::INPUT_DIM, 0.5f);
    ModelOutput output1, output2;

    // Path 1: Run 10 frames of low activation input, then test
    model.resetState();
    std::vector<float> lowFeatures(OnnxModel::INPUT_DIM, 0.1f);
    for (int i = 0; i < 10; ++i) {
        model.infer(lowFeatures.data(), output1);
    }
    model.infer(features.data(), output1);

    // Path 2: Run 10 frames of high activation input, then test
    model.resetState();
    std::vector<float> highFeatures(OnnxModel::INPUT_DIM, 2.0f);
    for (int i = 0; i < 10; ++i) {
        model.infer(highFeatures.data(), output2);
    }
    model.infer(features.data(), output2);

    // Outputs should be different due to different LSTM history
    bool outputsDiffer = (std::abs(output1.beatActivation - output2.beatActivation) > 0.01f) ||
                         (std::abs(output1.downbeatActivation - output2.downbeatActivation) > 0.01f);

    REQUIRE(outputsDiffer);
}

#else // !ONNX_ENABLED

TEST_CASE("ONNX tests skipped - ONNX not enabled", "[onnx]") {
    WARN("ONNX Runtime not available - skipping ONNX tests");
    SUCCEED();
}

#endif // ONNX_ENABLED
