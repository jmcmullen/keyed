/**
 * ONNX Runtime wrapper for BeatNet CRNN model
 */

#ifdef ONNX_ENABLED

#include "OnnxModel.hpp"
#include <onnxruntime_c_api.h>
#ifdef ONNX_ENABLE_COREML
#include <coreml_provider_factory.h>
#endif
#include <cmath>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "OnnxModel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) printf("[OnnxModel] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[OnnxModel] " __VA_ARGS__)
#endif

namespace engine {

OnnxModel::OnnxModel() {
    // Initialize LSTM state vectors
    hidden_.resize(NUM_LAYERS * 1 * HIDDEN_DIM, 0.0f);
    cell_.resize(NUM_LAYERS * 1 * HIDDEN_DIM, 0.0f);

    // Set up input/output names (must match ONNX model)
    inputNames_ = {"input", "hidden_in", "cell_in"};
    outputNames_ = {"output", "hidden_out", "cell_out"};
}

OnnxModel::~OnnxModel() {
    cleanup();
}

void OnnxModel::cleanup() {
    if (session_ && api_) {
        api_->ReleaseSession(session_);
        session_ = nullptr;
    }
    if (sessionOptions_ && api_) {
        api_->ReleaseSessionOptions(sessionOptions_);
        sessionOptions_ = nullptr;
    }
    if (memoryInfo_ && api_) {
        api_->ReleaseMemoryInfo(memoryInfo_);
        memoryInfo_ = nullptr;
    }
    if (env_ && api_) {
        api_->ReleaseEnv(env_);
        env_ = nullptr;
    }
    isLoaded_ = false;
}

void OnnxModel::initializeLstmState() {
    std::fill(hidden_.begin(), hidden_.end(), 0.0f);
    std::fill(cell_.begin(), cell_.end(), 0.0f);
}

bool OnnxModel::load(const std::string& modelPath) {
    cleanup();

    // Get the ONNX Runtime API
    api_ = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api_) {
        LOGE("Failed to get ONNX Runtime API\n");
        return false;
    }

    OrtStatus* status = nullptr;

    // Create environment
    status = api_->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "BeatNet", &env_);
    if (status) {
        LOGE("CreateEnv failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        return false;
    }

    // Create session options
    status = api_->CreateSessionOptions(&sessionOptions_);
    if (status) {
        LOGE("CreateSessionOptions failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        cleanup();
        return false;
    }

    (void)api_->SetSessionGraphOptimizationLevel(sessionOptions_, ORT_ENABLE_ALL);

    // Enable hardware acceleration based on platform
#ifdef __ANDROID__
    // Try to enable NNAPI for Android - uses GPU/DSP/NPU when available
    OrtSessionOptionsAppendExecutionProvider_Nnapi(sessionOptions_, 0);
    LOGI("NNAPI execution provider enabled\n");
#elif defined(__APPLE__) && defined(ONNX_ENABLE_COREML)
    // Enable CoreML for iOS/macOS - uses Neural Engine on supported devices
    // Flags:
    //   0x001 = COREML_FLAG_CREATE_MLPROGRAM (use ML Program format, iOS 15+)
    //   0x004 = COREML_FLAG_ONLY_ENABLE_DEVICE_WITH_ANE (require Neural Engine)
    // We use 0x001 for ML Program which is more efficient on modern devices
    uint32_t coremlFlags = 0x001;  // COREML_FLAG_CREATE_MLPROGRAM
    OrtStatus* coremlStatus = OrtSessionOptionsAppendExecutionProvider_CoreML(sessionOptions_, coremlFlags);
    if (coremlStatus) {
        LOGI("CoreML not available: %s. Falling back to CPU\n", api_->GetErrorMessage(coremlStatus));
        api_->ReleaseStatus(coremlStatus);
    } else {
        LOGI("CoreML execution provider enabled (ML Program mode)\n");
    }
#else
    LOGI("Using CPU execution provider\n");
#endif

    // Create memory info
    status = api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo_);
    if (status) {
        LOGE("CreateCpuMemoryInfo failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        cleanup();
        return false;
    }

    // Create session
    status = api_->CreateSession(env_, modelPath.c_str(), sessionOptions_, &session_);
    if (status) {
        LOGE("CreateSession failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        cleanup();
        return false;
    }

    isLoaded_ = true;
    initializeLstmState();

    LOGI("Model loaded successfully from: %s\n", modelPath.c_str());
    return true;
}

bool OnnxModel::isReady() const {
    return isLoaded_ && session_ != nullptr;
}

void OnnxModel::resetState() {
    initializeLstmState();
}

bool OnnxModel::infer(const float* features, ModelOutput& output) {
    if (!isReady()) {
        return false;
    }

    OrtStatus* status = nullptr;

    // Input shapes
    const int64_t inputShape[] = {1, 1, INPUT_DIM};          // [batch, seq, features]
    const int64_t hiddenShape[] = {NUM_LAYERS, 1, HIDDEN_DIM}; // [layers, batch, hidden]

    // Create input tensors
    OrtValue* inputTensor = nullptr;
    OrtValue* hiddenTensor = nullptr;
    OrtValue* cellTensor = nullptr;

    status = api_->CreateTensorWithDataAsOrtValue(
        memoryInfo_,
        const_cast<float*>(features),
        INPUT_DIM * sizeof(float),
        inputShape, 3,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &inputTensor
    );
    if (status) {
        LOGE("CreateTensorWithDataAsOrtValue (input) failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        return false;
    }

    status = api_->CreateTensorWithDataAsOrtValue(
        memoryInfo_,
        hidden_.data(),
        hidden_.size() * sizeof(float),
        hiddenShape, 3,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &hiddenTensor
    );
    if (status) {
        LOGE("CreateTensorWithDataAsOrtValue (hidden) failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        api_->ReleaseValue(inputTensor);
        return false;
    }

    status = api_->CreateTensorWithDataAsOrtValue(
        memoryInfo_,
        cell_.data(),
        cell_.size() * sizeof(float),
        hiddenShape, 3,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &cellTensor
    );
    if (status) {
        LOGE("CreateTensorWithDataAsOrtValue (cell) failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        api_->ReleaseValue(inputTensor);
        api_->ReleaseValue(hiddenTensor);
        return false;
    }

    // Set up inputs array
    OrtValue* inputs[] = {inputTensor, hiddenTensor, cellTensor};

    // Allocate output tensors (will be filled by Run)
    OrtValue* outputs[3] = {nullptr, nullptr, nullptr};

    // Run inference
    status = api_->Run(
        session_,
        nullptr,  // run options
        inputNames_.data(), inputs, 3,
        outputNames_.data(), 3, outputs
    );

    // Clean up input tensors
    api_->ReleaseValue(inputTensor);
    api_->ReleaseValue(hiddenTensor);
    api_->ReleaseValue(cellTensor);

    if (status) {
        LOGE("Run failed: %s\n", api_->GetErrorMessage(status));
        api_->ReleaseStatus(status);
        return false;
    }

    // Extract output probabilities
    float* outputData = nullptr;
    status = api_->GetTensorMutableData(outputs[0], (void**)&outputData);
    if (status || !outputData) {
        if (status) {
            LOGE("GetTensorMutableData (output) failed: %s\n", api_->GetErrorMessage(status));
            api_->ReleaseStatus(status);
        }
        for (int i = 0; i < 3; i++) {
            if (outputs[i]) api_->ReleaseValue(outputs[i]);
        }
        return false;
    }

	float* hiddenOut = nullptr;
	float* cellOut = nullptr;
	OrtStatus* hiddenStatus = api_->GetTensorMutableData(outputs[1], (void**)&hiddenOut);
	OrtStatus* cellStatus = api_->GetTensorMutableData(outputs[2], (void**)&cellOut);

	if (hiddenStatus) {
		LOGE(
			"GetTensorMutableData (hidden_out) failed: %s\n",
			api_->GetErrorMessage(hiddenStatus)
		);
		api_->ReleaseStatus(hiddenStatus);
	}
	if (cellStatus) {
		LOGE(
			"GetTensorMutableData (cell_out) failed: %s\n",
			api_->GetErrorMessage(cellStatus)
		);
		api_->ReleaseStatus(cellStatus);
	}

    if (hiddenOut) {
        std::memcpy(hidden_.data(), hiddenOut, hidden_.size() * sizeof(float));
    }
    if (cellOut) {
        std::memcpy(cell_.data(), cellOut, cell_.size() * sizeof(float));
    }

    // Apply softmax if needed (check if output is already normalized)
    float sum = outputData[0] + outputData[1] + outputData[2];
    float probs[3];

    if (std::abs(sum - 1.0f) > 0.01f) {
        // Not normalized, apply softmax
        float maxVal = std::max({outputData[0], outputData[1], outputData[2]});
        float exp0 = std::exp(outputData[0] - maxVal);
        float exp1 = std::exp(outputData[1] - maxVal);
        float exp2 = std::exp(outputData[2] - maxVal);
        float expSum = exp0 + exp1 + exp2;
        probs[0] = exp0 / expSum;
        probs[1] = exp1 / expSum;
        probs[2] = exp2 / expSum;
    } else {
        probs[0] = outputData[0];
        probs[1] = outputData[1];
        probs[2] = outputData[2];
    }

    // Output order: [beat, downbeat, non-beat]
    output.beatActivation = probs[0];
    output.downbeatActivation = probs[1];

    // Clean up output tensors
    for (int i = 0; i < 3; i++) {
        if (outputs[i]) api_->ReleaseValue(outputs[i]);
    }

    return true;
}

} // namespace engine

#else // !ONNX_ENABLED

// Empty implementation file when ONNX is disabled
namespace engine {
// Stub class is defined in header
}

#endif // ONNX_ENABLED
