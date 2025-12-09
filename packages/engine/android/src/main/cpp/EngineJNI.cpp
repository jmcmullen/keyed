/**
 * JNI bindings for the Engine module
 * Wraps the shared C++ Engine class for Android/Kotlin access
 */

#include "Engine.hpp"
#include <jni.h>
#include <android/log.h>
#include <vector>
#include <string>

#define LOG_TAG "Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static engine::Engine* g_engine = nullptr;
static std::vector<engine::Engine::FrameResult> g_resultBuffer;

extern "C" {

JNIEXPORT void JNICALL
Java_services_session_keyed_engine_EngineModule_nativeInit(JNIEnv* env, jobject thiz) {
    if (g_engine == nullptr) {
        g_engine = new engine::Engine();
        g_resultBuffer.resize(100);
        LOGI("Engine initialized");
    }
}

JNIEXPORT void JNICALL
Java_services_session_keyed_engine_EngineModule_nativeReset(JNIEnv* env, jobject thiz) {
    if (g_engine) {
        g_engine->reset();
    }
}

JNIEXPORT void JNICALL
Java_services_session_keyed_engine_EngineModule_nativeDestroy(JNIEnv* env, jobject thiz) {
    if (g_engine) {
        delete g_engine;
        g_engine = nullptr;
    }
}

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeLoadModel(
    JNIEnv* env, jobject thiz, jstring modelPath) {

    if (!g_engine) {
        return JNI_FALSE;
    }

    const char* path = env->GetStringUTFChars(modelPath, nullptr);
    bool result = g_engine->loadModel(std::string(path));
    env->ReleaseStringUTFChars(modelPath, path);

    LOGI("loadModel result: %s", result ? "success" : "failed");
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeIsReady(JNIEnv* env, jobject thiz) {
    return (g_engine && g_engine->isReady()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_services_session_keyed_engine_EngineModule_nativeWarmUp(JNIEnv* env, jobject thiz) {
    if (!g_engine) {
        return JNI_FALSE;
    }
    bool result = g_engine->warmUp();
    LOGI("warmUp result: %s", result ? "success" : "failed");
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL
Java_services_session_keyed_engine_EngineModule_nativeProcessAudio(
    JNIEnv* env, jobject thiz, jfloatArray samples) {

    if (!g_engine || !g_engine->isReady()) {
        return nullptr;
    }

    jsize numSamples = env->GetArrayLength(samples);
    jfloat* sampleData = env->GetFloatArrayElements(samples, nullptr);

    int maxResults = 100;
    int numResults = g_engine->processAudio(sampleData, numSamples,
                                             g_resultBuffer.data(), maxResults);

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
        const auto& r = g_resultBuffer[i];
        jobject result = env->NewObject(resultClass, constructor,
            r.beatActivation,
            r.downbeatActivation
        );
        env->SetObjectArrayElement(resultArray, i, result);
        env->DeleteLocalRef(result);
    }

    return resultArray;
}

JNIEXPORT jfloat JNICALL
Java_services_session_keyed_engine_EngineModule_nativeGetBpm(JNIEnv* env, jobject thiz) {
    return g_engine ? g_engine->getBpm() : 0.0f;
}

JNIEXPORT jlong JNICALL
Java_services_session_keyed_engine_EngineModule_nativeGetFrameCount(JNIEnv* env, jobject thiz) {
    return g_engine ? static_cast<jlong>(g_engine->getFrameCount()) : 0;
}

} // extern "C"
