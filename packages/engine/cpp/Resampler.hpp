#pragma once

#include <vector>

namespace engine {

/**
 * Audio resampler for sample rate conversion
 *
 * Implements 2:1 downsampling (44100 Hz → 22050 Hz) using a windowed-sinc
 * FIR low-pass filter.
 *
 * The filter cutoff is below the output Nyquist frequency to avoid aliasing.
 */
class Resampler {
public:
	/**
	 * Create resampler with default FIR low-pass filter
	 * @param inputRate Input sample rate (e.g., 44100)
	 * @param outputRate Output sample rate (e.g., 22050)
	 */
	Resampler(int inputRate = 44100, int outputRate = 22050);

	/**
	 * Resample audio buffer
	 * @param input Input samples at inputRate
	 * @param inputSize Number of input samples
	 * @param output Output buffer (must be at least getOutputSize(inputSize))
	 * @return Number of output samples written
	 */
	int process(const float* input, int inputSize, float* output);

	/**
	 * Process with streaming (maintains state between calls)
	 * @param input Input samples
	 * @param inputSize Number of input samples
	 * @param output Output buffer
	 * @param maxOutputSize Maximum output samples to write
	 * @return Number of output samples written
	 */
	int processStreaming(const float* input, int inputSize, float* output, int maxOutputSize);

	/**
	 * Calculate output size for given input size
	 */
	int getOutputSize(int inputSize) const;

	/**
	 * Reset internal state (for streaming mode)
	 */
	void reset();

	/**
	 * Get filter delay in output samples
	 */
	int getDelay() const;

	// Sample rates
	static constexpr int INPUT_RATE = 44100;
	static constexpr int OUTPUT_RATE = 22050;
	static constexpr int RATIO = 2;  // INPUT_RATE / OUTPUT_RATE

private:
	int inputRate_;
	int outputRate_;
	int ratio_;

	// Windowed-sinc FIR coefficients
	std::vector<float> coefficients_;
	int filterLength_;

	// State for streaming mode
	std::vector<float> history_;
	std::vector<float> streamBuffer_;
	int historySize_;
};

/**
 * Simple linear interpolation resampler (lower quality, faster)
 *
 * Use for non-critical applications where CPU is more important than quality.
 */
class LinearResampler {
public:
	LinearResampler(int inputRate = 44100, int outputRate = 22050);

	int process(const float* input, int inputSize, float* output);
	int getOutputSize(int inputSize) const;

private:
	int inputRate_;
	int outputRate_;
	float ratio_;
};

} // namespace engine
