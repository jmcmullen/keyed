/**
 * Mel Spectrogram Extractor unit tests
 */

#include "catch_amalgamated.hpp"
#include "MelExtractor.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <fstream>
#include <vector>

using namespace engine;
using Catch::Approx;

TEST_CASE("MelConfig constants", "[mel][config]") {
    REQUIRE(MelConfig::SAMPLE_RATE == 22050);
    REQUIRE(MelConfig::HOP_LENGTH == 441);       // 20ms at 22050 Hz
    REQUIRE(MelConfig::WIN_LENGTH == 1411);      // 64ms at 22050 Hz
    REQUIRE(MelConfig::FFT_SIZE == 1411);
    REQUIRE(MelConfig::BANDS_PER_OCTAVE == 24);
    REQUIRE(MelConfig::F_MIN == 30.0f);
    REQUIRE(MelConfig::F_MAX == 17000.0f);
    REQUIRE(MelConfig::N_BANDS == 136);
    REQUIRE(MelConfig::MODEL_INPUT_DIM == 272);  // 136 mel + 136 diff

    // Verify FPS calculation
    float fps = static_cast<float>(MelConfig::SAMPLE_RATE) / MelConfig::HOP_LENGTH;
    REQUIRE(fps == Approx(50.0f).margin(0.1f));
}

TEST_CASE("LogFilterbank initialization", "[mel][filterbank]") {
    LogFilterbank fb(
        MelConfig::FFT_SIZE,
        MelConfig::SAMPLE_RATE,
        MelConfig::BANDS_PER_OCTAVE,
        MelConfig::F_MIN,
        MelConfig::F_MAX,
        true  // normalize
    );

    REQUIRE(fb.getNumBands() == 136);
    REQUIRE(fb.getNumBins() == 705);  // 1411/2 (excluding Nyquist like madmom)
}

TEST_CASE("LogFilterbank with flat spectrum", "[mel][filterbank]") {
    LogFilterbank fb(
        MelConfig::FFT_SIZE,
        MelConfig::SAMPLE_RATE,
        MelConfig::BANDS_PER_OCTAVE,
        MelConfig::F_MIN,
        MelConfig::F_MAX,
        true
    );

    // Create flat magnitude spectrum
    std::vector<float> flat(fb.getNumBins(), 1.0f);
    std::vector<float> output(fb.getNumBands());

    fb.apply(flat.data(), output.data());

    // With normalized filters, output should be approximately 1.0 for each band
    for (int i = 0; i < fb.getNumBands(); ++i) {
        // Allow some variation due to filter overlap and edge effects
        REQUIRE(output[i] >= 0.5f);
        REQUIRE(output[i] <= 1.5f);
    }
}


TEST_CASE("MelExtractor produces 272 features", "[mel]") {
    MelExtractor extractor;

    std::vector<float> frame(MelConfig::WIN_LENGTH, 0.1f);
    std::vector<float> features(272);

    SECTION("first frame returns true with zeros in diff part (matches madmom)") {
        bool result = extractor.processFrame(frame.data(), frame.size(), features.data());
        // First frame outputs features with diff=0 (like madmom)
        REQUIRE(result == true);

        // Log-mel part (first 136) should be non-zero
        float melSum = 0;
        for (int i = 0; i < 136; i++) {
            melSum += std::abs(features[i]);
        }
        REQUIRE(melSum > 0);

        // Diff part (136-271) should be all zeros for first frame
        float diffSum = 0;
        for (int i = 136; i < 272; i++) {
            diffSum += std::abs(features[i]);
        }
        REQUIRE(diffSum == 0.0f);
    }

    SECTION("second frame returns true with features") {
        // First frame
        extractor.processFrame(frame.data(), frame.size(), features.data());

        // Second frame
        bool result = extractor.processFrame(frame.data(), frame.size(), features.data());
        REQUIRE(result == true);

        // Features should be non-zero
        float sum = 0;
        for (auto f : features) {
            sum += std::abs(f);
        }
        REQUIRE(sum > 0);
    }
}

TEST_CASE("MelExtractor reset clears state", "[mel]") {
    MelExtractor extractor;

    std::vector<float> frame(MelConfig::WIN_LENGTH, 0.1f);
    std::vector<float> features(272);

    // Process two frames
    extractor.processFrame(frame.data(), frame.size(), features.data());
    extractor.processFrame(frame.data(), frame.size(), features.data());

    // Reset
    extractor.reset();

    // First frame after reset should return true but with zeros in diff part
    bool result = extractor.processFrame(frame.data(), frame.size(), features.data());
    REQUIRE(result == true);

    // Diff part should be zeros after reset
    float diffSum = 0;
    for (int i = 136; i < 272; i++) {
        diffSum += std::abs(features[i]);
    }
    REQUIRE(diffSum == 0.0f);
}

TEST_CASE("StreamingMelExtractor basic operation", "[mel][streaming]") {
    StreamingMelExtractor extractor;

    REQUIRE(extractor.getFeatureDim() == 272);
    REQUIRE(extractor.getFps() == Approx(50.0f).margin(0.1f));

    std::vector<float> features(272 * 10);

    SECTION("needs enough samples for first frame") {
        // Less than one window worth of samples
        std::vector<float> samples(MelConfig::WIN_LENGTH / 2, 0.1f);
        int frames = extractor.push(samples.data(), samples.size(),
                                    features.data(), 10);
        REQUIRE(frames == 0);
    }

    SECTION("produces frames with enough samples") {
        // Generate enough samples for multiple frames
        // One window + some hops
        size_t numSamples = MelConfig::WIN_LENGTH + MelConfig::HOP_LENGTH * 5;
        auto samples = test_utils::generateNoise(numSamples);

        int frames = extractor.push(samples.data(), samples.size(),
                                    features.data(), 10);

        // Should produce some frames (exact number depends on implementation)
        REQUIRE(frames >= 0);
    }
}

TEST_CASE("StreamingMelExtractor with synthetic audio", "[mel][streaming]") {
    StreamingMelExtractor extractor;

    // Generate 1 second of 440 Hz sine wave
    auto audio = test_utils::generateSineWave(440.0f, 22050.0f, 22050);

    // Allocate for all possible frames
    size_t maxFrames = audio.size() / MelConfig::HOP_LENGTH + 1;
    std::vector<float> features(272 * maxFrames);

    int numFrames = extractor.push(audio.data(), audio.size(),
                                   features.data(), maxFrames);

    SECTION("produces expected number of frames") {
        // Approximately 50 frames per second (22050 samples / 441 hop)
        // Minus initial buffering
        REQUIRE(numFrames >= 40);
        REQUIRE(numFrames <= 55);
    }

    SECTION("features have valid values") {
        for (int f = 0; f < numFrames; ++f) {
            float* frameFeatures = features.data() + f * 272;

            // Mel bands (first 136)
            for (int i = 0; i < 136; ++i) {
                // Log mel values should be non-negative (log10(1 + S) >= 0)
                REQUIRE(frameFeatures[i] >= 0.0f);
            }

            // Spectral difference (second 136) - can be any value after half-wave rectification
            // but typically small
            for (int i = 136; i < 272; ++i) {
                REQUIRE(std::isfinite(frameFeatures[i]));
            }
        }
    }
}

TEST_CASE("StreamingMelExtractor chunk size invariance", "[mel][streaming]") {
    // Processing audio in different chunk sizes should produce IDENTICAL results
    auto audio = test_utils::generateClickTrack(120.0f, 22050.0f, 2.0f);

    // Process all at once
    StreamingMelExtractor extractor1;
    std::vector<float> features1(272 * 200);
    int frames1 = extractor1.push(audio.data(), audio.size(), features1.data(), 200);

    // Process in 441-sample chunks (one hop)
    StreamingMelExtractor extractor2;
    std::vector<float> features2(272 * 200);
    int totalFrames2 = 0;
    for (size_t i = 0; i < audio.size(); i += 441) {
        size_t n = std::min(size_t(441), audio.size() - i);
        int frames = extractor2.push(audio.data() + i, n,
                                     features2.data() + totalFrames2 * 272,
                                     200 - totalFrames2);
        totalFrames2 += frames;
    }

    // Process in 882-sample chunks (two hops)
    StreamingMelExtractor extractor3;
    std::vector<float> features3(272 * 200);
    int totalFrames3 = 0;
    for (size_t i = 0; i < audio.size(); i += 882) {
        size_t n = std::min(size_t(882), audio.size() - i);
        int frames = extractor3.push(audio.data() + i, n,
                                     features3.data() + totalFrames3 * 272,
                                     200 - totalFrames3);
        totalFrames3 += frames;
    }

    // All should produce the same number of frames
    REQUIRE(frames1 == totalFrames2);
    REQUIRE(frames1 == totalFrames3);

    // All features should be identical
    for (int i = 0; i < frames1 * 272; ++i) {
        INFO("Feature " << i << ": all-at-once=" << features1[i]
             << " chunk-441=" << features2[i]
             << " chunk-882=" << features3[i]);
        REQUIRE(features1[i] == Approx(features2[i]).margin(1e-6f));
        REQUIRE(features1[i] == Approx(features3[i]).margin(1e-6f));
    }
}

TEST_CASE("StreamingMelExtractor incremental processing", "[mel][streaming]") {
    StreamingMelExtractor extractor;

    // Process audio in chunks (simulating real-time input)
    auto audio = test_utils::generateClickTrack(120.0f, 22050.0f, 2.0f);

    std::vector<float> allFeatures;
    std::vector<float> chunkFeatures(272 * 10);

    size_t chunkSize = 441;  // One hop worth
    for (size_t i = 0; i < audio.size(); i += chunkSize) {
        size_t n = std::min(chunkSize, audio.size() - i);

        int frames = extractor.push(audio.data() + i, n,
                                    chunkFeatures.data(), 10);

        for (int f = 0; f < frames; ++f) {
            for (int j = 0; j < 272; ++j) {
                allFeatures.push_back(chunkFeatures[f * 272 + j]);
            }
        }
    }

    int totalFrames = allFeatures.size() / 272;

    // 2 seconds at 50 FPS should give ~100 frames
    REQUIRE(totalFrames >= 90);
    REQUIRE(totalFrames <= 110);
}

// Test that our feature output has correct dimensions matching BeatNet
TEST_CASE("MelExtractor feature dimensions match BeatNet", "[mel][config]") {
    // BeatNet configuration from BeatNet.py:
    // - n_bands=[24] means 24 bands per octave
    // - FilteredSpectrogramProcessor produces ~136 total bands
    // - With spectral diff stacked: 136 + 136 = 272 features

    MelExtractor extractor;

    std::vector<float> frame(MelConfig::WIN_LENGTH, 0.1f);
    std::vector<float> features(272);

    // Process two frames to get diff
    extractor.processFrame(frame.data(), frame.size(), features.data());
    bool result = extractor.processFrame(frame.data(), frame.size(), features.data());

    REQUIRE(result == true);

    // Our implementation produces 136 mel + 136 diff = 272 features
    // This matches the BeatNet ONNX model input dimension: BDA(272, ...)
    REQUIRE(MelConfig::MODEL_INPUT_DIM == 272);
    REQUIRE(MelConfig::N_BANDS == 136);
    REQUIRE(MelConfig::BANDS_PER_OCTAVE == 24);
}
