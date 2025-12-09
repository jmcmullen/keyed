/**
 * Beat Latency Test - Measure detection latency for beatgrid alignment
 *
 * This test:
 * 1. Loads test audio files with known BPM
 * 2. Processes them through the engine frame-by-frame
 * 3. Detects beat activation peaks
 * 4. Compares peak positions to expected beat positions
 * 5. Calculates the average latency offset
 *
 * The output tells us exactly how many frames/ms to compensate in the UI.
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
#include <numeric>

using namespace engine;

// Constants
constexpr int TARGET_SAMPLE_RATE = 22050;
constexpr int HOP_LENGTH = 441;  // 20ms per frame at 22050Hz
constexpr float FRAME_DURATION_MS = 1000.0f * HOP_LENGTH / TARGET_SAMPLE_RATE;  // 20ms
constexpr float PEAK_THRESHOLD = 0.4f;  // Minimum activation to consider a beat
constexpr float PEAK_MIN_DISTANCE_MS = 200.0f;  // Minimum 200ms between peaks (~300 BPM max)

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

std::string getModelPath() {
	return getExecutableDir() + "/../../models/beatnet.onnx";
}

std::string getTestDataPath() {
	return getExecutableDir() + "/../../test-data";
}

float extractExpectedBpm(const std::string& filename) {
	size_t lastSlash = filename.find_last_of("/\\");
	std::string basename = (lastSlash != std::string::npos)
		? filename.substr(lastSlash + 1)
		: filename;

	size_t lastDot = basename.find_last_of('.');
	if (lastDot != std::string::npos) {
		basename = basename.substr(0, lastDot);
	}

	size_t underscore = basename.find('_');
	if (underscore != std::string::npos) {
		basename = basename.substr(0, underscore);
	}

	return static_cast<float>(atof(basename.c_str()));
}

struct BeatPeak {
	int frameIndex;
	float timeMs;
	float activation;
};

struct LatencyResult {
	std::string filename;
	float expectedBpm;
	float detectedBpm;
	int numPeaksDetected;
	int numPeaksMatched;
	float avgLatencyMs;
	float stdDevMs;
	float minLatencyMs;
	float maxLatencyMs;
	std::vector<float> latencies;  // Individual latency measurements
};

// Find local maxima in activation signal
std::vector<BeatPeak> findBeatPeaks(const std::vector<float>& activations) {
	std::vector<BeatPeak> peaks;

	const int minDistanceFrames = static_cast<int>(PEAK_MIN_DISTANCE_MS / FRAME_DURATION_MS);
	int lastPeakFrame = -minDistanceFrames;

	for (size_t i = 1; i < activations.size() - 1; i++) {
		float prev = activations[i - 1];
		float curr = activations[i];
		float next = activations[i + 1];

		// Local maximum above threshold
		if (curr > prev && curr > next && curr >= PEAK_THRESHOLD) {
			int framesSinceLastPeak = static_cast<int>(i) - lastPeakFrame;

			if (framesSinceLastPeak >= minDistanceFrames) {
				BeatPeak peak;
				peak.frameIndex = static_cast<int>(i);
				peak.timeMs = i * FRAME_DURATION_MS;
				peak.activation = curr;
				peaks.push_back(peak);
				lastPeakFrame = static_cast<int>(i);
			}
		}
	}

	return peaks;
}

// Generate expected beat times from BPM
std::vector<float> generateExpectedBeats(float bpm, float durationMs, float startOffsetMs = 0) {
	std::vector<float> beats;
	float beatIntervalMs = 60000.0f / bpm;

	for (float t = startOffsetMs; t < durationMs; t += beatIntervalMs) {
		beats.push_back(t);
	}

	return beats;
}

// Find the best phase alignment between detected and expected beats
float findBestPhaseOffset(const std::vector<BeatPeak>& detected,
                          const std::vector<float>& expected,
                          float beatIntervalMs) {
	if (detected.empty() || expected.empty()) return 0;

	// Try different phase offsets and find the one with minimum total error
	float bestOffset = 0;
	float bestError = std::numeric_limits<float>::max();

	// Search in 1ms increments over one beat interval
	for (float offset = 0; offset < beatIntervalMs; offset += 1.0f) {
		float totalError = 0;
		int matches = 0;

		for (const auto& peak : detected) {
			float adjustedTime = peak.timeMs;

			// Find nearest expected beat with this offset
			float minDist = std::numeric_limits<float>::max();
			for (float expectedTime : expected) {
				float dist = std::fabs(adjustedTime - (expectedTime + offset));
				if (dist < minDist) {
					minDist = dist;
				}
			}

			if (minDist < beatIntervalMs / 2) {
				totalError += minDist;
				matches++;
			}
		}

		if (matches > 0) {
			float avgError = totalError / matches;
			if (avgError < bestError) {
				bestError = avgError;
				bestOffset = offset;
			}
		}
	}

	return bestOffset;
}

LatencyResult measureLatency(const std::string& filepath, Engine& engine) {
	LatencyResult result;
	result.filename = filepath;
	result.expectedBpm = extractExpectedBpm(filepath);
	result.detectedBpm = 0;
	result.numPeaksDetected = 0;
	result.numPeaksMatched = 0;
	result.avgLatencyMs = 0;
	result.stdDevMs = 0;
	result.minLatencyMs = 0;
	result.maxLatencyMs = 0;

	// Initialize decoder
	ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 1, TARGET_SAMPLE_RATE);
	ma_decoder decoder;

	if (ma_decoder_init_file(filepath.c_str(), &decoderConfig, &decoder) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to open %s\n", filepath.c_str());
		return result;
	}

	// Reset engine
	engine.reset();

	// Collect all activations
	std::vector<float> beatActivations;
	std::vector<float> downbeatActivations;

	constexpr int CHUNK_SIZE = 441;
	float buffer[CHUNK_SIZE];
	static constexpr int MAX_RESULTS = 8;
	Engine::FrameResult results[MAX_RESULTS];

	ma_uint64 framesRead;

	while (ma_decoder_read_pcm_frames(&decoder, buffer, CHUNK_SIZE, &framesRead) == MA_SUCCESS && framesRead > 0) {
		int numResults = engine.processAudio(buffer, static_cast<int>(framesRead), results, MAX_RESULTS);

		for (int i = 0; i < numResults; i++) {
			beatActivations.push_back(results[i].beatActivation);
			downbeatActivations.push_back(results[i].downbeatActivation);
		}
	}

	ma_decoder_uninit(&decoder);

	// Get detected BPM
	result.detectedBpm = engine.getBpm();

	if (beatActivations.empty() || result.detectedBpm <= 0) {
		return result;
	}

	// Find beat peaks
	std::vector<BeatPeak> peaks = findBeatPeaks(beatActivations);
	result.numPeaksDetected = static_cast<int>(peaks.size());

	if (peaks.empty()) {
		return result;
	}

	// Generate expected beat positions using detected BPM
	float durationMs = beatActivations.size() * FRAME_DURATION_MS;
	float beatIntervalMs = 60000.0f / result.detectedBpm;

	// Find best phase alignment
	std::vector<float> expectedBeats = generateExpectedBeats(result.detectedBpm, durationMs, 0);
	float phaseOffset = findBestPhaseOffset(peaks, expectedBeats, beatIntervalMs);

	// Regenerate expected beats with phase offset
	expectedBeats = generateExpectedBeats(result.detectedBpm, durationMs, phaseOffset);

	// Calculate latency for each detected peak
	for (const auto& peak : peaks) {
		// Find nearest expected beat
		float minLatency = std::numeric_limits<float>::max();

		for (float expectedTime : expectedBeats) {
			float latency = peak.timeMs - expectedTime;

			// Only consider if within half a beat interval (could be matching wrong beat otherwise)
			if (std::fabs(latency) < beatIntervalMs / 2) {
				if (std::fabs(latency) < std::fabs(minLatency)) {
					minLatency = latency;
				}
			}
		}

		if (minLatency != std::numeric_limits<float>::max()) {
			result.latencies.push_back(minLatency);
		}
	}

	result.numPeaksMatched = static_cast<int>(result.latencies.size());

	// Calculate statistics
	if (!result.latencies.empty()) {
		float sum = std::accumulate(result.latencies.begin(), result.latencies.end(), 0.0f);
		result.avgLatencyMs = sum / result.latencies.size();

		result.minLatencyMs = *std::min_element(result.latencies.begin(), result.latencies.end());
		result.maxLatencyMs = *std::max_element(result.latencies.begin(), result.latencies.end());

		// Standard deviation
		float sqSum = 0;
		for (float lat : result.latencies) {
			sqSum += (lat - result.avgLatencyMs) * (lat - result.avgLatencyMs);
		}
		result.stdDevMs = std::sqrt(sqSum / result.latencies.size());
	}

	return result;
}

std::vector<std::string> getAudioFiles(const std::string& path) {
	std::vector<std::string> files;

	DIR* dir = opendir(path.c_str());
	if (dir) {
		struct dirent* entry;
		while ((entry = readdir(dir)) != nullptr) {
			std::string name = entry->d_name;
			if (name.size() > 4) {
				std::string ext = name.substr(name.size() - 4);
				for (char& c : ext) c = tolower(c);

				if (ext == ".wav") {
					files.push_back(path + "/" + name);
				}
			}
		}
		closedir(dir);

		std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
			return extractExpectedBpm(a) < extractExpectedBpm(b);
		});
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

	// Warm up
	engine.warmUp();

	// Get audio files
	std::vector<std::string> files = getAudioFiles(getTestDataPath());
	if (files.empty()) {
		fprintf(stderr, "Error: No audio files found in %s\n", getTestDataPath().c_str());
		return 1;
	}

	printf("\n");
	printf("=============================================================================\n");
	printf("                    BEAT DETECTION LATENCY ANALYSIS\n");
	printf("=============================================================================\n");
	printf("\n");
	printf("Frame duration: %.1f ms (hop_length=%d @ %d Hz)\n", FRAME_DURATION_MS, HOP_LENGTH, TARGET_SAMPLE_RATE);
	printf("Peak threshold: %.2f\n", PEAK_THRESHOLD);
	printf("Testing %zu files...\n\n", files.size());

	printf("%-20s %6s %6s %6s %8s %8s %8s %8s\n",
		"File", "BPM", "Peaks", "Match", "Avg(ms)", "Std(ms)", "Min(ms)", "Max(ms)");
	printf("-----------------------------------------------------------------------------\n");

	std::vector<LatencyResult> results;
	std::vector<float> allLatencies;

	for (const auto& file : files) {
		LatencyResult result = measureLatency(file, engine);
		results.push_back(result);

		// Collect all latencies for global stats
		allLatencies.insert(allLatencies.end(),
			result.latencies.begin(), result.latencies.end());

		// Display filename
		std::string displayName = file;
		size_t lastSlash = file.find_last_of("/\\");
		if (lastSlash != std::string::npos) {
			displayName = file.substr(lastSlash + 1);
		}
		if (displayName.size() > 20) {
			displayName = displayName.substr(0, 17) + "...";
		}

		printf("%-20s %6.1f %6d %6d %+8.1f %8.1f %+8.1f %+8.1f\n",
			displayName.c_str(),
			result.detectedBpm,
			result.numPeaksDetected,
			result.numPeaksMatched,
			result.avgLatencyMs,
			result.stdDevMs,
			result.minLatencyMs,
			result.maxLatencyMs);
	}

	printf("-----------------------------------------------------------------------------\n");

	// Global statistics
	if (!allLatencies.empty()) {
		float globalSum = std::accumulate(allLatencies.begin(), allLatencies.end(), 0.0f);
		float globalAvg = globalSum / allLatencies.size();

		float globalMin = *std::min_element(allLatencies.begin(), allLatencies.end());
		float globalMax = *std::max_element(allLatencies.begin(), allLatencies.end());

		float sqSum = 0;
		for (float lat : allLatencies) {
			sqSum += (lat - globalAvg) * (lat - globalAvg);
		}
		float globalStd = std::sqrt(sqSum / allLatencies.size());

		// Calculate frames of latency
		float framesLatency = globalAvg / FRAME_DURATION_MS;

		printf("\n");
		printf("=============================================================================\n");
		printf("                           GLOBAL RESULTS\n");
		printf("=============================================================================\n");
		printf("\n");
		printf("  Total beat peaks analyzed:  %zu\n", allLatencies.size());
		printf("\n");
		printf("  AVERAGE LATENCY:            %+.1f ms\n", globalAvg);
		printf("  Standard deviation:         %.1f ms\n", globalStd);
		printf("  Range:                      %+.1f to %+.1f ms\n", globalMin, globalMax);
		printf("\n");
		printf("  Latency in frames:          %.1f frames (@ 50 FPS)\n", framesLatency);
		printf("\n");
		printf("=============================================================================\n");
		printf("                        RECOMMENDED COMPENSATION\n");
		printf("=============================================================================\n");
		printf("\n");

		int compensationFrames = static_cast<int>(std::round(globalAvg / FRAME_DURATION_MS));

		printf("  Set LATENCY_COMPENSATION_FRAMES = %d\n", compensationFrames);
		printf("\n");
		printf("  This means: when a beat is detected, place the grid line\n");
		printf("              %d frames EARLIER in the history buffer.\n", compensationFrames);
		printf("\n");

		// Histogram
		printf("  Latency distribution:\n");
		int buckets[11] = {0};  // -100 to +100 in 20ms buckets
		for (float lat : allLatencies) {
			int bucket = static_cast<int>((lat + 100) / 20);
			bucket = std::max(0, std::min(10, bucket));
			buckets[bucket]++;
		}

		int maxBucket = *std::max_element(buckets, buckets + 11);
		for (int i = 0; i <= 10; i++) {
			int rangeStart = -100 + i * 20;
			int rangeEnd = rangeStart + 20;
			int barLen = (buckets[i] * 40) / std::max(1, maxBucket);

			printf("  %+4d to %+4d ms: ", rangeStart, rangeEnd);
			for (int j = 0; j < barLen; j++) printf("#");
			printf(" (%d)\n", buckets[i]);
		}
		printf("\n");
	}

	return 0;
}
