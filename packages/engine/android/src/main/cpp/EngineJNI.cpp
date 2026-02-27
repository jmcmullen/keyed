/**
 * JNI bindings for the Engine module
 * Wraps the shared C++ Engine class for Android/Kotlin access
 * Supports both BPM detection (BeatNet) and key detection (MusicalKeyCNN)
 */

#include "Engine.hpp"
#include <jni.h>
#include <android/log.h>
#include <vector>
#include <string>
#include <mutex>

#define LOG_TAG "Engine"
#ifdef NDEBUG
#define LOGI(...)
#else
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#endif
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static engine::Engine* g_engine = nullptr;
static std::vector<engine::Engine::FrameResult> g_resultBuffer;
static std::mutex g_engineMutex;

extern "C" {

// ============================================================================
// Engine Lifecycle
// ============================================================================

JNIEXPORT void JNICALL
Java_services_session_keyed_engine_EngineModule_nativeInit(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if (g_engine == nullptr) {
		g_engine = new engine::Engine();
		g_resultBuffer.resize(200);
		LOGI("Engine initialized");
	}
}

JNIEXPORT void JNICALL
Java_services_session_keyed_engine_EngineModule_nativeReset(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if (g_engine) {
		g_engine->reset();
	}
}

JNIEXPORT void JNICALL
Java_services_session_keyed_engine_EngineModule_nativeDestroy(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if (g_engine) {
		delete g_engine;
		g_engine = nullptr;
	}
}

// ============================================================================
// BPM Detection (BeatNet)
// ============================================================================

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeLoadModel(
	JNIEnv* env, jobject thiz, jstring modelPath) {

	const char* path = env->GetStringUTFChars(modelPath, nullptr);
	std::string pathStr(path);
	env->ReleaseStringUTFChars(modelPath, path);

	std::lock_guard<std::mutex> lock(g_engineMutex);
	if (!g_engine) {
		return JNI_FALSE;
	}

	bool result = g_engine->loadModel(pathStr);
	LOGI("loadModel result: %s", result ? "success" : "failed");
	return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeIsReady(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return (g_engine && g_engine->isReady()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeWarmUp(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if (!g_engine) {
		return JNI_FALSE;
	}
	bool result = g_engine->warmUp();
	LOGI("warmUp result: %s", result ? "success" : "failed");
	return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_services_session_keyed_engine_EngineModule_nativeGetBpm(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return g_engine ? g_engine->getBpm() : 0.0f;
}

JNIEXPORT jlong JNICALL
Java_services_session_keyed_engine_EngineModule_nativeGetFrameCount(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return g_engine ? static_cast<jlong>(g_engine->getFrameCount()) : 0;
}

// ============================================================================
// Key Detection (MusicalKeyCNN)
// ============================================================================

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeLoadKeyModel(
	JNIEnv* env, jobject thiz, jstring modelPath) {

	const char* path = env->GetStringUTFChars(modelPath, nullptr);
	std::string pathStr(path);
	env->ReleaseStringUTFChars(modelPath, path);

	std::lock_guard<std::mutex> lock(g_engineMutex);
	if (!g_engine) {
		return JNI_FALSE;
	}

	bool result = g_engine->loadKeyModel(pathStr);
	LOGI("loadKeyModel result: %s", result ? "success" : "failed");
	return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeIsKeyReady(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return (g_engine && g_engine->isKeyReady()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeWarmUpKey(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if (!g_engine) {
		return JNI_FALSE;
	}
	bool result = g_engine->warmUpKey();
	LOGI("warmUpKey result: %s", result ? "success" : "failed");
	return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_services_session_keyed_engine_EngineModule_nativeGetKey(JNIEnv* env, jobject thiz) {
	engine::Engine::KeyResult keyResult;

	{
		std::lock_guard<std::mutex> lock(g_engineMutex);
		if (!g_engine) {
			return nullptr;
		}
		keyResult = g_engine->getKey();
	}

	if (!keyResult.valid) {
		return nullptr;
	}

	// Find the KeyResult class
	jclass keyResultClass = env->FindClass("services/session/keyed/engine/KeyResult");
	if (keyResultClass == nullptr) {
		LOGE("KeyResult class not found");
		return nullptr;
	}

	// Signature: (camelot:String, notation:String, confidence:F, valid:Z)V
	jmethodID constructor = env->GetMethodID(keyResultClass, "<init>",
		"(Ljava/lang/String;Ljava/lang/String;FZ)V");
	if (constructor == nullptr) {
		LOGE("KeyResult constructor not found");
		return nullptr;
	}

	jstring camelot = env->NewStringUTF(keyResult.camelot.c_str());
	jstring notation = env->NewStringUTF(keyResult.notation.c_str());

	jobject result = env->NewObject(keyResultClass, constructor,
		camelot,
		notation,
		keyResult.confidence,
		keyResult.valid ? JNI_TRUE : JNI_FALSE
	);

	env->DeleteLocalRef(camelot);
	env->DeleteLocalRef(notation);

	return result;
}

JNIEXPORT jlong JNICALL
Java_services_session_keyed_engine_EngineModule_nativeGetKeyFrameCount(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return g_engine ? static_cast<jlong>(g_engine->getKeyFrameCount()) : 0;
}

// ============================================================================
// Audio Processing
// ============================================================================

JNIEXPORT jobjectArray JNICALL
Java_services_session_keyed_engine_EngineModule_nativeProcessAudio(
	JNIEnv* env, jobject thiz, jfloatArray samples) {

	jsize numSamples = env->GetArrayLength(samples);
	jfloat* sampleData = env->GetFloatArrayElements(samples, nullptr);

	int numResults = 0;
	std::vector<engine::Engine::FrameResult> localResults;

	{
		std::lock_guard<std::mutex> lock(g_engineMutex);
		if (!g_engine) {
			env->ReleaseFloatArrayElements(samples, sampleData, 0);
			return nullptr;
		}

		int maxResults = static_cast<int>(g_resultBuffer.size());
		numResults = g_engine->processAudio(sampleData, numSamples,
		                                     g_resultBuffer.data(), maxResults);

		// Copy results while holding lock
		if (numResults > 0) {
			localResults.assign(g_resultBuffer.begin(), g_resultBuffer.begin() + numResults);
		}
	}

	env->ReleaseFloatArrayElements(samples, sampleData, 0);

	if (numResults == 0) {
		return nullptr;
	}

	// Find the FrameResult class
	jclass resultClass = env->FindClass("services/session/keyed/engine/FrameResult");
	if (resultClass == nullptr) {
		LOGE("FrameResult class not found");
		return nullptr;
	}

	// Signature: (beatActivation:F, downbeatActivation:F)V
	jmethodID constructor = env->GetMethodID(resultClass, "<init>", "(FF)V");
	if (constructor == nullptr) {
		LOGE("FrameResult constructor not found");
		return nullptr;
	}

	// Create array of results
	jobjectArray resultArray = env->NewObjectArray(numResults, resultClass, nullptr);

	for (int i = 0; i < numResults; i++) {
		const auto& r = localResults[i];
		jobject result = env->NewObject(resultClass, constructor,
			r.beatActivation,
			r.downbeatActivation
		);
		env->SetObjectArrayElement(resultArray, i, result);
		env->DeleteLocalRef(result);
	}

	return resultArray;
}

// Legacy: Process audio at 22050 Hz for BPM only (no key detection)
JNIEXPORT jobjectArray JNICALL
Java_services_session_keyed_engine_EngineModule_nativeProcessAudioForBpm(
	JNIEnv* env, jobject thiz, jfloatArray samples) {

	jsize numSamples = env->GetArrayLength(samples);
	jfloat* sampleData = env->GetFloatArrayElements(samples, nullptr);

	int numResults = 0;
	std::vector<engine::Engine::FrameResult> localResults;

	{
		std::lock_guard<std::mutex> lock(g_engineMutex);
		if (!g_engine || !g_engine->isReady()) {
			env->ReleaseFloatArrayElements(samples, sampleData, 0);
			return nullptr;
		}

		int maxResults = static_cast<int>(g_resultBuffer.size());
		numResults = g_engine->processAudioForBpm(sampleData, numSamples,
		                                          g_resultBuffer.data(), maxResults);

		if (numResults > 0) {
			localResults.assign(g_resultBuffer.begin(), g_resultBuffer.begin() + numResults);
		}
	}

	env->ReleaseFloatArrayElements(samples, sampleData, 0);

	if (numResults == 0) {
		return nullptr;
	}

	jclass resultClass = env->FindClass("services/session/keyed/engine/FrameResult");
	if (resultClass == nullptr) {
		LOGE("FrameResult class not found");
		return nullptr;
	}

	jmethodID constructor = env->GetMethodID(resultClass, "<init>", "(FF)V");
	if (constructor == nullptr) {
		LOGE("FrameResult constructor not found");
		return nullptr;
	}

	jobjectArray resultArray = env->NewObjectArray(numResults, resultClass, nullptr);

	for (int i = 0; i < numResults; i++) {
		const auto& r = localResults[i];
		jobject result = env->NewObject(resultClass, constructor,
			r.beatActivation,
			r.downbeatActivation
		);
		env->SetObjectArrayElement(resultArray, i, result);
		env->DeleteLocalRef(result);
	}

	return resultArray;
}

} // extern "C"
