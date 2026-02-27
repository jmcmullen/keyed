#pragma once

#ifdef ONNX_ENABLED

// Forward declare ONNX Runtime types
struct OrtApi;
struct OrtEnv;
struct OrtMemoryInfo;

namespace engine {

/**
 * Shared ONNX Runtime Environment
 *
 * Singleton that provides a shared ONNX Runtime environment for all models.
 * Both BeatNet (BPM) and MusicalKeyCNN (key detection) use this shared environment.
 *
 * Benefits:
 * - Single initialization of ONNX Runtime
 * - Shared memory allocator
 * - Reduced memory footprint
 */
class OnnxRuntime {
public:
	/**
	 * Get singleton instance
	 */
	static OnnxRuntime& instance();

	/**
	 * Get ONNX Runtime API
	 */
	const OrtApi* api() const { return api_; }

	/**
	 * Get shared environment
	 */
	OrtEnv* env() const { return env_; }

	/**
	 * Get shared memory info (CPU)
	 */
	OrtMemoryInfo* memoryInfo() const { return memoryInfo_; }

	/**
	 * Check if runtime is initialized successfully
	 */
	bool isInitialized() const { return initialized_; }

	// Non-copyable
	OnnxRuntime(const OnnxRuntime&) = delete;
	OnnxRuntime& operator=(const OnnxRuntime&) = delete;

private:
	OnnxRuntime();
	~OnnxRuntime();

	bool initialize();
	void cleanup();

	const OrtApi* api_ = nullptr;
	OrtEnv* env_ = nullptr;
	OrtMemoryInfo* memoryInfo_ = nullptr;
	bool initialized_ = false;
};

} // namespace engine

#else // !ONNX_ENABLED

// Stub implementation when ONNX is not available
namespace engine {

class OnnxRuntime {
public:
	static OnnxRuntime& instance() {
		static OnnxRuntime inst;
		return inst;
	}

	const void* api() const { return nullptr; }
	void* env() const { return nullptr; }
	void* memoryInfo() const { return nullptr; }
	bool isInitialized() const { return false; }

private:
	OnnxRuntime() = default;
};

} // namespace engine

#endif // ONNX_ENABLED
