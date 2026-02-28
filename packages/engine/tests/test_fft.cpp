/**
 * FFT unit tests
 *
 * Tests pocketfft-based FFT implementation against numpy.fft reference.
 * Uses 1411-point FFT to match BeatNet/madmom configuration.
 */

#include "catch_amalgamated.hpp"
#include "FFT.hpp"
#include "MelExtractor.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <vector>
#include <complex>

using namespace engine;
using Catch::Approx;

TEST_CASE("FFT initialization", "[fft]") {
    SECTION("creates FFT of BeatNet config size (1411)") {
        FFT fft(MelConfig::FFT_SIZE);
        REQUIRE(fft.getSize() == 1411);
        REQUIRE(fft.getOutputSize() == 706);  // 1411/2 + 1
    }

    SECTION("creates FFT of power-of-2 size") {
        FFT fft(2048);
        REQUIRE(fft.getSize() == 2048);
        REQUIRE(fft.getOutputSize() == 1025);
    }

    SECTION("creates FFT of odd size") {
        // pocketfft supports any size, not just powers of 2
        FFT fft(1411);
        REQUIRE(fft.getSize() == 1411);
        REQUIRE(fft.getOutputSize() == 706);
    }
}

TEST_CASE("FFT impulse response", "[fft]") {
    constexpr size_t N = MelConfig::FFT_SIZE;  // 1411
    FFT fft(N);

    auto impulse = test_utils::generateImpulse(N);
    std::vector<std::complex<float>> output(fft.getOutputSize());

    fft.forward(impulse.data(), output.data());

    SECTION("impulse has flat magnitude spectrum") {
        std::vector<float> magnitude(fft.getOutputSize());
        fft.magnitude(output.data(), magnitude.data());

        // All bins should have magnitude 1.0
        for (size_t i = 0; i < magnitude.size(); ++i) {
            REQUIRE(magnitude[i] == Approx(1.0f).margin(1e-5f));
        }
    }

    SECTION("impulse has flat power spectrum") {
        std::vector<float> power(fft.getOutputSize());
        fft.powerSpectrum(output.data(), power.data());

        // All bins should have power 1.0
        for (size_t i = 0; i < power.size(); ++i) {
            REQUIRE(power[i] == Approx(1.0f).margin(1e-5f));
        }
    }
}

TEST_CASE("FFT sine wave detection", "[fft]") {
    constexpr size_t N = MelConfig::FFT_SIZE;  // 1411
    constexpr float sampleRate = static_cast<float>(MelConfig::SAMPLE_RATE);
    FFT fft(N);

    SECTION("detects 440 Hz") {
        auto sine = test_utils::generateSineWave(440.0f, sampleRate, N);
        std::vector<std::complex<float>> output(fft.getOutputSize());
        std::vector<float> magnitude(fft.getOutputSize());

        fft.forward(sine.data(), output.data());
        fft.magnitude(output.data(), magnitude.data());

        // Find peak bin
        size_t peakBin = test_utils::argmax(std::vector<float>(
            magnitude.begin(), magnitude.end()));

        // Expected bin: freq * fftSize / sampleRate = 440 * 1411 / 22050 ≈ 28
        float expectedBin = 440.0f * N / sampleRate;
        REQUIRE(std::abs(static_cast<float>(peakBin) - expectedBin) < 2.0f);
    }

    SECTION("detects 1000 Hz") {
        auto sine = test_utils::generateSineWave(1000.0f, sampleRate, N);
        std::vector<std::complex<float>> output(fft.getOutputSize());
        std::vector<float> magnitude(fft.getOutputSize());

        fft.forward(sine.data(), output.data());
        fft.magnitude(output.data(), magnitude.data());

        size_t peakBin = test_utils::argmax(std::vector<float>(
            magnitude.begin(), magnitude.end()));

        float expectedBin = 1000.0f * N / sampleRate;
        REQUIRE(std::abs(static_cast<float>(peakBin) - expectedBin) < 2.0f);
    }
}

TEST_CASE("FFT Parseval's theorem", "[fft]") {
    // Energy in time domain should equal energy in frequency domain
    constexpr size_t N = MelConfig::FFT_SIZE;  // 1411
    FFT fft(N);

    auto noise = test_utils::generateNoise(N, 0.5f);
    std::vector<std::complex<float>> output(fft.getOutputSize());
    std::vector<float> power(fft.getOutputSize());

    fft.forward(noise.data(), output.data());
    fft.powerSpectrum(output.data(), power.data());

    // Time domain energy
    float timeEnergy = 0;
    for (auto s : noise) {
        timeEnergy += s * s;
    }

    // Frequency domain energy (accounting for symmetry)
    float freqEnergy = power[0];  // DC
    for (size_t i = 1; i < power.size() - 1; ++i) {
        freqEnergy += 2.0f * power[i];  // Symmetric bins counted twice
    }
    if (N % 2 == 0) {
        freqEnergy += power[power.size() - 1];  // Nyquist (only for even N)
    } else {
        freqEnergy += 2.0f * power[power.size() - 1];  // Odd N: no Nyquist bin
    }
    freqEnergy /= static_cast<float>(N);  // Normalization

    // Should be approximately equal (within 1%)
    REQUIRE(freqEnergy == Approx(timeEnergy).epsilon(0.01f));
}

TEST_CASE("FFT with exact BeatNet configuration", "[fft][beatnet]") {
    // BeatNet uses 1411-sample windows (64ms at 22050 Hz)
    // pocketfft handles this directly without zero-padding
    constexpr size_t fftSize = MelConfig::FFT_SIZE;  // 1411
    constexpr float sampleRate = static_cast<float>(MelConfig::SAMPLE_RATE);

    FFT fft(fftSize);

    REQUIRE(fft.getSize() == 1411);
    REQUIRE(fft.getOutputSize() == 706);

    SECTION("processes 64ms frames correctly") {
        auto audio = test_utils::generateSineWave(440.0f, sampleRate, fftSize);

        std::vector<std::complex<float>> output(fft.getOutputSize());
        std::vector<float> magnitude(fft.getOutputSize());

        fft.forward(audio.data(), output.data());
        fft.magnitude(output.data(), magnitude.data());

        // Should detect 440 Hz
        size_t peakBin = test_utils::argmax(std::vector<float>(
            magnitude.begin(), magnitude.end()));

        float expectedBin = 440.0f * fftSize / sampleRate;  // ~28
        REQUIRE(std::abs(static_cast<float>(peakBin) - expectedBin) < 2.0f);
    }
}

