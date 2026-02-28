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

#include <algorithm>
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
        REQUIRE(Engine::SAMPLE_RATE == 44100);       // Native sample rate
        REQUIRE(Engine::BPM_SAMPLE_RATE == 22050);   // BPM pipeline sample rate
        REQUIRE(Engine::KEY_SAMPLE_RATE == 44100);   // Key detection sample rate
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

TEST_CASE("Engine key detection initialization", "[e2e][key]") {
    Engine engine;

    SECTION("key not ready before loading model") {
        REQUIRE_FALSE(engine.isKeyReady());
        auto key = engine.getKey();
        REQUIRE_FALSE(key.valid);
    }

    SECTION("key model loads successfully") {
        std::string keyModelPath = test_utils::getModelsDir() + "keynet.onnx";
        bool loaded = engine.loadKeyModel(keyModelPath);

        if (!loaded) {
            WARN("MusicalKeyCNN model not found at: " << keyModelPath);
            SKIP("MusicalKeyCNN model not available");
        }

        REQUIRE(engine.isKeyReady());
    }
}

TEST_CASE("Engine dual pipeline processing", "[e2e][key]") {
    Engine engine;

    // Load both models
    std::string bpmModelPath = test_utils::getModelPath();
    std::string keyModelPath = test_utils::getModelsDir() + "keynet.onnx";

    if (!engine.loadModel(bpmModelPath)) {
        SKIP("BeatNet model not available");
    }
    if (!engine.loadKeyModel(keyModelPath)) {
        SKIP("MusicalKeyCNN model not available");
    }

    REQUIRE(engine.isReady());
    REQUIRE(engine.isKeyReady());

    // Generate 25 seconds of C major chord at 44100 Hz
    // (enough for key detection which needs 100 frames at ~5 FPS)
    const float duration = 25.0f;
    const int totalSamples = static_cast<int>(Engine::SAMPLE_RATE * duration);
    std::vector<float> audio(totalSamples);

    for (int i = 0; i < totalSamples; i++) {
        float t = static_cast<float>(i) / Engine::SAMPLE_RATE;
        // C major chord: C4 (261.63), E4 (329.63), G4 (392.00)
        audio[i] = 0.33f * std::sin(2.0f * M_PI * 261.63f * t)
                 + 0.33f * std::sin(2.0f * M_PI * 329.63f * t)
                 + 0.33f * std::sin(2.0f * M_PI * 392.00f * t);
    }

    // Process in chunks
    std::vector<Engine::FrameResult> results(2000);
    int chunkSize = Engine::SAMPLE_RATE / 10;  // 100ms chunks

	for (int offset = 0; offset < totalSamples; offset += chunkSize) {
		int samplesToProcess = std::min(chunkSize, totalSamples - offset);
		int produced = engine.processAudio(
			audio.data() + offset,
			samplesToProcess,
			results.data(),
			results.size()
		);
		REQUIRE(produced >= 0);
	}

    // Check BPM detection is working
    size_t bpmFrames = engine.getFrameCount();
    INFO("BPM frames: " << bpmFrames);
    REQUIRE(bpmFrames > 100);  // Should have processed many frames

    // Check key detection
    size_t keyFrames = engine.getKeyFrameCount();
    INFO("Key frames: " << keyFrames);

    auto key = engine.getKey();
    INFO("Key valid: " << key.valid);
    INFO("Key: " << key.notation << " (" << key.camelot << ")");
    INFO("Confidence: " << key.confidence);

    // Key should have been detected after 25 seconds
    REQUIRE(key.valid);
    REQUIRE(!key.notation.empty());
    REQUIRE(!key.camelot.empty());
    REQUIRE(key.confidence > 0.0f);
}

#else // !ONNX_ENABLED

TEST_CASE("E2E tests skipped - ONNX not enabled", "[e2e]") {
    WARN("ONNX Runtime not available - skipping E2E tests");
    SUCCEED();
}

#endif // ONNX_ENABLED
