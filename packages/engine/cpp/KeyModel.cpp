/**
 * ONNX Runtime wrapper for MusicalKeyCNN model
 *
 * Implements musical key detection using the MusicalKeyCNN architecture.
 */

#ifdef ONNX_ENABLED

#include "KeyModel.hpp"
#include "OnnxRuntime.hpp"
#include <onnxruntime_c_api.h>
#ifdef ONNX_ENABLE_COREML
#include <coreml_provider_factory.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "KeyModel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) printf("[KeyModel] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[KeyModel] " __VA_ARGS__)
#endif

namespace engine {

// ============================================================================
// Key Mapping Tables
// ============================================================================

// Camelot notation mapping - matches MusicalKeyCNN's CAMELOT_MAPPING from dataset.py
// The model outputs indices in Camelot wheel order, NOT chromatic order!
//
// Index 0-11: Minor keys in Camelot order (1A through 12A)
// Index 12-23: Major keys in Camelot order (1B through 12B)
//
// Camelot wheel:
// Minor (A): 1A=G#m, 2A=Ebm, 3A=Bbm, 4A=Fm, 5A=Cm, 6A=Gm, 7A=Dm, 8A=Am, 9A=Em, 10A=Bm, 11A=F#m, 12A=C#m
// Major (B): 1B=B,   2B=F#,  3B=Db,  4B=Ab, 5B=Eb, 6B=Bb, 7B=F,  8B=C,  9B=G,  10B=D,  11B=A,   12B=E

const char* const KeyModel::CAMELOT_KEYS[NUM_CLASSES] = {
	// Minor keys (index 0-11): Camelot 1A through 12A
	"1A",  // 0: G#m/Abm
	"2A",  // 1: D#m/Ebm
	"3A",  // 2: A#m/Bbm
	"4A",  // 3: Fm
	"5A",  // 4: Cm
	"6A",  // 5: Gm
	"7A",  // 6: Dm
	"8A",  // 7: Am
	"9A",  // 8: Em
	"10A", // 9: Bm
	"11A", // 10: F#m/Gbm
	"12A", // 11: C#m/Dbm
	// Major keys (index 12-23): Camelot 1B through 12B
	"1B",  // 12: B
	"2B",  // 13: F#/Gb
	"3B",  // 14: C#/Db
	"4B",  // 15: G#/Ab
	"5B",  // 16: D#/Eb
	"6B",  // 17: A#/Bb
	"7B",  // 18: F
	"8B",  // 19: C
	"9B",  // 20: G
	"10B", // 21: D
	"11B", // 22: A
	"12B", // 23: E
};

const char* const KeyModel::NOTATION_KEYS[NUM_CLASSES] = {
	// Minor keys (index 0-11): Camelot order
	"G#m",  // 0: 1A
	"Ebm",  // 1: 2A
	"Bbm",  // 2: 3A
	"Fm",   // 3: 4A
	"Cm",   // 4: 5A
	"Gm",   // 5: 6A
	"Dm",   // 6: 7A
	"Am",   // 7: 8A
	"Em",   // 8: 9A
	"Bm",   // 9: 10A
	"F#m",  // 10: 11A
	"C#m",  // 11: 12A
	// Major keys (index 12-23): Camelot order
	"B",    // 12: 1B
	"F#",   // 13: 2B
	"Db",   // 14: 3B
	"Ab",   // 15: 4B
	"Eb",   // 16: 5B
	"Bb",   // 17: 6B
	"F",    // 18: 7B
	"C",    // 19: 8B
	"G",    // 20: 9B
	"D",    // 21: 10B
	"A",    // 22: 11B
	"E",    // 23: 12B
};

// ============================================================================
// KeyModel Implementation
// ============================================================================

KeyModel::KeyModel() {
	inputNames_ = {"input"};
	outputNames_ = {"output"};
}

KeyModel::~KeyModel() {
	cleanup();
}

void KeyModel::cleanup() {
	auto& runtime = OnnxRuntime::instance();
	const OrtApi* api = runtime.api();

	if (session_ && api) {
		api->ReleaseSession(session_);
		session_ = nullptr;
	}
	if (sessionOptions_ && api) {
		api->ReleaseSessionOptions(sessionOptions_);
		sessionOptions_ = nullptr;
	}
	isLoaded_ = false;
}

bool KeyModel::load(const std::string& modelPath) {
	cleanup();

	auto& runtime = OnnxRuntime::instance();
	if (!runtime.isInitialized()) {
		LOGE("ONNX Runtime not initialized\n");
		return false;
	}

	api_ = runtime.api();
	OrtStatus* status = nullptr;

	// Create session options
	status = api_->CreateSessionOptions(&sessionOptions_);
	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("CreateSessionOptions failed: %s\n", msg);
		api_->ReleaseStatus(status);
		return false;
	}

	(void)api_->SetSessionGraphOptimizationLevel(sessionOptions_, ORT_ENABLE_ALL);

	// Enable CoreML on iOS/macOS for GPU acceleration
	// NOTE: We use MLProgram format (0x010) and CPU+GPU (0x020) but NOT the
	// COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES (0x008) flag, which means
	// dynamic input shapes are allowed (needed for variable-length key detection)
#ifdef ONNX_ENABLE_COREML
	// COREML_FLAG_CREATE_MLPROGRAM (0x010) - Use ML Program format (iOS 15+, better compatibility)
	// COREML_FLAG_USE_CPU_AND_GPU (0x020) - Use CPU and GPU compute units
	// NOT setting COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES (0x008) allows dynamic shapes
	uint32_t coreml_flags = COREML_FLAG_CREATE_MLPROGRAM | COREML_FLAG_USE_CPU_AND_GPU;
	status = OrtSessionOptionsAppendExecutionProvider_CoreML(sessionOptions_, coreml_flags);
	if (status != nullptr) {
		// CoreML not available, fall back to CPU
		LOGI("CoreML not available, using CPU\n");
		api_->ReleaseStatus(status);
		status = nullptr;
	} else {
		LOGI("CoreML enabled (MLProgram, CPU+GPU, dynamic shapes allowed)\n");
	}
#else
	LOGI("Using CPU execution\n");
#endif

	// Create session from model file
	status = api_->CreateSession(runtime.env(), modelPath.c_str(), sessionOptions_, &session_);
	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("CreateSession failed: %s\n", msg);
		api_->ReleaseStatus(status);
		cleanup();
		return false;
	}

	isLoaded_ = true;
	LOGI("MusicalKeyCNN model loaded: %s\n", modelPath.c_str());
	return true;
}

bool KeyModel::isReady() const {
	return isLoaded_ && session_ != nullptr;
}

void KeyModel::softmax(float* logits, int size) {
	// Find max for numerical stability
	float maxVal = logits[0];
	for (int i = 1; i < size; i++) {
		maxVal = std::max(maxVal, logits[i]);
	}

	// Compute exp and sum
	float sum = 0.0f;
	for (int i = 0; i < size; i++) {
		logits[i] = std::exp(logits[i] - maxVal);
		sum += logits[i];
	}

	// Normalize
	for (int i = 0; i < size; i++) {
		logits[i] /= sum;
	}
}

bool KeyModel::infer(const float* cqtSpectrogram, KeyOutput& output, float* probabilities) {
	if (!isReady()) {
		LOGE("Model not ready\n");
		return false;
	}

	auto& runtime = OnnxRuntime::instance();
	OrtStatus* status = nullptr;

	// Input shape: [batch=1, channel=1, freq=105, time=100]
	const int64_t inputShape[] = {1, 1, INPUT_FREQ_BINS, INPUT_TIME_FRAMES};
	const size_t inputSize = INPUT_SIZE * sizeof(float);

	// Create input tensor
	OrtValue* inputTensor = nullptr;
	status = api_->CreateTensorWithDataAsOrtValue(
		runtime.memoryInfo(),
		const_cast<float*>(cqtSpectrogram),
		inputSize,
		inputShape,
		4,
		ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
		&inputTensor
	);

	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("CreateTensorWithDataAsOrtValue failed: %s\n", msg);
		api_->ReleaseStatus(status);
		return false;
	}

	// Run inference
	OrtValue* outputs[1] = {nullptr};
	const OrtValue* inputs[1] = {inputTensor};

	status = api_->Run(
		session_,
		nullptr,  // Run options
		inputNames_.data(),
		inputs,
		1,
		outputNames_.data(),
		1,
		outputs
	);

	// Release input tensor
	api_->ReleaseValue(inputTensor);

	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("Run failed: %s\n", msg);
		api_->ReleaseStatus(status);
		return false;
	}

	// Get output data
	float* outputData = nullptr;
	status = api_->GetTensorMutableData(outputs[0], (void**)&outputData);
	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("GetTensorMutableData failed: %s\n", msg);
		api_->ReleaseStatus(status);
		api_->ReleaseValue(outputs[0]);
		return false;
	}

	// Copy output logits
	float logits[NUM_CLASSES];
	std::memcpy(logits, outputData, NUM_CLASSES * sizeof(float));

	// Release output tensor
	api_->ReleaseValue(outputs[0]);

	// Apply softmax to get probabilities
	softmax(logits, NUM_CLASSES);

	// Find predicted class (argmax)
	int predictedClass = 0;
	float maxProb = logits[0];
	for (int i = 1; i < NUM_CLASSES; i++) {
		if (logits[i] > maxProb) {
			maxProb = logits[i];
			predictedClass = i;
		}
	}

	// Fill output
	output.keyIndex = predictedClass;
	output.confidence = maxProb;
	output.camelot = CAMELOT_KEYS[predictedClass];
	output.notation = NOTATION_KEYS[predictedClass];

	// Copy all probabilities if requested (for voting/averaging)
	if (probabilities != nullptr) {
		std::memcpy(probabilities, logits, NUM_CLASSES * sizeof(float));
	}

	return true;
}

bool KeyModel::inferVariable(const float* cqtSpectrogram, int numFrames, KeyOutput& output) {
	if (!isReady()) {
		LOGE("Model not ready\n");
		return false;
	}

	if (numFrames < 1) {
		LOGE("Need at least 1 frame\n");
		return false;
	}

	auto& runtime = OnnxRuntime::instance();
	OrtStatus* status = nullptr;

	// Input arrives in row-major [time][freq] from Engine.
	// Transpose to [freq][time] for ONNX input tensor shape [1, 1, freq, time].
	std::vector<float> transposed(INPUT_FREQ_BINS * numFrames);
	for (int t = 0; t < numFrames; t++) {
		for (int f = 0; f < INPUT_FREQ_BINS; f++) {
			transposed[f * numFrames + t] = cqtSpectrogram[t * INPUT_FREQ_BINS + f];
		}
	}

	// Input shape: [batch=1, channel=1, freq=105, time=numFrames]
	const int64_t inputShape[] = {1, 1, INPUT_FREQ_BINS, static_cast<int64_t>(numFrames)};
	const size_t inputSize = INPUT_FREQ_BINS * numFrames * sizeof(float);

	OrtValue* inputTensor = nullptr;
	status = api_->CreateTensorWithDataAsOrtValue(
		runtime.memoryInfo(),
		transposed.data(),
		inputSize,
		inputShape,
		4,
		ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
		&inputTensor
	);

	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("CreateTensorWithDataAsOrtValue failed: %s\n", msg);
		api_->ReleaseStatus(status);
		return false;
	}

	// Run inference
	OrtValue* outputs[1] = {nullptr};
	const OrtValue* inputs[1] = {inputTensor};

	status = api_->Run(
		session_,
		nullptr,
		inputNames_.data(),
		inputs,
		1,
		outputNames_.data(),
		1,
		outputs
	);

	api_->ReleaseValue(inputTensor);

	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("Run failed: %s\n", msg);
		api_->ReleaseStatus(status);
		return false;
	}

	// Get output data
	float* outputData = nullptr;
	status = api_->GetTensorMutableData(outputs[0], (void**)&outputData);
	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("GetTensorMutableData failed: %s\n", msg);
		api_->ReleaseStatus(status);
		api_->ReleaseValue(outputs[0]);
		return false;
	}

	// Copy and process output
	float logits[NUM_CLASSES];
	std::memcpy(logits, outputData, NUM_CLASSES * sizeof(float));
	api_->ReleaseValue(outputs[0]);

	softmax(logits, NUM_CLASSES);

	// Find predicted class
	int predictedClass = 0;
	float maxProb = logits[0];
	for (int i = 1; i < NUM_CLASSES; i++) {
		if (logits[i] > maxProb) {
			maxProb = logits[i];
			predictedClass = i;
		}
	}

	output.keyIndex = predictedClass;
	output.confidence = maxProb;
	output.camelot = CAMELOT_KEYS[predictedClass];
	output.notation = NOTATION_KEYS[predictedClass];

	return true;
}

} // namespace engine

#endif // ONNX_ENABLED
