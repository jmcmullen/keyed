/**
 * Batch Test - Test BPM and Key detection accuracy on WAV files in test-data/
 *
 * File naming: filename = expected BPM (e.g., 132.wav expects 132 BPM)
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "Engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <libgen.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <map>

using namespace engine;

// Get the directory containing the executable
std::string getExecutableDir() {
	char path[1024];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size) == 0) {
		char* dir = dirname(path);
		return std::string(dir);
	}
	return ".";
}

// Paths relative to executable (in cli/build/)
std::string getModelPath() {
	return getExecutableDir() + "/../../models/beatnet.onnx";
}

std::string getKeyModelPath() {
	return getExecutableDir() + "/../../models/keynet.onnx";
}

std::string getTestDataPath() {
	return getExecutableDir() + "/../../test-data";
}

// Audio settings - use native 44100 Hz for key detection
constexpr int TARGET_SAMPLE_RATE = 44100;

// Expected keys from Rekordbox (ground truth)
std::map<std::string, std::string> getExpectedKeys() {
	return {
		{"116", "6B"},
		{"118_2", "9A"},
		{"118", "9A"},
		{"120", "11B"},
		{"121", "2A"},
		{"123", "8A"},
		{"124", "8A"},
		{"125", "11B"},
		{"126", "11B"},
		{"127_2A", "2A"},
		{"131", "8A"},
		{"132_2", "8A"},
		{"132", "12B"},
		{"134", "8A"},
		{"138", "2A"},
		{"140", "8A"},
	};
}

struct TestResult {
	std::string filename;
	float expectedBpm;
	float detectedBpm;
	float bpmError;
	size_t bpmFrames;
	bool bpmPassed;  // Within 0.5 BPM

	std::string expectedKey;
	std::string detectedKey;
	float keyConfidence;
	size_t keyFrames;
	bool keyPassed;  // Exact match
};

std::string extractBasename(const std::string& filename) {
	// Extract filename without path and extension
	size_t lastSlash = filename.find_last_of("/\\");
	std::string basename = (lastSlash != std::string::npos)
		? filename.substr(lastSlash + 1)
		: filename;

	size_t lastDot = basename.find_last_of('.');
	if (lastDot != std::string::npos) {
		basename = basename.substr(0, lastDot);
	}

	return basename;
}

float extractExpectedBpm(const std::string& filename) {
	std::string basename = extractBasename(filename);

	// Handle variants like "132_2" - extract BPM before underscore
	size_t underscore = basename.find('_');
	if (underscore != std::string::npos) {
		basename = basename.substr(0, underscore);
	}

	// Parse as float
	return static_cast<float>(atof(basename.c_str()));
}

TestResult testFile(const std::string& filepath, Engine& engine,
                    const std::map<std::string, std::string>& expectedKeys) {
	TestResult result;
	result.filename = filepath;
	result.expectedBpm = extractExpectedBpm(filepath);
	result.detectedBpm = 0;
	result.bpmError = 0;
	result.bpmFrames = 0;
	result.bpmPassed = false;

	// Look up expected key
	std::string basename = extractBasename(filepath);
	auto it = expectedKeys.find(basename);
	result.expectedKey = (it != expectedKeys.end()) ? it->second : "?";
	result.detectedKey = "";
	result.keyConfidence = 0;
	result.keyFrames = 0;
	result.keyPassed = false;

	// Initialize decoder at 44100 Hz (native for key detection)
	ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 1, TARGET_SAMPLE_RATE);
	ma_decoder decoder;

	if (ma_decoder_init_file(filepath.c_str(), &decoderConfig, &decoder) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to open %s\n", filepath.c_str());
		return result;
	}

	// Reset engine state
	engine.reset();

	// Process audio in chunks (882 samples = 20ms at 44100Hz)
	constexpr int CHUNK_SIZE = 882;
	float buffer[CHUNK_SIZE];
	static constexpr int MAX_RESULTS = 16;
	Engine::FrameResult results[MAX_RESULTS];

	ma_uint64 framesRead;

	while (ma_decoder_read_pcm_frames(&decoder, buffer, CHUNK_SIZE, &framesRead) == MA_SUCCESS && framesRead > 0) {
		engine.processAudio(buffer, static_cast<int>(framesRead), results, MAX_RESULTS);
	}

	ma_decoder_uninit(&decoder);

	// Get BPM results
	result.detectedBpm = engine.getBpm();
	result.bpmFrames = engine.getFrameCount();

	if (result.expectedBpm > 0 && result.detectedBpm > 0) {
		result.bpmError = result.detectedBpm - result.expectedBpm;
		result.bpmPassed = std::fabs(result.bpmError) <= 0.5f;
	}

	// Get Key results
	auto keyResult = engine.getKey();
	result.keyFrames = engine.getKeyFrameCount();
	if (keyResult.valid) {
		result.detectedKey = keyResult.camelot;
		result.keyConfidence = keyResult.confidence;
		result.keyPassed = (result.detectedKey == result.expectedKey);
	}

	return result;
}

std::vector<std::string> getAudioFiles(const std::string& path) {
	std::vector<std::string> files;

	// Check if path is a file or directory
	DIR* dir = opendir(path.c_str());
	if (dir) {
		// It's a directory - scan for audio files
		struct dirent* entry;
		while ((entry = readdir(dir)) != nullptr) {
			std::string name = entry->d_name;
			// Check for various audio extensions
				auto hasExt = [&name](const std::string& ext) {
					if (name.size() <= ext.size()) return false;
					std::string suffix = name.substr(name.size() - ext.size());
					for (char& c : suffix) {
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					}
					return suffix == ext;
				};
			if (hasExt(".m4a") || hasExt(".mp3") || hasExt(".wav") ||
			    hasExt(".ogg") || hasExt(".flac") || hasExt(".aiff") || hasExt(".aif")) {
				files.push_back(path + "/" + name);
			}
		}
		closedir(dir);

		// Sort files by expected BPM
		std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
			return extractExpectedBpm(a) < extractExpectedBpm(b);
		});
	} else {
		// It's a single file
		files.push_back(path);
	}

	return files;
}

int main() {
	// Load BPM model
	Engine engine;
	std::string modelPath = getModelPath();
	if (!engine.loadModel(modelPath)) {
		fprintf(stderr, "Error: Failed to load BeatNet from %s\n", modelPath.c_str());
		return 1;
	}
	printf("Loaded BeatNet model\n");

	// Load Key model
	std::string keyModelPath = getKeyModelPath();
	if (!engine.loadKeyModel(keyModelPath)) {
		fprintf(stderr, "Error: Failed to load MusicalKeyCNN from %s\n", keyModelPath.c_str());
		return 1;
	}
	printf("Loaded MusicalKeyCNN model\n");

	// Get expected keys from Rekordbox
	auto expectedKeys = getExpectedKeys();

	// Get audio files
	std::vector<std::string> files = getAudioFiles(getTestDataPath());
	if (files.empty()) {
		fprintf(stderr, "Error: No audio files found\n");
		return 1;
	}

	printf("\nTesting %zu audio file(s) at 44100Hz...\n", files.size());
	printf("=====================================================================================================\n");
	printf("%-15s | %8s %8s %7s %4s | %6s %6s %5s %4s\n",
		"File", "ExpBPM", "DetBPM", "Error", "OK", "ExpKey", "DetKey", "Conf", "OK");
	printf("=====================================================================================================\n");

	std::vector<TestResult> results;
	int bpmPassed = 0;
	int keyPassed = 0;
	float totalBpmError = 0;

	for (const auto& file : files) {
		TestResult result = testFile(file, engine, expectedKeys);
		results.push_back(result);

		// Extract just filename for display
		std::string displayName = extractBasename(file);

		printf("%-15s | %8.1f %8.1f %+7.2f %4s | %6s %6s %5.0f%% %4s\n",
			displayName.c_str(),
			result.expectedBpm,
			result.detectedBpm,
			result.bpmError,
			result.bpmPassed ? "YES" : "NO",
			result.expectedKey.c_str(),
			result.detectedKey.empty() ? "-" : result.detectedKey.c_str(),
			result.keyConfidence * 100.0f,
			result.keyPassed ? "YES" : "NO");

		if (result.bpmPassed) bpmPassed++;
		if (result.keyPassed) keyPassed++;
		totalBpmError += std::fabs(result.bpmError);
	}

	printf("=====================================================================================================\n");

	// Summary
	float avgBpmError = totalBpmError / static_cast<float>(results.size());
	printf("\nBPM Summary:\n");
	printf("  Passed (within 0.5 BPM): %d / %zu (%.1f%%)\n",
		bpmPassed, results.size(),
		100.0f * bpmPassed / results.size());
	printf("  Average absolute error: %.2f BPM\n", avgBpmError);

	printf("\nKey Summary:\n");
	printf("  Matched Rekordbox: %d / %zu (%.1f%%)\n",
		keyPassed, results.size(),
		100.0f * keyPassed / results.size());

	// Show key mismatches
	printf("\nKey Mismatches (vs Rekordbox):\n");
	bool anyKeyMismatch = false;
	for (const auto& r : results) {
		if (!r.keyPassed && !r.detectedKey.empty()) {
			anyKeyMismatch = true;
			std::string displayName = extractBasename(r.filename);
			printf("  %s: Rekordbox=%s, Ours=%s (%.0f%% confidence)\n",
				displayName.c_str(), r.expectedKey.c_str(),
				r.detectedKey.c_str(), r.keyConfidence * 100.0f);
		}
	}
	if (!anyKeyMismatch) {
		printf("  None!\n");
	}

	// Show files where key wasn't detected
	printf("\nNo Key Detected:\n");
	bool anyNoKey = false;
	for (const auto& r : results) {
		if (r.detectedKey.empty()) {
			anyNoKey = true;
			std::string displayName = extractBasename(r.filename);
			printf("  %s: only %zu CQT frames (need 100)\n",
				displayName.c_str(), r.keyFrames);
		}
	}
	if (!anyNoKey) {
		printf("  None!\n");
	}

	bool hasFailures =
		(bpmPassed != static_cast<int>(results.size())) ||
		(keyPassed != static_cast<int>(results.size()));
	return hasFailures ? 1 : 0;
}
