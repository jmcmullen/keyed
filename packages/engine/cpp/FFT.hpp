#pragma once

#include <cstddef>
#include <vector>
#include <complex>

namespace engine {

/**
 * FFT - real-to-complex transform for audio processing.
 *
 * Uses pocketfft, which supports arbitrary FFT sizes.
 */
class FFT {
public:
    /**
     * Create FFT processor for given size
     * @param size FFT size
     */
    explicit FFT(size_t size);
    ~FFT();

    FFT(const FFT&) = delete;
    FFT& operator=(const FFT&) = delete;

    /**
     * Compute real-to-complex FFT
     * @param input Real input samples (size elements)
     * @param output Complex output (size/2 + 1 elements)
     */
    void forward(const float* input, std::complex<float>* output);

    /**
     * Get magnitude spectrum from complex FFT output
     * @param fftOutput Complex FFT output
     * @param magnitudes Output magnitude array (size/2 + 1 elements)
     */
    void magnitude(const std::complex<float>* fftOutput, float* magnitudes);

    /**
     * Get power spectrum (magnitude squared)
     * @param fftOutput Complex FFT output
     * @param power Output power array (size/2 + 1 elements)
     */
    void powerSpectrum(const std::complex<float>* fftOutput, float* power);

    size_t getSize() const { return size_; }
    size_t getOutputSize() const { return size_ / 2 + 1; }

private:
    size_t size_;

    // pocketfft implementation state
    void* impl_;
};

} // namespace engine
