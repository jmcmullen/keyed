#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <stdexcept>
#include <random>

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

inline std::string getGoldenDir() {
    return getTestDir() + "golden/";
}

inline std::string getAudioDir() {
    return getTestDir() + "audio/";
}

/**
 * Load raw PCM audio file (float32 little-endian, mono, 22050 Hz)
 * This matches the format output by librosa and numpy.tofile()
 */
inline std::vector<float> loadRawAudio(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open audio file: " + path);
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t numSamples = fileSize / sizeof(float);
    std::vector<float> samples(numSamples);
    file.read(reinterpret_cast<char*>(samples.data()), fileSize);

    return samples;
}

/**
 * Binary features file structure
 */
struct BinaryFeatures {
    int numFrames;
    int featureDim;
    std::vector<float> features;  // Flat array: numFrames * featureDim
};

/**
 * Binary activations file structure (ONNX model outputs)
 */
struct BinaryActivations {
    int numFrames;
    std::vector<float> beatActivations;
    std::vector<float> downbeatActivations;
};

/**
 * Load binary features file
 * Format: [numFrames (int32), featureDim (int32), features (float32 array)]
 */
inline BinaryFeatures loadBinaryFeatures(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open binary features file: " + path);
    }

    BinaryFeatures result;
    file.read(reinterpret_cast<char*>(&result.numFrames), sizeof(int32_t));
    file.read(reinterpret_cast<char*>(&result.featureDim), sizeof(int32_t));

    size_t totalFloats = static_cast<size_t>(result.numFrames) * result.featureDim;
    result.features.resize(totalFloats);
    file.read(reinterpret_cast<char*>(result.features.data()), totalFloats * sizeof(float));

    return result;
}

/**
 * Load binary activations file (ONNX model outputs)
 * Format: [numFrames (int32), activations (float32 array of [beat, downbeat] pairs)]
 */
inline BinaryActivations loadBinaryActivations(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open binary activations file: " + path);
    }

    BinaryActivations result;
    file.read(reinterpret_cast<char*>(&result.numFrames), sizeof(int32_t));

    result.beatActivations.resize(result.numFrames);
    result.downbeatActivations.resize(result.numFrames);

    // Read interleaved [beat, downbeat] pairs
    for (int i = 0; i < result.numFrames; ++i) {
        float beat, downbeat;
        file.read(reinterpret_cast<char*>(&beat), sizeof(float));
        file.read(reinterpret_cast<char*>(&downbeat), sizeof(float));
        result.beatActivations[i] = beat;
        result.downbeatActivations[i] = downbeat;
    }

    return result;
}

/**
 * Get the path to the ONNX model for testing
 */
inline std::string getModelPath() {
    std::string testDir = getTestDir();
    return testDir + "../models/beatnet.onnx";
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
        for (int j = 0; j < clickLength && startSample + j < numSamples; ++j) {
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
 * Calculate mean absolute error between two vectors
 */
inline float meanAbsoluteError(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        return std::numeric_limits<float>::max();
    }
    float sum = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(a[i] - b[i]);
    }
    return sum / static_cast<float>(a.size());
}

/**
 * Calculate max absolute error between two vectors
 */
inline float maxAbsoluteError(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        return std::numeric_limits<float>::max();
    }
    float maxErr = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        maxErr = std::max(maxErr, std::abs(a[i] - b[i]));
    }
    return maxErr;
}

/**
 * Find index of maximum value
 */
inline size_t argmax(const std::vector<float>& v) {
    if (v.empty()) return 0;
    return std::max_element(v.begin(), v.end()) - v.begin();
}

/**
 * Simple JSON float array parser
 * Parses: [1.0, 2.0, 3.0] or {"key": [1.0, 2.0, 3.0]}
 */
inline std::vector<float> parseJsonFloatArray(const std::string& json) {
    std::vector<float> result;

    // Find array start
    size_t start = json.find('[');
    size_t end = json.rfind(']');
    if (start == std::string::npos || end == std::string::npos) {
        return result;
    }

    std::string content = json.substr(start + 1, end - start - 1);

    std::istringstream iss(content);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // Trim whitespace
        size_t first = token.find_first_not_of(" \t\n\r");
        size_t last = token.find_last_not_of(" \t\n\r");
        if (first != std::string::npos) {
            try {
                result.push_back(std::stof(token.substr(first, last - first + 1)));
            } catch (...) {
                // Skip non-numeric tokens
            }
        }
    }

    return result;
}

/**
 * Load a JSON file and extract a float array by key path
 * Supports nested keys like "test_cases.sine_440hz.magnitude"
 */
inline std::vector<float> loadJsonArray(const std::string& path, const std::string& keyPath) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open JSON file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Split key path by dots
    std::vector<std::string> keys;
    std::istringstream keyStream(keyPath);
    std::string key;
    while (std::getline(keyStream, key, '.')) {
        keys.push_back(key);
    }

    // Navigate through nested keys
    size_t pos = 0;
    for (const auto& k : keys) {
        std::string searchKey = "\"" + k + "\"";
        pos = content.find(searchKey, pos);
        if (pos == std::string::npos) {
            throw std::runtime_error("Key not found in JSON: " + k);
        }
        pos += searchKey.length();
    }

    // Find the array following the key
    size_t arrayStart = content.find('[', pos);
    if (arrayStart == std::string::npos) {
        throw std::runtime_error("No array found for key: " + keyPath);
    }

    // Find matching closing bracket
    int depth = 1;
    size_t arrayEnd = arrayStart + 1;
    while (arrayEnd < content.size() && depth > 0) {
        if (content[arrayEnd] == '[') depth++;
        else if (content[arrayEnd] == ']') depth--;
        arrayEnd++;
    }

    return parseJsonFloatArray(content.substr(arrayStart, arrayEnd - arrayStart));
}

/**
 * Load a single float value from JSON by key path
 */
inline float loadJsonFloat(const std::string& path, const std::string& keyPath) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open JSON file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Split key path by dots
    std::vector<std::string> keys;
    std::istringstream keyStream(keyPath);
    std::string key;
    while (std::getline(keyStream, key, '.')) {
        keys.push_back(key);
    }

    // Navigate through nested keys
    size_t pos = 0;
    for (const auto& k : keys) {
        std::string searchKey = "\"" + k + "\"";
        pos = content.find(searchKey, pos);
        if (pos == std::string::npos) {
            throw std::runtime_error("Key not found in JSON: " + k);
        }
        pos += searchKey.length();
    }

    // Find the colon after the key
    size_t colonPos = content.find(':', pos);
    if (colonPos == std::string::npos) {
        throw std::runtime_error("Invalid JSON format for key: " + keyPath);
    }

    // Find the value (skip whitespace)
    size_t valueStart = colonPos + 1;
    while (valueStart < content.size() && std::isspace(content[valueStart])) {
        valueStart++;
    }

    // Read until comma, closing brace, or newline
    size_t valueEnd = valueStart;
    while (valueEnd < content.size() &&
           content[valueEnd] != ',' &&
           content[valueEnd] != '}' &&
           content[valueEnd] != '\n') {
        valueEnd++;
    }

    std::string valueStr = content.substr(valueStart, valueEnd - valueStart);
    // Trim whitespace
    size_t first = valueStr.find_first_not_of(" \t\r\n");
    size_t last = valueStr.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        throw std::runtime_error("Empty value for key: " + keyPath);
    }
    valueStr = valueStr.substr(first, last - first + 1);

    return std::stof(valueStr);
}

} // namespace test_utils
