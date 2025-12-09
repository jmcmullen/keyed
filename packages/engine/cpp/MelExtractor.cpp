/**
 * Mel Spectrogram Feature Extraction for BeatNet
 *
 * Exact port of madmom's LogarithmicFilterbank to C++
 * Implements the preprocessing pipeline matching BeatNet's log_spect.py
 */

#include "MelExtractor.hpp"
#include "FFT.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <complex>

namespace engine {

// ============================================================================
// Utility Functions - Exact match to madmom/audio/filters.py
// ============================================================================

/**
 * Create Hann window (matches scipy.signal.hann)
 */
static void createHannWindow(float* window, int length) {
    const double PI = 3.14159265358979323846;
    for (int i = 0; i < length; i++) {
        // scipy hann uses symmetric window by default
        window[i] = 0.5f * (1.0f - std::cos((2.0 * PI * i) / (length - 1)));
    }
}

/**
 * Generate logarithmically spaced frequencies - exact match to madmom log_frequencies()
 *
 * def log_frequencies(bands_per_octave, fmin, fmax, fref=A4):
 *     left = np.floor(np.log2(float(fmin) / fref) * bands_per_octave)
 *     right = np.ceil(np.log2(float(fmax) / fref) * bands_per_octave)
 *     frequencies = fref * 2. ** (np.arange(left, right) / float(bands_per_octave))
 *     frequencies = frequencies[np.searchsorted(frequencies, fmin):]
 *     frequencies = frequencies[:np.searchsorted(frequencies, fmax, 'right')]
 *     return frequencies
 */
static std::vector<float> logFrequencies(int bandsPerOctave, float fMin, float fMax, float fRef) {
    const double log2Fmin = std::log2(static_cast<double>(fMin) / fRef);
    const double log2Fmax = std::log2(static_cast<double>(fMax) / fRef);

    const int left = static_cast<int>(std::floor(log2Fmin * bandsPerOctave));
    const int right = static_cast<int>(std::ceil(log2Fmax * bandsPerOctave));

    // Generate all frequencies in range [left, right)
    std::vector<float> allFreqs;
    for (int i = left; i < right; i++) {
        float freq = fRef * std::pow(2.0f, static_cast<float>(i) / bandsPerOctave);
        allFreqs.push_back(freq);
    }

    // Filter by fmin/fmax (searchsorted equivalent)
    std::vector<float> frequencies;
    for (float f : allFreqs) {
        if (f >= fMin && f <= fMax) {
            frequencies.push_back(f);
        }
    }

    return frequencies;
}

/**
 * Convert frequencies to bins - exact match to madmom frequencies2bins()
 *
 * Uses searchsorted to find closest bin (not round)
 */
static std::vector<int> frequencies2bins(
    const std::vector<float>& frequencies,
    const std::vector<float>& binFrequencies,
    bool uniqueBins
) {
    std::vector<int> indices;
    indices.reserve(frequencies.size());

    for (float freq : frequencies) {
        // searchsorted: find insertion point
        auto it = std::lower_bound(binFrequencies.begin(), binFrequencies.end(), freq);
        int idx = static_cast<int>(it - binFrequencies.begin());

        // clip to valid range [1, len-1] then choose closest
        idx = std::max(1, std::min(idx, static_cast<int>(binFrequencies.size()) - 1));

        float left = binFrequencies[idx - 1];
        float right = binFrequencies[idx];

        // Choose the closer bin
        if (freq - left < right - freq) {
            idx = idx - 1;
        }

        indices.push_back(idx);
    }

    // Remove duplicates if uniqueBins is true
    if (uniqueBins) {
        std::vector<int> unique;
        for (int idx : indices) {
            if (unique.empty() || unique.back() != idx) {
                unique.push_back(idx);
            }
        }
        return unique;
    }

    return indices;
}

// ============================================================================
// LogFilterbank Implementation - Exact match to madmom LogarithmicFilterbank
// ============================================================================

LogFilterbank::LogFilterbank(int fftSize, int sampleRate, int bandsPerOctave,
                             float fMin, float fMax, bool normalize)
    : numBins_(fftSize / 2) {  // Exclude Nyquist like madmom

    // Generate bin frequencies using madmom's fft_frequencies formula:
    // np.fft.fftfreq(num_bins * 2, 1/sample_rate)[:num_bins]
    // This is equivalent to: i * sample_rate / (num_bins * 2)
    std::vector<float> binFrequencies(numBins_);
    for (int i = 0; i < numBins_; i++) {
        binFrequencies[i] = static_cast<float>(i) * sampleRate / (numBins_ * 2);
    }

    // Generate log-spaced center frequencies
    auto frequencies = logFrequencies(bandsPerOctave, fMin, fMax, MelConfig::F_REF);

    // Convert to bins with unique_bins=True
    auto bins = frequencies2bins(frequencies, binFrequencies, true);

    // Create triangular filters using overlapping triplets
    // Match TriangularFilter.band_bins() with overlap=True
    numBands_ = 0;

    for (size_t i = 0; i + 2 < bins.size(); i++) {
        int start = bins[i];
        int center = bins[i + 1];
        int stop = bins[i + 2];

        // Handle too-small filters (madmom: if stop - start < 2)
        if (stop - start < 2) {
            center = start;
            stop = start + 1;
        }

        // Create filter
        std::vector<float> filter(numBins_, 0.0f);

        // Make center and stop relative to start for the filter data
        int relCenter = center - start;
        int relStop = stop - start;

        // Rising edge (without the center) - linspace(0, 1, center, endpoint=False)
        for (int k = 0; k < relCenter; k++) {
            float val = static_cast<float>(k) / relCenter;
            if (start + k < numBins_) {
                filter[start + k] = val;
            }
        }

        // Falling edge (including center, without last bin) - linspace(1, 0, stop-center, endpoint=False)
        for (int k = 0; k < relStop - relCenter; k++) {
            float val = 1.0f - static_cast<float>(k) / (relStop - relCenter);
            if (center + k < numBins_) {
                filter[center + k] = val;
            }
        }

        // Normalize filter (sum to 1)
        if (normalize) {
            float sum = 0.0f;
            for (int k = 0; k < numBins_; k++) {
                sum += filter[k];
            }
            if (sum > 0.0f) {
                for (int k = 0; k < numBins_; k++) {
                    filter[k] /= sum;
                }
            }
        }

        filters_.push_back(std::move(filter));
        numBands_++;
    }
}

void LogFilterbank::apply(const float* magnitude, float* output) const {
    for (int m = 0; m < numBands_; m++) {
        float sum = 0.0f;
        const auto& filter = filters_[m];
        for (int k = 0; k < numBins_; k++) {
            sum += magnitude[k] * filter[k];
        }
        output[m] = sum;
    }
}

// ============================================================================
// MelExtractor Implementation
// ============================================================================

struct MelExtractor::Impl {
    // FFT processor
    std::unique_ptr<FFT> fft;

    // Filterbank
    std::unique_ptr<LogFilterbank> filterbank;

    // Pre-allocated buffers
    std::vector<float> window;
    std::vector<float> windowedBuffer;
    std::vector<std::complex<float>> fftOutput;
    std::vector<float> magnitudeBuffer;
    std::vector<float> filteredBuffer;
    std::vector<float> logMelBuffer;
    std::vector<float> previousLogMel;
    std::vector<float> diffBuffer;

    bool hasPreviousFrame;

    Impl() : hasPreviousFrame(false) {
        const int fftSize = MelConfig::FFT_SIZE;  // 1411 - exact match to madmom
        const int numBins = fftSize / 2 + 1;      // 706 bins from FFT
        const int filterbankBins = fftSize / 2;   // 705 bins (madmom excludes Nyquist)

        // Initialize FFT with exact size (pocketfft supports any size, like numpy)
        fft = std::make_unique<FFT>(fftSize);

        // Create filterbank
        filterbank = std::make_unique<LogFilterbank>(
            fftSize,
            MelConfig::SAMPLE_RATE,
            MelConfig::BANDS_PER_OCTAVE,
            MelConfig::F_MIN,
            MelConfig::F_MAX,
            true  // normalize
        );

        const int nBands = filterbank->getNumBands();

        // Allocate buffers
        window.resize(MelConfig::WIN_LENGTH);
        createHannWindow(window.data(), MelConfig::WIN_LENGTH);

        windowedBuffer.resize(fftSize, 0.0f);
        fftOutput.resize(numBins);
        magnitudeBuffer.resize(filterbankBins);  // Exclude Nyquist like madmom
        filteredBuffer.resize(nBands);
        logMelBuffer.resize(nBands);
        previousLogMel.resize(nBands, 0.0f);
        diffBuffer.resize(nBands);
    }
};

MelExtractor::MelExtractor() : impl_(std::make_unique<Impl>()) {}

MelExtractor::~MelExtractor() = default;

void MelExtractor::reset() {
    impl_->hasPreviousFrame = false;
    std::fill(impl_->previousLogMel.begin(), impl_->previousLogMel.end(), 0.0f);
}

bool MelExtractor::processFrame(const float* frame, int frameLength, float* features) {
    auto& impl = *impl_;
    const int winLength = MelConfig::WIN_LENGTH;
    const int filterbankBins = MelConfig::FFT_SIZE / 2;  // 705 bins (exclude Nyquist like madmom)
    const int nBands = impl.filterbank->getNumBands();

    // Apply Hann window
    const int len = std::min(frameLength, winLength);
    for (int i = 0; i < len; i++) {
        impl.windowedBuffer[i] = frame[i] * impl.window[i];
    }
    // Zero-pad the rest (shouldn't happen if frameLength == winLength)
    for (int i = len; i < static_cast<int>(impl.windowedBuffer.size()); i++) {
        impl.windowedBuffer[i] = 0.0f;
    }

    // Compute FFT
    impl.fft->forward(impl.windowedBuffer.data(), impl.fftOutput.data());

    // Compute magnitude spectrum (exclude Nyquist bin like madmom's include_nyquist=False)
    for (int i = 0; i < filterbankBins; i++) {
        impl.magnitudeBuffer[i] = std::abs(impl.fftOutput[i]);
    }

    // Apply filterbank
    impl.filterbank->apply(impl.magnitudeBuffer.data(), impl.filteredBuffer.data());

    // Apply log scaling: log10(1 + S) - matches LogarithmicSpectrogramProcessor(mul=1, add=1)
    for (int i = 0; i < nBands; i++) {
        impl.logMelBuffer[i] = std::log10(1.0f + impl.filteredBuffer[i]);
    }

    // Compute spectral difference
    // First frame: diff is all zeros (no previous frame to compare against)
    // Subsequent frames: diff = max(current - previous, 0)
    if (!impl.hasPreviousFrame) {
        // First frame: diff is zeros
        std::fill(impl.diffBuffer.begin(), impl.diffBuffer.end(), 0.0f);
        impl.hasPreviousFrame = true;
    } else {
        // Compute diff with half-wave rectification (positive_diffs=True)
        for (int i = 0; i < nBands; i++) {
            float d = impl.logMelBuffer[i] - impl.previousLogMel[i];
            impl.diffBuffer[i] = (d > 0.0f) ? d : 0.0f;
        }
    }

    // Copy current to previous for next frame
    std::copy(impl.logMelBuffer.begin(), impl.logMelBuffer.end(),
              impl.previousLogMel.begin());

    // Stack features: [log-mel, diff] = 272 features (stack_diffs=np.hstack)
    std::copy(impl.logMelBuffer.begin(), impl.logMelBuffer.end(), features);
    std::copy(impl.diffBuffer.begin(), impl.diffBuffer.end(), features + nBands);

    return true;
}

// ============================================================================
// StreamingMelExtractor Implementation
// ============================================================================

struct StreamingMelExtractor::Impl {
    MelExtractor extractor;
    std::vector<float> buffer;
    int writePos;
    int samplesUntilNextFrame;

    // Pre-padding for centered frames (madmom compatibility)
    static constexpr int PADDING = MelConfig::WIN_LENGTH / 2;  // 705

    Impl() {
        // Buffer size: enough for one full window plus one hop
        buffer.resize(MelConfig::WIN_LENGTH + MelConfig::HOP_LENGTH, 0.0f);

        // Pre-fill with zeros for centered framing (like madmom)
        // Frame 0 is centered at sample 0, so it uses zeros[0:705] + audio[0:706]
        writePos = PADDING;  // Start writing after the zero-padding

        // First frame is ready after receiving (WIN_LENGTH - PADDING) samples
        // For WIN_LENGTH=1411, PADDING=705, this is 706 samples
        samplesUntilNextFrame = MelConfig::WIN_LENGTH - PADDING;
    }
};

StreamingMelExtractor::StreamingMelExtractor()
    : impl_(std::make_unique<Impl>()) {}

StreamingMelExtractor::~StreamingMelExtractor() = default;

void StreamingMelExtractor::reset() {
    impl_->extractor.reset();
    std::fill(impl_->buffer.begin(), impl_->buffer.end(), 0.0f);
    // Reset to padded state (like constructor)
    impl_->writePos = Impl::PADDING;
    impl_->samplesUntilNextFrame = MelConfig::WIN_LENGTH - Impl::PADDING;
}

int StreamingMelExtractor::push(const float* samples, int numSamples,
                                 float* features, int maxFrames) {
    auto& impl = *impl_;
    const int winLength = MelConfig::WIN_LENGTH;
    const int hopLength = MelConfig::HOP_LENGTH;
    const int bufferSize = static_cast<int>(impl.buffer.size());
    const int featureDim = MelConfig::MODEL_INPUT_DIM;

    int framesProduced = 0;
    std::vector<float> frame(winLength);

    for (int i = 0; i < numSamples && framesProduced < maxFrames; i++) {
        // Write sample to circular buffer
        impl.buffer[impl.writePos % bufferSize] = samples[i];
        impl.writePos++;
        impl.samplesUntilNextFrame--;

        // Check if we have a complete frame
        if (impl.samplesUntilNextFrame <= 0) {
            // Extract frame from circular buffer
            const int startPos = impl.writePos - winLength;
            for (int j = 0; j < winLength; j++) {
                int bufIdx = ((startPos + j) % bufferSize + bufferSize) % bufferSize;
                frame[j] = impl.buffer[bufIdx];
            }

            // Process frame to get 272-dim features
            bool hasFeatures = impl.extractor.processFrame(
                frame.data(), winLength,
                features + framesProduced * featureDim
            );

            if (hasFeatures) {
                framesProduced++;
            }

            // Next frame after hopLength samples
            impl.samplesUntilNextFrame = hopLength;
        }
    }

    return framesProduced;
}

} // namespace engine
