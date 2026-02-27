/**
 * Audio Resampler Implementation
 *
 * Implements 2:1 downsampling using a windowed sinc low-pass filter.
 */

#include "Resampler.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace engine {

// Generate windowed sinc low-pass filter coefficients
static std::vector<float> generateSincFilter(int length, float cutoff) {
	std::vector<float> coeffs(length);
	int halfLen = length / 2;

	// Windowed sinc filter
	float sum = 0.0f;
	for (int i = 0; i < length; i++) {
		float n = static_cast<float>(i - halfLen);

		// Sinc function
		float sinc;
		if (std::abs(n) < 1e-6f) {
			sinc = 1.0f;
		} else {
			sinc = std::sin(M_PI * cutoff * n) / (M_PI * n);
		}

		// Blackman window for good stopband attenuation
		float window = 0.42f - 0.5f * std::cos(2.0f * M_PI * i / (length - 1))
		                     + 0.08f * std::cos(4.0f * M_PI * i / (length - 1));

		coeffs[i] = sinc * window;
		sum += coeffs[i];
	}

	// Normalize to unity gain at DC
	for (int i = 0; i < length; i++) {
		coeffs[i] /= sum;
	}

	return coeffs;
}

Resampler::Resampler(int inputRate, int outputRate)
	: inputRate_(inputRate)
	, outputRate_(outputRate)
	, ratio_(inputRate / outputRate)
{
	// Generate low-pass filter with cutoff below new Nyquist
	// For 2:1 downsample: new Nyquist is at 0.5 of input, so cutoff at ~0.45
	// The sinc cutoff parameter is normalized to 2.0 = Nyquist of input
	// So 0.9 means 0.9 * (fs_out / fs_in) = 0.9 * 0.5 = 0.45 of input Nyquist
	// Use 127 taps for better stopband attenuation (>60dB)
	filterLength_ = 127;
	float cutoff = 0.9f / ratio_;  // 0.45 for 2:1 ratio
	coefficients_ = generateSincFilter(filterLength_, cutoff);

	// Initialize history buffer for streaming
	historySize_ = filterLength_ - 1;
	history_.resize(historySize_, 0.0f);
}

int Resampler::getOutputSize(int inputSize) const {
	return inputSize / ratio_;
}

int Resampler::getDelay() const {
	return (filterLength_ / 2) / ratio_;
}

void Resampler::reset() {
	std::fill(history_.begin(), history_.end(), 0.0f);
}

int Resampler::process(const float* input, int inputSize, float* output) {
	const int halfLen = filterLength_ / 2;
	int outputIdx = 0;

	// Process each output sample (take every ratio'th filtered sample)
	for (int n = halfLen; n < inputSize - halfLen; n += ratio_) {
		float sum = 0.0f;

		// Apply FIR filter
		for (int k = 0; k < filterLength_; k++) {
			int inputIdx = n - halfLen + k;
			sum += input[inputIdx] * coefficients_[k];
		}

		output[outputIdx++] = sum;
	}

	return outputIdx;
}

int Resampler::processStreaming(const float* input, int inputSize, float* output, int maxOutputSize) {
	// Create working buffer with history
	std::vector<float> buffer(historySize_ + inputSize);

	// Copy history to beginning
	std::copy(history_.begin(), history_.end(), buffer.begin());

	// Append new input
	std::copy(input, input + inputSize, buffer.begin() + historySize_);

	const int halfLen = filterLength_ / 2;
	int outputIdx = 0;

	// Process starting from where we have full filter context
	for (int n = halfLen; n < static_cast<int>(buffer.size()) - halfLen && outputIdx < maxOutputSize; n += ratio_) {
		float sum = 0.0f;

		// Apply FIR filter
		for (int k = 0; k < filterLength_; k++) {
			int inputIdx = n - halfLen + k;
			sum += buffer[inputIdx] * coefficients_[k];
		}

		output[outputIdx++] = sum;
	}

	// Update history with the last samples of the combined buffer
	// We need to keep enough samples for the next filter operation
	int bufferSize = static_cast<int>(buffer.size());
	if (bufferSize >= historySize_) {
		std::copy(buffer.end() - historySize_, buffer.end(), history_.begin());
	}

	return outputIdx;
}

// ============================================================================
// LinearResampler - Simple but lower quality
// ============================================================================

LinearResampler::LinearResampler(int inputRate, int outputRate)
	: inputRate_(inputRate)
	, outputRate_(outputRate)
	, ratio_(static_cast<float>(inputRate) / outputRate)
{
}

int LinearResampler::getOutputSize(int inputSize) const {
	return static_cast<int>(inputSize / ratio_);
}

int LinearResampler::process(const float* input, int inputSize, float* output) {
	int outputSize = getOutputSize(inputSize);

	for (int i = 0; i < outputSize; i++) {
		float srcPos = i * ratio_;
		int srcIdx = static_cast<int>(srcPos);
		float frac = srcPos - srcIdx;

		if (srcIdx + 1 < inputSize) {
			// Linear interpolation
			output[i] = input[srcIdx] * (1.0f - frac) + input[srcIdx + 1] * frac;
		} else if (srcIdx < inputSize) {
			output[i] = input[srcIdx];
		} else {
			output[i] = 0.0f;
		}
	}

	return outputSize;
}

} // namespace engine
