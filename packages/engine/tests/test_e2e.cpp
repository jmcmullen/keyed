/**
 * End-to-End Pipeline Tests
 *
 * Tests the full audio processing pipeline:
 * Audio -> Mel Features -> ONNX Model -> Autocorrelation BPM
 *
 * NOTE: For comprehensive BPM accuracy testing with real audio files,
 * use cli/batch_test which tests against recorded music.
 */

#include "catch_amalgamated.hpp"
#include "Engine.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

// Skip all tests if ONNX is not enabled
#ifdef ONNX_ENABLED

TEST_CASE("Engine initialization", "[e2e]") {
    Engine engine;

    SECTION("engine starts without model") {
        REQUIRE_FALSE(engine.isReady());
    }

    SECTION("constants are correct") {
        REQUIRE(Engine::SAMPLE_RATE == 22050);
        REQUIRE(Engine::HOP_LENGTH == 441);
        REQUIRE(Engine::FEATURE_DIM == 272);
    }
}

TEST_CASE("Engine loads ONNX model", "[e2e]") {
    Engine engine;

    std::string modelPath = test_utils::getModelPath();
    INFO("Model path: " << modelPath);

    bool loaded = engine.loadModel(modelPath);

    if (!loaded) {
        WARN("Model file not found at: " << modelPath);
        SKIP("Model file not available for testing");
    }

    REQUIRE(engine.isReady());
}

TEST_CASE("Engine full pipeline smoke test", "[e2e]") {
    Engine engine;

    std::string modelPath = test_utils::getModelPath();
    if (!engine.loadModel(modelPath)) {
        SKIP("Model file not available");
    }

    SECTION("processes sine wave without crashing") {
        auto audio = test_utils::generateSineWave(440.0f, Engine::SAMPLE_RATE, Engine::SAMPLE_RATE * 2);

        std::vector<Engine::FrameResult> results(200);
        int numResults = engine.processAudio(audio.data(), audio.size(), results.data(), results.size());

        REQUIRE(numResults >= 0);
    }

    SECTION("processes click track without crashing") {
        auto audio = test_utils::generateClickTrack(120.0f, Engine::SAMPLE_RATE, 5.0f);

        std::vector<Engine::FrameResult> results(300);
        int numResults = engine.processAudio(audio.data(), audio.size(), results.data(), results.size());

        REQUIRE(numResults > 0);

        // Verify API is accessible (BPM may be 0 for synthetic audio)
        float bpm = engine.getBpm();
        REQUIRE(bpm >= 0.0f);
    }
}

TEST_CASE("Engine BPM detection on real audio", "[e2e][audio]") {
    Engine engine;

    std::string modelPath = test_utils::getModelPath();
    if (!engine.loadModel(modelPath)) {
        SKIP("Model file not available");
    }

    // Test with real audio files (requires test audio in test-data/)
    std::vector<std::pair<std::string, float>> testCases = {
        {"120", 120.0f},
        {"125", 125.0f},
    };

    for (const auto& [bpmLabel, expectedBpm] : testCases) {
        DYNAMIC_SECTION("detects " << bpmLabel << " BPM audio correctly") {
            std::string audioPath = test_utils::getAudioDir() + bpmLabel + ".raw";

            std::vector<float> audio;
            try {
                audio = test_utils::loadRawAudio(audioPath);
            } catch (const std::exception& e) {
                WARN("Audio file not found: " << audioPath);
                continue;
            }

            INFO("Loaded " << audio.size() << " samples");

            engine.reset();

            std::vector<Engine::FrameResult> results(1000);
            int chunkSize = Engine::SAMPLE_RATE / 2;

            for (size_t offset = 0; offset < audio.size(); offset += chunkSize) {
                int samplesToProcess = std::min(static_cast<int>(audio.size() - offset), chunkSize);
                engine.processAudio(audio.data() + offset, samplesToProcess, results.data(), results.size());
            }

            float detectedBpm = engine.getBpm();
            INFO("Detected BPM: " << detectedBpm << " (expected: " << expectedBpm << ")");

            float bpmError = std::abs(detectedBpm - expectedBpm);
            CHECK(bpmError <= 1.0f);
        }
    }
}

TEST_CASE("Engine state reset", "[e2e]") {
    Engine engine;

    std::string modelPath = test_utils::getModelPath();
    if (!engine.loadModel(modelPath)) {
        SKIP("Model file not available");
    }

    // Process some audio
    auto audio = test_utils::generateClickTrack(120.0f, Engine::SAMPLE_RATE, 3.0f);
    std::vector<Engine::FrameResult> results(200);
    engine.processAudio(audio.data(), audio.size(), results.data(), results.size());

    size_t frameCount1 = engine.getFrameCount();
    REQUIRE(frameCount1 > 0);

    engine.reset();

    size_t frameCount2 = engine.getFrameCount();
    REQUIRE(frameCount2 == 0);
}

#else // !ONNX_ENABLED

TEST_CASE("E2E tests skipped - ONNX not enabled", "[e2e]") {
    WARN("ONNX Runtime not available - skipping E2E tests");
    SUCCEED();
}

#endif // ONNX_ENABLED
