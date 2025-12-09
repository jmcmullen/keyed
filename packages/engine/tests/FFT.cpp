/**
 * FFT implementation using pocketfft
 * Same algorithm as numpy.fft - supports any FFT size
 */

#include "FFT.hpp"
#include "pocketfft_hdronly.h"
#include <cmath>

namespace engine {

struct FFTImpl {
	size_t size;
	pocketfft::shape_t shape;
	pocketfft::stride_t stride_in;
	pocketfft::stride_t stride_out;
	pocketfft::shape_t axes;

	// Pre-allocated buffers
	std::vector<float> inputBuffer;
	std::vector<std::complex<float>> outputBuffer;
};

FFT::FFT(size_t size) : size_(size) {
	auto* impl = new FFTImpl();
	impl->size = size;

	// Setup for 1D real-to-complex FFT
	impl->shape = {size};
	impl->stride_in = {sizeof(float)};
	impl->stride_out = {sizeof(std::complex<float>)};
	impl->axes = {0};

	// Pre-allocate buffers
	impl->inputBuffer.resize(size);
	impl->outputBuffer.resize(size / 2 + 1);

	impl_ = impl;
}

FFT::~FFT() {
	auto* impl = static_cast<FFTImpl*>(impl_);
	delete impl;
}

void FFT::forward(const float* input, std::complex<float>* output) {
	auto* impl = static_cast<FFTImpl*>(impl_);

	// Copy input to buffer
	std::copy(input, input + impl->size, impl->inputBuffer.begin());

	// Perform real-to-complex FFT using pocketfft
	// This matches numpy.fft.rfft exactly
	pocketfft::r2c(
		impl->shape,
		impl->stride_in,
		impl->stride_out,
		impl->axes,
		pocketfft::FORWARD,
		impl->inputBuffer.data(),
		impl->outputBuffer.data(),
		1.0f  // No scaling (matches numpy)
	);

	// Copy to output
	size_t outputSize = impl->size / 2 + 1;
	std::copy(impl->outputBuffer.begin(), impl->outputBuffer.begin() + outputSize, output);
}

void FFT::magnitude(const std::complex<float>* fftOutput, float* magnitudes) {
	size_t outputSize = size_ / 2 + 1;
	for (size_t i = 0; i < outputSize; ++i) {
		magnitudes[i] = std::abs(fftOutput[i]);
	}
}

void FFT::powerSpectrum(const std::complex<float>* fftOutput, float* power) {
	size_t outputSize = size_ / 2 + 1;
	for (size_t i = 0; i < outputSize; ++i) {
		float re = fftOutput[i].real();
		float im = fftOutput[i].imag();
		power[i] = re * re + im * im;
	}
}

} // namespace engine
