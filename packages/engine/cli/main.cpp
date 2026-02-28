/**
 * Engine CLI - Real-time BPM and Key detection from microphone
 *
 * Usage: ./beatnet_cli [-d <device>] [-l]
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "Engine.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <signal.h>
#include <thread>
#include <vector>

using namespace engine;

// Model paths (relative to cli/build directory)
constexpr const char* BPM_MODEL_PATH = "../../models/beatnet.onnx";
constexpr const char* KEY_MODEL_PATH = "../../models/keynet.onnx";

// Audio settings - use 44100 Hz for key detection (resampled internally for BPM)
constexpr int SAMPLE_RATE = 44100;
constexpr int CHANNELS = 1;
constexpr int BUFFER_SIZE = 882;  // 20ms at 44100Hz

// Global state
std::atomic<bool> g_running{true};
std::mutex g_mutex;
Engine g_engine;

// Stats
std::atomic<int> g_frameCount{0};
std::chrono::steady_clock::time_point g_startTime;

void signalHandler(int) {
	g_running = false;
}

void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
	(void)device;
	(void)output;

	const float* samples = static_cast<const float*>(input);

	// Process through engine
	static constexpr int MAX_RESULTS = 8;
	Engine::FrameResult results[MAX_RESULTS];

	// Use try_lock to avoid blocking the real-time audio thread
	// Skip this frame if mutex is busy rather than risk priority inversion
	if (g_mutex.try_lock()) {
		int numResults = g_engine.processAudio(samples, static_cast<int>(frameCount), results, MAX_RESULTS);
		g_frameCount += numResults;
		g_mutex.unlock();
	}
}

void printUsage(const char* progName) {
	printf("Usage: %s [-d <device>] [-l]\n", progName);
	printf("\n");
	printf("Options:\n");
	printf("  -l          List available audio input devices\n");
	printf("  -d <index>  Use device at index (from -l output)\n");
	printf("\n");
	printf("Real-time BPM and Key detection from audio input.\n");
	printf("BPM: BeatNet neural network (Ballroom-trained)\n");
	printf("Key: MusicalKeyCNN with CQT spectrogram analysis\n");
	printf("Press Ctrl+C to stop.\n");
}

void listDevices() {
	ma_context context;
	if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to initialize audio context\n");
		return;
	}

	ma_device_info* captureDevices;
	ma_uint32 captureDeviceCount;
	ma_device_info* playbackDevices;
	ma_uint32 playbackDeviceCount;

	if (ma_context_get_devices(&context, &playbackDevices, &playbackDeviceCount, &captureDevices, &captureDeviceCount) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to enumerate devices\n");
		ma_context_uninit(&context);
		return;
	}

	printf("Available audio input devices:\n");
	printf("==============================\n");
	for (ma_uint32 i = 0; i < captureDeviceCount; i++) {
		printf("  [%u] %s%s\n", i, captureDevices[i].name,
			captureDevices[i].isDefault ? " (default)" : "");
	}
	printf("\n");

	ma_context_uninit(&context);
}

void printStatus() {
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_startTime).count();

	// Get BPM and Key (hold mutex briefly to read engine state)
	float bpm;
	size_t bpmFrames;
	Engine::KeyResult keyResult;
	size_t keyFrames;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		bpm = g_engine.getBpm();
		bpmFrames = g_engine.getFrameCount();
		keyResult = g_engine.getKey();
		keyFrames = g_engine.getKeyFrameCount();
	}

	// Build status line
	char bpmStr[32];
	if (bpm > 0) {
		snprintf(bpmStr, sizeof(bpmStr), "%5.0f", bpm);
	} else {
		snprintf(bpmStr, sizeof(bpmStr), "  ...");
	}

	char keyStr[32];
	if (keyResult.valid) {
		snprintf(keyStr, sizeof(keyStr), "%s (%s) %2.0f%%",
			keyResult.notation.c_str(), keyResult.camelot.c_str(),
			keyResult.confidence * 100.0f);
	} else {
		snprintf(keyStr, sizeof(keyStr), "... (%zu/100 frames)", keyFrames);
	}

	printf("\r[%02lld:%02lld] BPM: %s | Key: %-20s   ",
		(long long)(elapsed / 60), (long long)(elapsed % 60),
		bpmStr, keyStr);
	fflush(stdout);
}

int main(int argc, char* argv[]) {
	int deviceIndex = -1;  // -1 means use default
	bool listOnly = false;

	// Parse arguments
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
			listOnly = true;
		} else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0) {
			if (i + 1 < argc) {
				const char* arg = argv[++i];
				char* endptr;
				errno = 0;
				long val = strtol(arg, &endptr, 10);

				if (errno == ERANGE || val < 0 || val > INT_MAX) {
					fprintf(stderr, "Error: device index '%s' out of range\n", arg);
					return 1;
				}
				if (endptr == arg || *endptr != '\0') {
					fprintf(stderr, "Error: invalid device index '%s'\n", arg);
					return 1;
				}
				deviceIndex = static_cast<int>(val);
			} else {
				fprintf(stderr, "Error: -d requires a device index\n");
				return 1;
			}
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printUsage(argv[0]);
			return 0;
		}
	}

	// List devices mode
	if (listOnly) {
		listDevices();
		return 0;
	}

	// Load BPM model
	printf("Loading BPM model: %s\n", BPM_MODEL_PATH);
	if (!g_engine.loadModel(BPM_MODEL_PATH)) {
		fprintf(stderr, "Error: Failed to load BPM model\n");
		return 1;
	}
	printf("BPM model loaded\n");

	// Load Key model
	printf("Loading Key model: %s\n", KEY_MODEL_PATH);
	if (!g_engine.loadKeyModel(KEY_MODEL_PATH)) {
		fprintf(stderr, "Error: Failed to load Key model\n");
		return 1;
	}
	printf("Key model loaded\n\n");

	// Setup signal handler
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	// Initialize audio context for device enumeration
	ma_context context;
	if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to initialize audio context\n");
		return 1;
	}

	// Get device list if specific device requested
	ma_device_id* deviceId = nullptr;
	ma_device_id selectedDeviceId;

	if (deviceIndex >= 0) {
		ma_device_info* captureDevices;
		ma_uint32 captureDeviceCount;
		ma_device_info* playbackDevices;
		ma_uint32 playbackDeviceCount;

		if (ma_context_get_devices(&context, &playbackDevices, &playbackDeviceCount, &captureDevices, &captureDeviceCount) != MA_SUCCESS) {
			fprintf(stderr, "Error: Failed to enumerate devices\n");
			ma_context_uninit(&context);
			return 1;
		}

		if (captureDeviceCount == 0) {
			fprintf(stderr, "Error: No capture devices available\n");
			ma_context_uninit(&context);
			return 1;
		}

		if ((ma_uint32)deviceIndex >= captureDeviceCount) {
			fprintf(stderr, "Error: Device index %d out of range (0-%u)\n", deviceIndex, captureDeviceCount - 1);
			ma_context_uninit(&context);
			return 1;
		}

		selectedDeviceId = captureDevices[deviceIndex].id;
		deviceId = &selectedDeviceId;
		printf("Selected device: [%d] %s\n\n", deviceIndex, captureDevices[deviceIndex].name);
	}

	// Initialize miniaudio device
	ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
	deviceConfig.capture.pDeviceID = deviceId;
	deviceConfig.capture.format = ma_format_f32;
	deviceConfig.capture.channels = CHANNELS;
	deviceConfig.sampleRate = SAMPLE_RATE;
	deviceConfig.periodSizeInFrames = BUFFER_SIZE;
	deviceConfig.dataCallback = audioCallback;

	ma_device device;
	if (ma_device_init(&context, &deviceConfig, &device) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to initialize audio device\n");
		ma_context_uninit(&context);
		return 1;
	}

	printf("Audio device: %s\n", device.capture.name);
	printf("Sample rate: %d Hz\n", device.sampleRate);
	printf("Buffer size: %d frames\n\n", BUFFER_SIZE);
	printf("Listening... (Ctrl+C to stop)\n");
	printf("=============================\n\n");

	// Start recording
	if (ma_device_start(&device) != MA_SUCCESS) {
		fprintf(stderr, "Error: Failed to start audio device\n");
		ma_device_uninit(&device);
		ma_context_uninit(&context);
		return 1;
	}

	g_startTime = std::chrono::steady_clock::now();

	// Main loop - just print status periodically
	while (g_running) {
		printStatus();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	printf("\n\nStopping...\n");

	// Cleanup
	ma_device_uninit(&device);
	ma_context_uninit(&context);

	// Print summary
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - g_startTime).count();

	float bpm = g_engine.getBpm();
	size_t bpmFrames = g_engine.getFrameCount();
	auto keyResult = g_engine.getKey();
	size_t keyFrames = g_engine.getKeyFrameCount();

	printf("\n");
	printf("Session Summary\n");
	printf("===============\n");
	printf("Duration: %lld:%02lld\n", (long long)(elapsed / 60), (long long)(elapsed % 60));
	printf("\n");

	printf("BPM Detection:\n");
	printf("  Frames: %zu\n", bpmFrames);
	if (bpm > 0) {
		printf("  Result: %.0f BPM\n", bpm);
	} else {
		printf("  Result: Not enough data (need ~2 seconds)\n");
	}
	printf("\n");

	printf("Key Detection:\n");
	printf("  CQT Frames: %zu / 100\n", keyFrames);
	if (keyResult.valid) {
		printf("  Result: %s (%s)\n", keyResult.notation.c_str(), keyResult.camelot.c_str());
		printf("  Confidence: %.0f%%\n", keyResult.confidence * 100.0f);
	} else {
		printf("  Result: Not enough data (need ~20 seconds)\n");
	}

	return 0;
}
