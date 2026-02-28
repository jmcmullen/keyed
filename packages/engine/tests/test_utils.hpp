#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <algorithm>
#include <limits>

namespace test_utils {

/**
 * Get the directory containing test files
 */
inline std::string getTestDir() {
    // __FILE__ is the path to this header
    std::string path(__FILE__);
    size_t lastSlash = path.rfind('/');
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash + 1);
    }
    return "./";
}

inline std::string getModelsDir() {
    return getTestDir() + "../models/";
}

/**
 * Get the path to the BeatNet ONNX model
 */
inline std::string getModelPath() {
    return getModelsDir() + "beatnet.onnx";
}

/**
 * Generate a sine wave for testing
 */
inline std::vector<float> generateSineWave(float frequency, float sampleRate,
                                           size_t numSamples, float amplitude = 1.0f) {
    std::vector<float> samples(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        samples[i] = amplitude * std::sin(2.0f * M_PI * frequency * t);
    }
    return samples;
}

/**
 * Generate an impulse signal
 */
inline std::vector<float> generateImpulse(size_t numSamples) {
    std::vector<float> samples(numSamples, 0.0f);
    if (numSamples > 0) {
        samples[0] = 1.0f;
    }
    return samples;
}

/**
 * Generate random noise
 */
inline std::vector<float> generateNoise(size_t numSamples, float amplitude = 0.1f,
                                        unsigned int seed = 42) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> dist(0.0f, amplitude);

    std::vector<float> samples(numSamples);
    for (auto& s : samples) {
        s = dist(rng);
    }
    return samples;
}

/**
 * Generate a click track (synthetic beats)
 */
inline std::vector<float> generateClickTrack(float bpm, float sampleRate,
                                             float durationSeconds,
                                             float clickFrequency = 1000.0f) {
    size_t numSamples = static_cast<size_t>(durationSeconds * sampleRate);
    std::vector<float> samples(numSamples, 0.0f);

    float samplesPerBeat = (60.0f / bpm) * sampleRate;
    int clickLength = 100;  // Short click

    for (float pos = 0; pos < numSamples; pos += samplesPerBeat) {
        int startSample = static_cast<int>(pos);
        for (int j = 0; j < clickLength && startSample + j < static_cast<int>(numSamples); ++j) {
            float t = static_cast<float>(j) / sampleRate;
            float envelope = std::exp(-static_cast<float>(j) / 20.0f);
            samples[startSample + j] = envelope * std::sin(2.0f * M_PI * clickFrequency * t);
        }
    }

    return samples;
}

/**
 * Compare floats with tolerance
 */
inline bool floatsEqual(float a, float b, float tolerance = 1e-5f) {
    return std::abs(a - b) < tolerance;
}

/**
 * Find index of maximum value
 */
inline size_t argmax(const std::vector<float>& v) {
    if (v.empty()) return std::numeric_limits<size_t>::max();
    return std::max_element(v.begin(), v.end()) - v.begin();
}

} // namespace test_utils
