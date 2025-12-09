/**
 * BeatNet CLI - Real-time beat detection from microphone
 *
 * Usage: ./beatnet_cli [-d <device>] [-l]
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "Engine.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <signal.h>
#include <thread>
#include <vector>

using namespace engine;

// Model path (Ballroom-trained, best for dance music)
// Relative to cli/build directory
constexpr const char* MODEL_PATH = "../../models/beatnet.onnx";

// Audio settings
constexpr int SAMPLE_RATE = 22050;
constexpr int CHANNELS = 1;
constexpr int BUFFER_SIZE = 441;  // 20ms at 22050Hz (matches hop length)

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
	printf("Real-time beat detection from audio input.\n");
	printf("Uses BeatNet Model 2 (Ballroom-trained, best for dance music).\n");
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

	// Get BPM (hold mutex briefly to read engine state)
	float bpm;
	size_t frameCount;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		bpm = g_engine.getBpm();
		frameCount = g_engine.getFrameCount();
	}

	if (bpm > 0) {
		printf("\r[%02lld:%02lld] BPM: %5.0f | frames: %4zu   ",
			(long long)(elapsed / 60), (long long)(elapsed % 60),
			bpm,
			frameCount);
	} else {
		// Not enough frames yet
		printf("\r[%02lld:%02lld] BPM: analyzing... (%zu frames)   ",
			(long long)(elapsed / 60), (long long)(elapsed % 60),
			frameCount);
	}
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
				deviceIndex = atoi(argv[++i]);
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

	// Load model
	printf("Loading model: %s\n", MODEL_PATH);
	if (!g_engine.loadModel(MODEL_PATH)) {
		fprintf(stderr, "Error: Failed to load model\n");
		return 1;
	}
	printf("Model loaded successfully\n\n");

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
	size_t frameCount = g_engine.getFrameCount();

	printf("\n");
	printf("Session Summary\n");
	printf("===============\n");
	printf("Duration: %lld:%02lld\n", (long long)(elapsed / 60), (long long)(elapsed % 60));
	printf("Frames processed: %zu\n", frameCount);
	printf("\n");
	if (bpm > 0) {
		printf("Detected BPM: %.0f\n", bpm);
	} else {
		printf("BPM: Not enough data (need ~2 seconds of audio)\n");
	}

	return 0;
}
