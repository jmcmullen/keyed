/**
 * CQT (Constant-Q Transform) Feature Extraction
 *
 * Implements librosa.cqt() exactly for key detection with MusicalKeyCNN.
 *
 * The CQT computes a time-frequency representation with:
 * - Logarithmically spaced frequency bins
 * - Constant Q factor (quality factor = center_freq / bandwidth)
 * - Variable window lengths per bin (longer for lower frequencies)
 *
 * Reference: librosa.cqt() with parameters:
 *   sr=44100, hop_length=8820, n_bins=105, bins_per_octave=24, fmin=65
 */

#include "CqtExtractor.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>

namespace engine {

// ============================================================================
// Constants
// ============================================================================

static constexpr double PI = 3.14159265358979323846;
static constexpr double TWO_PI = 2.0 * PI;

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Compute Q factor for CQT
 * Q = 1 / (2^(1/bins_per_octave) - 1)
 */
static double computeQFactor(int binsPerOctave) {
	return 1.0 / (std::pow(2.0, 1.0 / binsPerOctave) - 1.0);
}

/**
 * Compute center frequencies for all CQT bins
 * f_k = fmin * 2^(k / bins_per_octave)
 */
static std::vector<float> computeCenterFrequencies(int nBins, float fMin, int binsPerOctave) {
	std::vector<float> freqs(nBins);
	for (int k = 0; k < nBins; k++) {
		freqs[k] = fMin * std::pow(2.0f, static_cast<float>(k) / binsPerOctave);
	}
	return freqs;
}

/**
 * Compute filter (window) length for each bin
 * N_k = ceil(Q * sr / f_k)
 */
static std::vector<int> computeFilterLengths(const std::vector<float>& freqs,
                                              double Q, int sampleRate) {
	std::vector<int> lengths(freqs.size());
	for (size_t k = 0; k < freqs.size(); k++) {
		lengths[k] = static_cast<int>(std::ceil(Q * sampleRate / freqs[k]));
	}
	return lengths;
}

/**
 * Create Hann window (matches librosa with fftbins=True)
 * Periodic/asymmetric window: w[n] = 0.5 * (1 - cos(2*pi*n / N))
 */
static void createHannWindow(float* window, int length) {
	for (int i = 0; i < length; i++) {
		// Periodic window (fftbins=True in scipy)
		window[i] = 0.5f * (1.0f - std::cos(TWO_PI * i / length));
	}
}

/**
 * Create CQT kernel for a single bin
 *
 * The kernel is: window[n] * exp(+2*pi*i * f_k * n / sr)
 * (positive frequency, matching librosa)
 */
static CqtKernel createKernel(float centerFreq, int filterLength, int sampleRate) {
	CqtKernel kernel;
	kernel.centerFreq = centerFreq;
	kernel.filterLength = filterLength;
	kernel.kernel.resize(filterLength);

	// Create window (periodic/asymmetric like librosa)
	std::vector<float> window(filterLength);
	createHannWindow(window.data(), filterLength);

	// Create complex exponential kernel with window
	// kernel[n] = window[n] * exp(+2*pi*i * f * n / sr)
	// Using positive frequency to match librosa
	const double freqRatio = TWO_PI * centerFreq / sampleRate;

	for (int n = 0; n < filterLength; n++) {
		double phase = freqRatio * n;  // Positive phase (librosa convention)
		float real = window[n] * std::cos(phase);
		float imag = window[n] * std::sin(phase);
		kernel.kernel[n] = std::complex<float>(real, imag);
	}

	return kernel;
}

// ============================================================================
// CqtExtractor Implementation
// ============================================================================

struct CqtExtractor::Impl {
	std::vector<CqtKernel> kernels;
	std::vector<float> centerFrequencies;
	std::vector<int> filterLengths;
	double Q;
	int maxFilterLength;

	Impl() {
		// Compute Q factor
		Q = computeQFactor(CqtConfig::BINS_PER_OCTAVE);

		// Compute center frequencies
		centerFrequencies = computeCenterFrequencies(
			CqtConfig::N_BINS, CqtConfig::F_MIN, CqtConfig::BINS_PER_OCTAVE);

		// Compute filter lengths
		filterLengths = computeFilterLengths(
			centerFrequencies, Q, CqtConfig::SAMPLE_RATE);

		// Find max filter length
		maxFilterLength = *std::max_element(filterLengths.begin(), filterLengths.end());

		// Pre-compute kernels for each bin
		kernels.resize(CqtConfig::N_BINS);
		for (int k = 0; k < CqtConfig::N_BINS; k++) {
			kernels[k] = createKernel(
				centerFrequencies[k], filterLengths[k], CqtConfig::SAMPLE_RATE);
		}
	}
};

CqtExtractor::CqtExtractor() : impl_(std::make_unique<Impl>()) {}

CqtExtractor::~CqtExtractor() = default;

void CqtExtractor::reset() {
	// No state to reset for single-frame extractor
}

bool CqtExtractor::processFrame(const float* audio, int numSamples, float* cqtBins) {
	const auto& impl = *impl_;

	// Process each bin
	for (int k = 0; k < CqtConfig::N_BINS; k++) {
		const auto& kernel = impl.kernels[k];
		const int len = kernel.filterLength;

		// Check if we have enough samples for this bin
		// Center the window on the frame position
		// Audio should be provided with enough context for all bins
		if (numSamples < len) {
			// Not enough samples - zero pad (should not happen in normal operation)
			cqtBins[k] = 0.0f;
			continue;
		}

		// Compute dot product with kernel (centered)
		// For centered CQT: use samples from [center - len/2, center + len/2)
		const int offset = (numSamples - len) / 2;
		const float* audioStart = audio + offset;

		// Complex dot product with conjugate: sum(audio[n] * conj(kernel[n]))
		// This computes the correlation (inner product) with the kernel
		float realSum = 0.0f;
		float imagSum = 0.0f;

		for (int n = 0; n < len; n++) {
			float sample = audioStart[n];
			// Use conjugate of kernel: conj(a+bi) = a-bi
			realSum += sample * kernel.kernel[n].real();
			imagSum -= sample * kernel.kernel[n].imag();  // Note: minus for conjugate
		}

		// Normalize by sqrt(filter length) / 2 to match librosa's FFT-based CQT
		// The factor of 2 accounts for the difference between time-domain
		// convolution and librosa's FFT-based approach with L1-normalized kernels
		float norm = std::sqrt(static_cast<float>(len)) * 0.5f;
		realSum /= norm;
		imagSum /= norm;

		// Compute magnitude
		float magnitude = std::sqrt(realSum * realSum + imagSum * imagSum);

		// Apply log1p scaling (matches preprocessing: np.log1p(np.abs(cqt)))
		cqtBins[k] = std::log1p(magnitude);
	}

	return true;
}

const std::vector<float>& CqtExtractor::getCenterFrequencies() const {
	return impl_->centerFrequencies;
}

const std::vector<int>& CqtExtractor::getFilterLengths() const {
	return impl_->filterLengths;
}

// ============================================================================
// StreamingCqtExtractor Implementation
// ============================================================================

struct StreamingCqtExtractor::Impl {
	CqtExtractor extractor;

	// Circular buffer for audio samples
	std::vector<float> buffer;
	int64_t writePos;
	int64_t samplesReceived;
	int64_t frameCount;

	// Buffer needs to hold enough samples for:
	// 1. The longest filter (for lowest frequency bin)
	// 2. Plus one hop length for the next frame
	static constexpr int BUFFER_SIZE = CqtConfig::MAX_FILTER_LENGTH + CqtConfig::HOP_LENGTH;

	// Padding for centered framing (half of max filter length)
	static constexpr int PADDING = CqtConfig::MAX_FILTER_LENGTH / 2;

	Impl() : writePos(0), samplesReceived(0), frameCount(0) {
		buffer.resize(BUFFER_SIZE, 0.0f);

		// Pre-fill with zeros for centered framing
		// First frame is centered at sample 0, using zero-padding on the left
		writePos = PADDING;
	}

	void reset() {
		std::fill(buffer.begin(), buffer.end(), 0.0f);
		writePos = PADDING;
		samplesReceived = 0;
		frameCount = 0;
		extractor.reset();
	}
};

StreamingCqtExtractor::StreamingCqtExtractor()
	: impl_(std::make_unique<Impl>()) {}

StreamingCqtExtractor::~StreamingCqtExtractor() = default;

void StreamingCqtExtractor::reset() {
	impl_->reset();
}

int StreamingCqtExtractor::push(const float* samples, int numSamples,
                                 float* cqtFrames, int maxFrames) {
	auto& impl = *impl_;
	const int hopLength = CqtConfig::HOP_LENGTH;
	const int maxFilterLen = impl.extractor.getMaxFilterLength();

	int framesProduced = 0;

	for (int i = 0; i < numSamples; i++) {
		// Write sample to buffer (with wrap-around)
		impl.buffer[static_cast<size_t>(impl.writePos % Impl::BUFFER_SIZE)] = samples[i];
		impl.writePos++;
		impl.samplesReceived++;

		// Check if we have enough samples for a frame
		// Frame N is centered at sample N * hopLength
		// We need maxFilterLen/2 samples after the center for centered framing
		// samplesNeeded = N * hopLength + maxFilterLen/2
		int64_t samplesNeeded =
			impl.frameCount * static_cast<int64_t>(hopLength) + maxFilterLen / 2;

		if (impl.samplesReceived >= samplesNeeded) {
			// Frame N is centered at sample N * hopLength.
			int64_t frameCenter = impl.frameCount * static_cast<int64_t>(hopLength);

			if (framesProduced < maxFrames) {
				// We need maxFilterLen samples centered at frameCenter.
				std::vector<float> frameAudio(maxFilterLen);

				int64_t startSample = frameCenter - maxFilterLen / 2;
				for (int j = 0; j < maxFilterLen; j++) {
					int64_t sampleIdx = startSample + j;
					// Map to buffer position (accounting for initial padding).
					int64_t bufIdx = (Impl::PADDING + sampleIdx) % Impl::BUFFER_SIZE;
					if (bufIdx < 0) {
						bufIdx += Impl::BUFFER_SIZE;
					}
					frameAudio[j] = impl.buffer[static_cast<size_t>(bufIdx)];
				}

				// Process frame.
				impl.extractor.processFrame(
					frameAudio.data(), maxFilterLen,
					cqtFrames + framesProduced * CqtConfig::N_BINS);

				framesProduced++;
			}

			// Always advance frameCount once the frame becomes available.
			// This keeps scheduling in sync even when caller's maxFrames is reached.
			impl.frameCount++;
		}
	}

	return framesProduced;
}

int StreamingCqtExtractor::getFrameCount() const {
	if (impl_->frameCount > std::numeric_limits<int>::max()) {
		return std::numeric_limits<int>::max();
	}
	return static_cast<int>(impl_->frameCount);
}

} // namespace engine
