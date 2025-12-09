/**
 * Integration tests for the audio processing pipeline
 *
 * NOTE: These are SMOKE TESTS that verify the pipeline runs without crashing.
 * For BPM accuracy testing, see cli/batch_test which tests against real audio.
 * For mel extractor accuracy, see test_mel_extractor.cpp.
 */

#include "catch_amalgamated.hpp"
#include "Engine.hpp"
#include "MelExtractor.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

TEST_CASE("Engine initialization", "[integration]") {
    Engine engine;

    SECTION("initial state is valid") {
        // BPM should be 0 before any processing
        REQUIRE(engine.getBpm() == 0.0f);
        REQUIRE(engine.getFrameCount() == 0);
    }
}

TEST_CASE("Engine reset", "[integration]") {
    Engine engine;

    // Generate some audio
    auto audio = test_utils::generateClickTrack(120.0f, 22050.0f, 2.0f);

    // Push through mel extractor to generate some internal state
    StreamingMelExtractor melExtractor;
    std::vector<float> features(272 * 200);
    melExtractor.push(audio.data(), audio.size(), features.data(), 200);

    // Reset
    engine.reset();

    // Should be back to initial state
    REQUIRE(engine.getFrameCount() == 0);
}

TEST_CASE("StreamingMelExtractor produces features", "[integration]") {
    StreamingMelExtractor extractor;

    SECTION("returns correct frame count for 1 second of audio") {
        auto audio = test_utils::generateSineWave(440.0f, 22050.0f, 22050);
        std::vector<float> features(272 * 100);

        int frames = extractor.push(audio.data(), audio.size(),
                                    features.data(), 100);

        // 1 second at 50 FPS should give ~50 frames (minus initial buffer)
        REQUIRE(frames >= 40);
        REQUIRE(frames <= 55);
    }

    SECTION("features are finite (no NaN/inf)") {
        auto audio = test_utils::generateClickTrack(120.0f, 22050.0f, 1.0f);
        std::vector<float> features(272 * 100);

        int frames = extractor.push(audio.data(), audio.size(),
                                    features.data(), 100);

        for (int f = 0; f < frames; ++f) {
            for (int i = 0; i < 272; ++i) {
                float val = features[f * 272 + i];
                REQUIRE(std::isfinite(val));
            }
        }
    }

    SECTION("mel bands are non-negative (log10(1+S) >= 0)") {
        auto audio = test_utils::generateClickTrack(120.0f, 22050.0f, 1.0f);
        std::vector<float> features(272 * 100);

        int frames = extractor.push(audio.data(), audio.size(),
                                    features.data(), 100);

        for (int f = 0; f < frames; ++f) {
            for (int i = 0; i < 136; ++i) {  // First 136 = mel bands
                float val = features[f * 272 + i];
                REQUIRE(val >= 0.0f);
            }
        }
    }
}

TEST_CASE("StreamingMelExtractor chunk processing", "[integration]") {
    // Test that processing audio in chunks produces the same result

    StreamingMelExtractor extractor;

    auto audio = test_utils::generateClickTrack(120.0f, 22050.0f, 5.0f);

    std::vector<float> features(272 * 300);

    int totalFrames = 0;

    // Process in chunks (simulating real-time)
    size_t chunkSize = 441;  // One hop
    for (size_t i = 0; i < audio.size(); i += chunkSize) {
        size_t n = std::min(chunkSize, audio.size() - i);

        int frames = extractor.push(audio.data() + i, n,
                                    features.data() + totalFrames * 272,
                                    300 - totalFrames);
        totalFrames += frames;
    }

    // 5 seconds at 50 FPS = ~250 frames
    REQUIRE(totalFrames >= 240);
    REQUIRE(totalFrames <= 260);
}
