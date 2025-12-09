#pragma once

#include <cstddef>
#include <vector>
#include <cmath>
#include <memory>

namespace engine {

/**
 * Configuration matching BeatNet's mel spectrogram parameters
 * From BeatNet/src/BeatNet/log_spect.py and BeatNet.py
 */
struct MelConfig {
    static constexpr int SAMPLE_RATE = 22050;
    static constexpr int HOP_LENGTH = 441;      // 20ms at 22050Hz -> 50 FPS
    static constexpr int WIN_LENGTH = 1411;     // 64ms at 22050Hz
    static constexpr int FFT_SIZE = 1411;       // Match madmom (no zero-padding)
    static constexpr int BANDS_PER_OCTAVE = 24;
    static constexpr float F_MIN = 30.0f;
    static constexpr float F_MAX = 17000.0f;
    static constexpr int N_BANDS = 136;         // Output mel bands
    static constexpr int MODEL_INPUT_DIM = 272; // 136 mel + 136 diff
    static constexpr float F_REF = 440.0f;      // Reference frequency for log spacing
};

/**
 * Logarithmic Filterbank
 * Generates triangular filters spaced logarithmically (constant bands per octave)
 * Matches madmom's LogarithmicFilterbank with unique_filters=True
 */
class LogFilterbank {
public:
    LogFilterbank(int fftSize, int sampleRate, int bandsPerOctave,
                  float fMin, float fMax, bool normalize = true);

    /** Apply filterbank to magnitude spectrum */
    void apply(const float* magnitude, float* output) const;

    int getNumBands() const { return numBands_; }
    int getNumBins() const { return numBins_; }

private:
    int numBands_;
    int numBins_;
    std::vector<std::vector<float>> filters_;
};

/**
 * Mel Spectrogram Feature Extractor
 *
 * Processes audio frames and outputs features matching BeatNet's input format.
 * Each output frame has 272 features (136 log-mel bands + 136 spectral diff)
 *
 * Implements the preprocessing pipeline matching BeatNet's log_spect.py:
 * - STFT with Hann window
 * - Logarithmic filterbank (24 bands per octave, 30-17000 Hz) -> 136 bands
 * - Logarithmic scaling: log10(1 + S)
 * - Spectral difference with half-wave rectification
 */
class MelExtractor {
public:
    MelExtractor();
    ~MelExtractor();

    /** Reset state (call when starting a new audio stream) */
    void reset();

    /**
     * Process a single audio frame
     * @param frame Audio samples (should be WIN_LENGTH samples)
     * @param features Output feature vector (272 floats)
     * @return true if features were produced (false for first frame)
     */
    bool processFrame(const float* frame, int frameLength, float* features);

    /** Get number of output features per frame (272) */
    static constexpr int getFeatureDim() { return MelConfig::MODEL_INPUT_DIM; }

    /** Get frames per second (50) */
    static constexpr float getFps() {
        return static_cast<float>(MelConfig::SAMPLE_RATE) / MelConfig::HOP_LENGTH;
    }

private:
    // Forward declaration of implementation
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Streaming Mel Spectrogram Extractor
 *
 * Maintains a circular buffer and outputs 272-dimensional features
 * as audio is pushed. Handles hop-based frame extraction automatically.
 */
class StreamingMelExtractor {
public:
    StreamingMelExtractor();
    ~StreamingMelExtractor();

    /** Reset state */
    void reset();

    /**
     * Push audio samples and process any complete frames
     * @param samples Audio samples to add
     * @param numSamples Number of samples
     * @param features Output buffer for features (must be large enough)
     * @param maxFrames Maximum number of frames to output
     * @return Number of feature frames produced
     */
    int push(const float* samples, int numSamples,
             float* features, int maxFrames);

    /** Get number of output features per frame (272) */
    static constexpr int getFeatureDim() { return MelConfig::MODEL_INPUT_DIM; }

    /** Get frames per second (50) */
    static constexpr float getFps() { return MelExtractor::getFps(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
