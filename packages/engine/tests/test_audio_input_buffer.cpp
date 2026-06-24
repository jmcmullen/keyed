#include "AudioInputBuffer.hpp"
#include "catch_amalgamated.hpp"

#include <cmath>
#include <vector>

using namespace engine;
using Catch::Approx;

TEST_CASE("AudioInputBuffer passes through matching sample rates", "[audio-input]") {
	AudioInputBuffer buffer(2048, 44100.0);
	buffer.configure(44100.0);

	std::vector<float> input(1000);
	for (int i = 0; i < static_cast<int>(input.size()); i++) {
		input[i] = static_cast<float>(i) / 1000.0f;
	}

	REQUIRE(buffer.writeMono(input.data(), static_cast<int>(input.size())) == 1000);

	std::vector<float> output(1000);
	REQUIRE(buffer.read(output.data(), static_cast<int>(output.size())) == 1000);

	for (int i = 0; i < static_cast<int>(output.size()); i++) {
		REQUIRE(output[i] == Approx(input[i]));
	}

	const auto stats = buffer.stats();
	REQUIRE(stats.writes == 1);
	REQUIRE(stats.reads == 1);
	REQUIRE(stats.droppedFrames == 0);
	REQUIRE(stats.queuedFrames == 0);
}

TEST_CASE("AudioInputBuffer keeps recent samples on overflow", "[audio-input]") {
	AudioInputBuffer buffer(4, 44100.0);
	buffer.configure(44100.0);

	const float input[] = {0, 1, 2, 3, 4, 5};
	REQUIRE(buffer.writeMono(input, 6) == 4);

	float output[4] = {};
	REQUIRE(buffer.read(output, 4) == 4);
	REQUIRE(output[0] == Approx(2));
	REQUIRE(output[1] == Approx(3));
	REQUIRE(output[2] == Approx(4));
	REQUIRE(output[3] == Approx(5));
	REQUIRE(buffer.stats().droppedFrames == 2);
}

TEST_CASE("AudioInputBuffer resamples streaming input", "[audio-input][resample]") {
	constexpr double sourceRate = 48000.0;
	constexpr double targetRate = 44100.0;
	constexpr double tone = 440.0;
	AudioInputBuffer buffer(96000, targetRate);
	buffer.configure(sourceRate);

	std::vector<float> output;
	std::vector<float> chunk(960);
	std::vector<float> tmp(2048);

	for (int part = 0; part < 50; part++) {
		for (int i = 0; i < static_cast<int>(chunk.size()); i++) {
			const double t = static_cast<double>(part * chunk.size() + i) / sourceRate;
			chunk[i] = static_cast<float>(std::sin(2.0 * M_PI * tone * t));
		}

		REQUIRE(buffer.writeMono(chunk.data(), static_cast<int>(chunk.size())) == static_cast<int>(chunk.size()));

		while (true) {
			const int count = buffer.read(tmp.data(), static_cast<int>(tmp.size()));
			if (count == 0) {
				break;
			}
			output.insert(output.end(), tmp.begin(), tmp.begin() + count);
		}
	}

	INFO("Output size: " << output.size());
	REQUIRE(output.size() > 44000);
	REQUIRE(output.size() <= 44100);

	int crossings = 0;
	for (size_t i = 1; i < output.size(); i++) {
		if ((output[i] >= 0) != (output[i - 1] >= 0)) {
			crossings++;
		}
	}

	const double duration = static_cast<double>(output.size()) / targetRate;
	const double expected = tone * 2.0 * duration;
	REQUIRE(static_cast<double>(crossings) > expected * 0.95);
	REQUIRE(static_cast<double>(crossings) < expected * 1.05);
	REQUIRE(buffer.stats().resampleReads > 0);
}
