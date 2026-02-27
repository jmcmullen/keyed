#pragma once

#include <cstddef>
#include <complex>
#include <memory>
#include <vector>

namespace engine {

/**
 * Configuration matching MusicalKeyCNN's CQT spectrogram parameters
 * From preprocess_data.py: librosa.cqt(sr=44100, hop_length=8820, n_bins=105,
 *                                       bins_per_octave=24, fmin=65)
 */
struct CqtConfig {
	static constexpr int SAMPLE_RATE = 44100;
	static constexpr int HOP_LENGTH = 8820;       // ~200ms at 44100Hz -> ~5 FPS
	static constexpr int N_BINS = 105;            // Total frequency bins
	static constexpr int BINS_PER_OCTAVE = 24;    // Resolution
	static constexpr float F_MIN = 65.0f;         // Hz (C2)
	static constexpr int TIME_FRAMES = 100;       // Frames needed for model input

	// Derived constants
	static constexpr float FPS = static_cast<float>(SAMPLE_RATE) / HOP_LENGTH;

	// Q factor: Q = 1 / (2^(1/bins_per_octave) - 1)
	// For bins_per_octave=24: Q ≈ 34.127
	static constexpr float Q_FACTOR = 34.127f;

	// Max filter length (for lowest frequency bin: Q * sr / fmin)
	// ≈ 34.127 * 44100 / 65 ≈ 23154 samples
	static constexpr int MAX_FILTER_LENGTH = 23200;
};

/**
 * CQT Kernel
 *
 * Pre-computed complex exponential kernel for a single CQT bin.
 * Each bin has its own window length based on its center frequency.
 */
struct CqtKernel {
	float centerFreq;                           // Center frequency (Hz)
	int filterLength;                           // Window length (samples)
	std::vector<std::complex<float>> kernel;    // Complex exponential * window
};

/**
 * CQT Spectrogram Feature Extractor
 *
 * Implements the Constant-Q Transform matching librosa.cqt() exactly.
 *
 * Key properties:
 * - Each frequency bin has a different window length (longer for lower freqs)
 * - Logarithmically spaced frequency bins
 * - Q factor is constant (quality factor = center_freq / bandwidth)
 *
 * The transform for each bin k at frame n is:
 *   CQT[k,n] = sum_m { x[n*hop + m] * kernel_k[m] }
 *
 * where kernel_k[m] = window[m] * exp(-2πi * f_k * m / sr)
 */
class CqtExtractor {
public:
	CqtExtractor();
	~CqtExtractor();

	/** Reset state (call when starting a new audio stream) */
	void reset();

	/**
	 * Process audio and extract CQT frame
	 * @param audio Audio samples centered at the frame position
	 * @param numSamples Number of samples available
	 * @param cqtBins Output CQT magnitude vector (105 floats, log1p scaled)
	 * @return true if CQT frame was produced
	 */
	bool processFrame(const float* audio, int numSamples, float* cqtBins);

	/** Get center frequencies for each bin */
	const std::vector<float>& getCenterFrequencies() const;

	/** Get filter lengths for each bin */
	const std::vector<int>& getFilterLengths() const;

	/** Get number of output CQT bins (105) */
	static constexpr int getNumBins() { return CqtConfig::N_BINS; }

	/** Get frames per second (~5) */
	static constexpr float getFps() { return CqtConfig::FPS; }

	/** Get maximum filter length needed */
	static constexpr int getMaxFilterLength() { return CqtConfig::MAX_FILTER_LENGTH; }

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

/**
 * Streaming CQT Spectrogram Extractor
 *
 * Maintains a circular buffer and outputs 105-dimensional CQT features
 * as audio is pushed. Handles hop-based frame extraction automatically.
 *
 * Buffer size must accommodate the longest filter (lowest frequency bin).
 */
class StreamingCqtExtractor {
public:
	StreamingCqtExtractor();
	~StreamingCqtExtractor();

	/** Reset state */
	void reset();

	/**
	 * Push audio samples and process any complete frames
	 * @param samples Audio samples at 44100Hz
	 * @param numSamples Number of samples
	 * @param cqtFrames Output buffer for CQT frames (must hold maxFrames * 105 floats)
	 * @param maxFrames Maximum number of frames to output
	 * @return Number of CQT frames produced
	 */
	int push(const float* samples, int numSamples,
	         float* cqtFrames, int maxFrames);

	/** Get total frames extracted so far */
	int getFrameCount() const;

	/** Get number of output CQT bins per frame (105) */
	static constexpr int getNumBins() { return CqtConfig::N_BINS; }

	/** Get frames per second (~5) */
	static constexpr float getFps() { return CqtExtractor::getFps(); }

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace engine
