#pragma once

#include "pocketfft_hdronly.h"
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>

namespace engine {

/**
 * Autocorrelation-based BPM estimation from neural network activations.
 *
 * This method achieves higher accuracy than beat interval timing by:
 * 1. Computing autocorrelation on raw activations (finds dominant periodicity)
 * 2. Using parabolic interpolation for sub-frame precision
 * 3. Avoiding quantization from discrete beat detection
 *
 * Algorithm:
 * 1. Sum beat + downbeat activations
 * 2. Compute autocorrelation via FFT (O(n log n))
 * 3. Find peak in valid tempo range (60-180 BPM)
 * 4. Refine with parabolic interpolation
 * 5. Convert lag to BPM: bpm = 60 * fps / peak_lag
 */
class AutocorrBpmEstimator {
public:
	static constexpr float FPS = 50.0f;
	static constexpr float MIN_BPM = 60.0f;
	static constexpr float MAX_BPM = 180.0f;

	// DJ music typical range for octave correction
	static constexpr float DJ_MIN_BPM = 75.0f;
	static constexpr float DJ_MAX_BPM = 165.0f;

	/**
	 * Estimate BPM from neural network activations using autocorrelation.
	 *
	 * @param beatActivations Beat activation probabilities (one per frame)
	 * @param downbeatActivations Downbeat activation probabilities (one per frame)
	 * @param numFrames Number of frames in the arrays
	 * @param applyOctaveCorrection If true, correct half/double time for DJ range
	 * @return Estimated BPM (rounded to integer), or 0 if insufficient data
	 */
	static float estimate(const float* beatActivations,
	                      const float* downbeatActivations,
	                      size_t numFrames,
	                      bool applyOctaveCorrection = true) {
		if (numFrames < static_cast<size_t>(FPS)) {
			return 0.0f;
		}

		// Sum beat + downbeat activations
		std::vector<float> signal(numFrames);
		for (size_t i = 0; i < numFrames; i++) {
			signal[i] = beatActivations[i] + downbeatActivations[i];
		}

		// Compute autocorrelation using FFT
		std::vector<float> autocorr = computeAutocorrelationFFT(signal);

		// Valid tempo range lags
		int minLag = static_cast<int>(FPS * 60.0f / MAX_BPM);  // ~17 frames (180 BPM)
		int maxLag = static_cast<int>(FPS * 60.0f / MIN_BPM);  // 50 frames (60 BPM)

		if (maxLag >= static_cast<int>(numFrames)) {
			maxLag = static_cast<int>(numFrames) - 1;
		}
		if (minLag >= maxLag) {
			return 0.0f;
		}

		// Find peak in valid range
		int peakIdx = minLag;
		float peakVal = autocorr[minLag];
		for (int i = minLag + 1; i < maxLag; i++) {
			if (autocorr[i] > peakVal) {
				peakVal = autocorr[i];
				peakIdx = i;
			}
		}

		// Refine with parabolic interpolation
		float refinedPeakIdx = static_cast<float>(peakIdx);
		if (peakIdx > 0 && peakIdx < static_cast<int>(numFrames) - 1) {
			float y0 = autocorr[peakIdx - 1];
			float y1 = autocorr[peakIdx];
			float y2 = autocorr[peakIdx + 1];
			if (y1 > y0 && y1 > y2) {  // Valid peak
				float denom = y0 - 2.0f * y1 + y2;
				if (std::fabs(denom) > 1e-8f) {
					float offset = 0.5f * (y0 - y2) / denom;
					refinedPeakIdx = peakIdx + offset;
				}
			}
		}

		// Convert lag to BPM
		float bpm = 60.0f * FPS / refinedPeakIdx;
		bpm = std::round(bpm);

		// Apply octave correction for DJ range
		if (applyOctaveCorrection && bpm > 0) {
			float doubled = bpm * 2.0f;
			float halved = bpm / 2.0f;

			if (bpm < DJ_MIN_BPM && doubled >= DJ_MIN_BPM && doubled <= DJ_MAX_BPM) {
				bpm = doubled;
			} else if (bpm > DJ_MAX_BPM && halved >= DJ_MIN_BPM && halved <= DJ_MAX_BPM) {
				bpm = halved;
			}
		}

		return bpm;
	}

private:
	/**
	 * Compute autocorrelation using FFT method - O(n log n)
	 * autocorr[k] = sum_i(signal[i] * signal[i+k])
	 *
	 * Uses pocketfft for efficient computation.
	 */
	static std::vector<float> computeAutocorrelationFFT(const std::vector<float>& signal) {
		size_t n = signal.size();

		// FFT size should be power of 2 and at least 2*n for linear (non-circular) autocorr
		size_t fftSize = 1;
		while (fftSize < 2 * n) fftSize *= 2;

		// Zero-pad signal into complex buffer
		std::vector<std::complex<float>> padded(fftSize, {0.0f, 0.0f});
		for (size_t i = 0; i < n; i++) {
			padded[i] = {signal[i], 0.0f};
		}

		// Setup pocketfft parameters
		pocketfft::shape_t shape = {fftSize};
		pocketfft::stride_t stride = {sizeof(std::complex<float>)};
		pocketfft::shape_t axes = {0};

		// Forward FFT (in-place)
		std::vector<std::complex<float>> spectrum(fftSize);
		pocketfft::c2c(shape, stride, stride, axes, pocketfft::FORWARD,
		               padded.data(), spectrum.data(), 1.0f);

		// Power spectrum (multiply by conjugate)
		for (size_t k = 0; k < fftSize; k++) {
			float re = spectrum[k].real();
			float im = spectrum[k].imag();
			spectrum[k] = {re * re + im * im, 0.0f};
		}

		// Inverse FFT
		std::vector<std::complex<float>> ifftResult(fftSize);
		pocketfft::c2c(shape, stride, stride, axes, pocketfft::BACKWARD,
		               spectrum.data(), ifftResult.data(), 1.0f / fftSize);

		// Extract real part and normalize
		std::vector<float> autocorr(n);
		float norm = ifftResult[0].real() + 1e-8f;
		for (size_t i = 0; i < n; i++) {
			autocorr[i] = ifftResult[i].real() / norm;
		}

		return autocorr;
	}
};

/**
 * Circular buffer to collect activations for autocorrelation BPM estimation.
 * Uses a ring buffer for O(1) push operations.
 *
 * Also caches BPM computation to avoid redundant calculations.
 */
class ActivationBuffer {
public:
	static constexpr size_t DEFAULT_MAX_FRAMES = 512;  // ~10 seconds at 50 FPS
	static constexpr size_t MIN_FRAMES_FOR_BPM = 100;  // ~2 seconds

	explicit ActivationBuffer(size_t maxFrames = DEFAULT_MAX_FRAMES)
		: maxFrames_(maxFrames),
		  head_(0),
		  count_(0),
		  cachedBpm_(0.0f),
		  framesSinceLastCompute_(0),
		  bpmComputeInterval_(25) {  // Recompute every 25 frames (~500ms)
		beatActivations_.resize(maxFrames, 0.0f);
		downbeatActivations_.resize(maxFrames, 0.0f);
	}

	void push(float beatActivation, float downbeatActivation) {
		beatActivations_[head_] = beatActivation;
		downbeatActivations_[head_] = downbeatActivation;
		head_ = (head_ + 1) % maxFrames_;
		if (count_ < maxFrames_) {
			count_++;
		}
		framesSinceLastCompute_++;

		// Auto-compute BPM periodically if we have enough frames
		if (count_ >= MIN_FRAMES_FOR_BPM &&
		    framesSinceLastCompute_ >= bpmComputeInterval_) {
			recomputeBpm();
		}
	}

	void clear() {
		head_ = 0;
		count_ = 0;
		cachedBpm_ = 0.0f;
		framesSinceLastCompute_ = 0;
	}

	size_t size() const {
		return count_;
	}

	/**
	 * Get cached BPM estimate. Returns 0 if insufficient data.
	 * BPM is automatically recomputed periodically during push().
	 */
	float getCachedBpm() const {
		return cachedBpm_;
	}

	/**
	 * Force recompute BPM (used when stopping recording).
	 */
	float estimateBpm(bool applyOctaveCorrection = true) {
		if (count_ < MIN_FRAMES_FOR_BPM) {
			return 0.0f;
		}

		// Extract activations in order (oldest to newest)
		std::vector<float> beatActs(count_);
		std::vector<float> downbeatActs(count_);
		extractInOrder(beatActs.data(), downbeatActs.data());

		cachedBpm_ = AutocorrBpmEstimator::estimate(
			beatActs.data(), downbeatActs.data(), count_, applyOctaveCorrection);
		framesSinceLastCompute_ = 0;

		return cachedBpm_;
	}

private:
	void recomputeBpm() {
		// Extract activations in order
		std::vector<float> beatActs(count_);
		std::vector<float> downbeatActs(count_);
		extractInOrder(beatActs.data(), downbeatActs.data());

		cachedBpm_ = AutocorrBpmEstimator::estimate(
			beatActs.data(), downbeatActs.data(), count_, true);
		framesSinceLastCompute_ = 0;
	}

	void extractInOrder(float* beatOut, float* downbeatOut) const {
		// Ring buffer: oldest is at (head_ - count_ + maxFrames_) % maxFrames_
		// or simply: if count_ < maxFrames_, start at 0; else start at head_
		size_t start = (count_ < maxFrames_) ? 0 : head_;
		for (size_t i = 0; i < count_; i++) {
			size_t idx = (start + i) % maxFrames_;
			beatOut[i] = beatActivations_[idx];
			downbeatOut[i] = downbeatActivations_[idx];
		}
	}

	size_t maxFrames_;
	size_t head_;      // Next write position
	size_t count_;     // Current number of frames (up to maxFrames_)
	std::vector<float> beatActivations_;
	std::vector<float> downbeatActivations_;

	// BPM caching
	float cachedBpm_;
	size_t framesSinceLastCompute_;
	size_t bpmComputeInterval_;
};

} // namespace engine
