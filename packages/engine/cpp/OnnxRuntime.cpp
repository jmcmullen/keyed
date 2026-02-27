/**
 * Shared ONNX Runtime Environment
 *
 * Singleton providing a shared ONNX Runtime environment for all models.
 */

#ifdef ONNX_ENABLED

#include "OnnxRuntime.hpp"
#include <onnxruntime_c_api.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "OnnxRuntime"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) printf("[OnnxRuntime] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[OnnxRuntime] " __VA_ARGS__)
#endif

namespace engine {

OnnxRuntime& OnnxRuntime::instance() {
	static OnnxRuntime inst;
	return inst;
}

OnnxRuntime::OnnxRuntime() {
	initialized_ = initialize();
	if (initialized_) {
		LOGI("ONNX Runtime initialized successfully\n");
	} else {
		LOGE("Failed to initialize ONNX Runtime\n");
	}
}

OnnxRuntime::~OnnxRuntime() {
	cleanup();
}

bool OnnxRuntime::initialize() {
	// Get the ONNX Runtime API
	api_ = OrtGetApiBase()->GetApi(ORT_API_VERSION);
	if (!api_) {
		LOGE("Failed to get ONNX Runtime API\n");
		return false;
	}

	OrtStatus* status = nullptr;

	// Create shared environment
	status = api_->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "KeyedEngine", &env_);
	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("CreateEnv failed: %s\n", msg);
		api_->ReleaseStatus(status);
		return false;
	}

	// Create shared CPU memory info
	status = api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo_);
	if (status != nullptr) {
		const char* msg = api_->GetErrorMessage(status);
		LOGE("CreateCpuMemoryInfo failed: %s\n", msg);
		api_->ReleaseStatus(status);
		cleanup();
		return false;
	}

	return true;
}

void OnnxRuntime::cleanup() {
	if (memoryInfo_ && api_) {
		api_->ReleaseMemoryInfo(memoryInfo_);
		memoryInfo_ = nullptr;
	}
	if (env_ && api_) {
		api_->ReleaseEnv(env_);
		env_ = nullptr;
	}
	initialized_ = false;
}

} // namespace engine

#endif // ONNX_ENABLED
