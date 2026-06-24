#include "AudioInputBuffer.hpp"

#include <algorithm>
#include <cmath>

namespace engine {

AudioInputBuffer::AudioInputBuffer(size_t capacity, double targetRate)
	: capacity_(std::max<size_t>(capacity, 2))
	, targetRate_(targetRate > 0 ? targetRate : 44100.0)
	, buffer_(capacity_, 0.0f)
	, read_(0)
	, write_(0)
	, writes_(0)
	, reads_(0)
	, writtenFrames_(0)
	, outputFrames_(0)
	, droppedFrames_(0)
	, resampleReads_(0)
	, lastInputFrames_(0)
	, sourceRate_(targetRate_)
	, peak_(0.0f)
	, rms_(0.0f)
	, pos_(0.0)
	, cursor_(0)
{
}

void AudioInputBuffer::configure(double sourceRate) {
	std::lock_guard<std::mutex> lock(mutex_);
	sourceRate_.store(sourceRate > 0 ? sourceRate : targetRate_, std::memory_order_release);
	resetLocked();
}

void AudioInputBuffer::reset() {
	std::lock_guard<std::mutex> lock(mutex_);
	resetLocked();
}

void AudioInputBuffer::resetLocked() {
	read_.store(0, std::memory_order_release);
	write_.store(0, std::memory_order_release);
	writes_.store(0, std::memory_order_release);
	reads_.store(0, std::memory_order_release);
	writtenFrames_.store(0, std::memory_order_release);
	outputFrames_.store(0, std::memory_order_release);
	droppedFrames_.store(0, std::memory_order_release);
	resampleReads_.store(0, std::memory_order_release);
	lastInputFrames_.store(0, std::memory_order_release);
	peak_.store(0.0f, std::memory_order_release);
	rms_.store(0.0f, std::memory_order_release);
	pos_ = 0.0;
	cursor_ = 0;
	std::fill(buffer_.begin(), buffer_.end(), 0.0f);
}

int AudioInputBuffer::writeMono(const float* input, int frames) {
	if (input == nullptr || frames <= 0) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(mutex_);

	const int start = frames > static_cast<int>(capacity_)
		? frames - static_cast<int>(capacity_)
		: 0;
	const int count = frames - start;
	const auto extra = static_cast<uint64_t>(start);

	uint64_t write = write_.load(std::memory_order_relaxed);
	uint64_t read = read_.load(std::memory_order_acquire);
	const uint64_t needed = static_cast<uint64_t>(count);

	if (write + needed > read + capacity_) {
		const uint64_t next = write + needed - capacity_;
		while (read < next && !read_.compare_exchange_weak(
			read,
			next,
			std::memory_order_release,
			std::memory_order_acquire
		)) {}
		if (read < next) {
			droppedFrames_.fetch_add(next - read, std::memory_order_relaxed);
			read = next;
		}
	}

	float peak = 0.0f;
	float sum = 0.0f;
	for (int i = 0; i < count; i++) {
		const float sample = input[start + i];
		const float abs = std::abs(sample);
		peak = std::max(peak, abs);
		sum += sample * sample;
		buffer_[(write + static_cast<uint64_t>(i)) % capacity_] = sample;
	}

	write_.store(write + needed, std::memory_order_release);
	writes_.fetch_add(1, std::memory_order_relaxed);
	writtenFrames_.fetch_add(needed, std::memory_order_relaxed);
	droppedFrames_.fetch_add(extra, std::memory_order_relaxed);
	lastInputFrames_.store(static_cast<uint64_t>(frames), std::memory_order_release);
	peak_.store(peak, std::memory_order_release);
	rms_.store(count > 0 ? std::sqrt(sum / static_cast<float>(count)) : 0.0f, std::memory_order_release);
	return count;
}

int AudioInputBuffer::read(float* output, int capacity) {
	if (output == nullptr || capacity <= 0) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(mutex_);

	uint64_t read = read_.load(std::memory_order_relaxed);
	const uint64_t write = write_.load(std::memory_order_acquire);
	if (write <= read) {
		return 0;
	}

	syncCursor(read);

	const uint64_t available = write - read;
	const double rate = sourceRate_.load(std::memory_order_acquire);
	if (std::abs(rate - targetRate_) < 1.0) {
		const int count = static_cast<int>(std::min<uint64_t>(
			static_cast<uint64_t>(capacity),
			available
		));
		for (int i = 0; i < count; i++) {
			output[i] = sampleAt(read + static_cast<uint64_t>(i));
		}
		read_.store(read + static_cast<uint64_t>(count), std::memory_order_release);
		cursor_ = read + static_cast<uint64_t>(count);
		pos_ = 0.0;
		reads_.fetch_add(1, std::memory_order_relaxed);
		outputFrames_.fetch_add(static_cast<uint64_t>(count), std::memory_order_relaxed);
		return count;
	}

	if (available < 2) {
		return 0;
	}

	const double ratio = rate / targetRate_;
	int produced = 0;
	while (produced < capacity && pos_ + 1.0 < static_cast<double>(available)) {
		const auto index = static_cast<uint64_t>(pos_);
		const float frac = static_cast<float>(pos_ - static_cast<double>(index));
		const float prev = sampleAt(read + index);
		const float next = sampleAt(read + index + 1);
		output[produced] = prev + (next - prev) * frac;
		produced++;
		pos_ += ratio;
	}

	if (produced == 0) {
		return 0;
	}

	const auto consumed = std::min<uint64_t>(
		static_cast<uint64_t>(std::floor(pos_)),
		available - 1
	);
	if (consumed > 0) {
		read_.store(read + consumed, std::memory_order_release);
		cursor_ = read + consumed;
		pos_ -= static_cast<double>(consumed);
	}

	reads_.fetch_add(1, std::memory_order_relaxed);
	outputFrames_.fetch_add(static_cast<uint64_t>(produced), std::memory_order_relaxed);
	resampleReads_.fetch_add(1, std::memory_order_relaxed);
	return produced;
}

AudioInputStats AudioInputBuffer::stats() const {
	const uint64_t read = read_.load(std::memory_order_acquire);
	const uint64_t write = write_.load(std::memory_order_acquire);
	return {
		writes_.load(std::memory_order_acquire),
		reads_.load(std::memory_order_acquire),
		writtenFrames_.load(std::memory_order_acquire),
		outputFrames_.load(std::memory_order_acquire),
		droppedFrames_.load(std::memory_order_acquire),
		resampleReads_.load(std::memory_order_acquire),
		write > read ? write - read : 0,
		lastInputFrames_.load(std::memory_order_acquire),
		sourceRate_.load(std::memory_order_acquire),
		targetRate_,
		peak_.load(std::memory_order_acquire),
		rms_.load(std::memory_order_acquire),
	};
}

float AudioInputBuffer::sampleAt(uint64_t index) const {
	return buffer_[index % capacity_];
}

void AudioInputBuffer::syncCursor(uint64_t read) {
	if (read == cursor_) {
		return;
	}

	if (read > cursor_) {
		const double delta = static_cast<double>(read - cursor_);
		pos_ = delta >= pos_ ? 0.0 : pos_ - delta;
		cursor_ = read;
		return;
	}

	pos_ = 0.0;
	cursor_ = read;
}

} // namespace engine
