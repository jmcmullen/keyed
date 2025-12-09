/**
 * Batch BPM Test - Test BPM detection accuracy on WAV files in test-data/
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
#include <libgen.h>
#include <unistd.h>
#include <mach-o/dyld.h>

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

std::string getTestDataPath() {
	return getExecutableDir() + "/../../test-data";
}

// Audio settings
constexpr int TARGET_SAMPLE_RATE = 22050;

struct TestResult {
	std::string filename;
	float expectedBpm;
	float detectedBpm;
	float error;
	size_t activationFrames;
	bool passed;  // Within 0.5 BPM
};

float extractExpectedBpm(const std::string& filename) {
	// Extract filename without path and extension
	size_t lastSlash = filename.find_last_of("/\\");
	std::string basename = (lastSlash != std::string::npos)
		? filename.substr(lastSlash + 1)
		: filename;

	size_t lastDot = basename.find_last_of('.');
	if (lastDot != std::string::npos) {
		basename = basename.substr(0, lastDot);
	}

	// Handle variants like "132_2" - extract BPM before underscore
	size_t underscore = basename.find('_');
	if (underscore != std::string::npos) {
		basename = basename.substr(0, underscore);
	}

	// Parse as float
	return static_cast<float>(atof(basename.c_str()));
}

TestResult testFile(const std::string& filepath, Engine& engine) {
	TestResult result;
	result.filename = filepath;
	result.expectedBpm = extractExpectedBpm(filepath);
	result.detectedBpm = 0;
	result.error = 0;
	result.activationFrames = 0;
	result.passed = false;

	// Initialize decoder
	ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 1, TARGET_SAMPLE_RATE);
	ma_decoder decoder;

	if (ma_decoder_init_file(filepath.c_str(), &decoderConfig, &decoder) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to open %s\n", filepath.c_str());
		return result;
	}

	// Reset engine state
	engine.reset();

	// Process audio in chunks
	constexpr int CHUNK_SIZE = 441;  // 20ms at 22050Hz
	float buffer[CHUNK_SIZE];
	static constexpr int MAX_RESULTS = 8;
	Engine::FrameResult results[MAX_RESULTS];

	ma_uint64 framesRead;

	while (ma_decoder_read_pcm_frames(&decoder, buffer, CHUNK_SIZE, &framesRead) == MA_SUCCESS && framesRead > 0) {
		engine.processAudio(buffer, static_cast<int>(framesRead), results, MAX_RESULTS);
	}

	ma_decoder_uninit(&decoder);

	// Get BPM
	result.detectedBpm = engine.getBpm();
	result.activationFrames = engine.getFrameCount();

	if (result.expectedBpm > 0 && result.detectedBpm > 0) {
		result.error = result.detectedBpm - result.expectedBpm;
		result.passed = std::fabs(result.error) <= 0.5f;
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
			if (name.size() > 4) {
				std::string ext = name.substr(name.size() - 4);
				// Convert to lowercase for comparison
				for (char& c : ext) c = tolower(c);

				if (ext == ".m4a" || ext == ".mp3" || ext == ".wav" ||
				    ext == ".ogg" || ext == "flac") {
					files.push_back(path + "/" + name);
				}
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
	// Load model
	Engine engine;
	std::string modelPath = getModelPath();
	if (!engine.loadModel(modelPath)) {
		fprintf(stderr, "Error: Failed to load model from %s\n", modelPath.c_str());
		return 1;
	}

	// Get audio files
	std::vector<std::string> files = getAudioFiles(getTestDataPath());
	if (files.empty()) {
		fprintf(stderr, "Error: No audio files found\n");
		return 1;
	}

	printf("Testing %zu audio file(s)...\n", files.size());
	printf("========================================================================\n");
	printf("%-30s %8s %8s %8s %8s %6s\n",
		"File", "Expected", "Detected", "Error", "Frames", "Pass");
	printf("========================================================================\n");

	std::vector<TestResult> results;
	int passed = 0;
	float totalAbsError = 0;

	for (const auto& file : files) {
		TestResult result = testFile(file, engine);
		results.push_back(result);

		// Extract just filename for display
		std::string displayName = file;
		size_t lastSlash = file.find_last_of("/\\");
		if (lastSlash != std::string::npos) {
			displayName = file.substr(lastSlash + 1);
		}

		printf("%-30s %8.1f %8.1f %+8.2f %8zu %6s\n",
			displayName.c_str(),
			result.expectedBpm,
			result.detectedBpm,
			result.error,
			result.activationFrames,
			result.passed ? "YES" : "NO");

		if (result.passed) passed++;
		totalAbsError += std::fabs(result.error);
	}

	printf("========================================================================\n");

	// Summary
	float avgError = totalAbsError / static_cast<float>(results.size());
	printf("\nSummary:\n");
	printf("  Files tested: %zu\n", results.size());
	printf("  Passed (within 0.5 BPM): %d / %zu (%.1f%%)\n",
		passed, results.size(),
		100.0f * passed / results.size());
	printf("  Average absolute error: %.2f BPM\n", avgError);

	// Show failures
	printf("\nFailures:\n");
	bool anyFailed = false;
	for (const auto& r : results) {
		if (!r.passed && r.detectedBpm > 0) {
			anyFailed = true;
			std::string displayName = r.filename;
			size_t lastSlash = r.filename.find_last_of("/\\");
			if (lastSlash != std::string::npos) {
				displayName = r.filename.substr(lastSlash + 1);
			}
			printf("  %s: expected %.1f, got %.1f (error: %+.2f)\n",
				displayName.c_str(), r.expectedBpm, r.detectedBpm, r.error);
		}
	}
	if (!anyFailed) {
		printf("  None!\n");
	}

	return (passed == static_cast<int>(results.size())) ? 0 : 1;
}
