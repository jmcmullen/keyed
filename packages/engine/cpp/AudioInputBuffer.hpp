#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace engine {

struct AudioInputStats {
	uint64_t writes;
	uint64_t reads;
	uint64_t writtenFrames;
	uint64_t outputFrames;
	uint64_t droppedFrames;
	uint64_t resampleReads;
	uint64_t queuedFrames;
	uint64_t lastInputFrames;
	double sourceRate;
	double targetRate;
	float peak;
	float rms;
};

class AudioInputBuffer {
public:
	AudioInputBuffer(size_t capacity, double targetRate);

	void configure(double sourceRate);
	void reset();
	int writeMono(const float* input, int frames);
	int read(float* output, int capacity);
	AudioInputStats stats() const;

private:
	float sampleAt(uint64_t index) const;
	void resetLocked();
	void syncCursor(uint64_t read);

	size_t capacity_;
	double targetRate_;
	std::vector<float> buffer_;
	mutable std::mutex mutex_;
	std::atomic<uint64_t> read_;
	std::atomic<uint64_t> write_;
	std::atomic<uint64_t> writes_;
	std::atomic<uint64_t> reads_;
	std::atomic<uint64_t> writtenFrames_;
	std::atomic<uint64_t> outputFrames_;
	std::atomic<uint64_t> droppedFrames_;
	std::atomic<uint64_t> resampleReads_;
	std::atomic<uint64_t> lastInputFrames_;
	std::atomic<double> sourceRate_;
	std::atomic<float> peak_;
	std::atomic<float> rms_;
	double pos_;
	uint64_t cursor_;
};

} // namespace engine
