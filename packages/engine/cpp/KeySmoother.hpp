#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace engine {

enum class KeyWindow {
	Fast,
	Live,
	Stable,
};

struct KeySmootherInput {
	int keyIndex = -1;
	float confidence = 0.0f;
	float margin = 0.0f;
	std::string camelot;
	std::string notation;
	KeyWindow window = KeyWindow::Fast;
};

struct KeySmootherResult {
	int keyIndex = -1;
	float confidence = 0.0f;
	std::string camelot;
	std::string notation;
	bool valid = false;
};

class KeySmoother {
public:
	void reset() {
		count_ = 0;
		head_ = 0;
		pending_ = -1;
		wins_ = 0;
		result_ = {};
	}

	KeySmootherResult push(const KeySmootherInput& input, size_t frame) {
		if (input.keyIndex >= 0 && input.keyIndex < KEY_COUNT && input.confidence > 0.0f) {
			votes_[head_] = {
				input.keyIndex,
				std::clamp(input.confidence, 0.0f, 1.0f),
				std::clamp(input.margin, 0.0f, 1.0f),
				input.camelot,
				input.notation,
				input.window,
				frame,
				true,
			};
			head_ = (head_ + 1) % votes_.size();
			count_ = std::min(count_ + 1, votes_.size());
		}

		update(frame);
		return result_;
	}

	KeySmootherResult current() const {
		return result_;
	}

private:
	struct Vote {
		int keyIndex = -1;
		float confidence = 0.0f;
		float margin = 0.0f;
		std::string camelot;
		std::string notation;
		KeyWindow window = KeyWindow::Fast;
		size_t frame = 0;
		bool valid = false;
	};

	static constexpr int KEY_COUNT = 24;
	static constexpr float FPS = 5.0f;
	static constexpr float HALF_LIFE = 3.0f;
	static constexpr float MIN_SCORE = 0.03f;
	static constexpr size_t RECENT_FRAMES = 15;

	static float windowWeight(KeyWindow window) {
		if (window == KeyWindow::Live) {
			return 1.0f;
		}
		if (window == KeyWindow::Stable) {
			return 0.85f;
		}
		return 0.65f;
	}

	static float voteScore(const Vote& vote, size_t frame) {
		const float age = static_cast<float>(frame > vote.frame ? frame - vote.frame : 0) / FPS;
		const float recency = std::pow(0.5f, age / HALF_LIFE);
		const float margin = 0.5f + std::min(1.0f, vote.margin * 4.0f);
		return vote.confidence * margin * windowWeight(vote.window) * recency;
	}

	void update(size_t frame) {
		std::array<float, KEY_COUNT> scores = {};
		std::array<float, KEY_COUNT> probs = {};
		std::array<int, KEY_COUNT> recent = {};
		float total = 0.0f;

		for (size_t i = 0; i < count_; i++) {
			const Vote& vote = votes_[i];
			if (!vote.valid) {
				continue;
			}

			const float score = voteScore(vote, frame);
			if (score <= 0.0f) {
				continue;
			}

			scores[static_cast<size_t>(vote.keyIndex)] += score;
			probs[static_cast<size_t>(vote.keyIndex)] =
				std::max(probs[static_cast<size_t>(vote.keyIndex)], vote.confidence);
			total += score;

			const size_t age = frame > vote.frame ? frame - vote.frame : 0;
			if (age <= RECENT_FRAMES && vote.window != KeyWindow::Stable) {
				recent[static_cast<size_t>(vote.keyIndex)]++;
			}
		}

		int best = -1;
		float bestScore = 0.0f;
		for (int i = 0; i < KEY_COUNT; i++) {
			if (scores[static_cast<size_t>(i)] > bestScore) {
				bestScore = scores[static_cast<size_t>(i)];
				best = i;
			}
		}

		if (best < 0 || bestScore < MIN_SCORE || total <= 0.0f) {
			return;
		}

		const float share = std::clamp(bestScore / total, 0.0f, 1.0f);
		const float confidence = std::clamp(
			0.65f * probs[static_cast<size_t>(best)] + 0.35f * share,
			0.0f,
			1.0f
		);

		if (!result_.valid) {
			accept(best, confidence);
			return;
		}

		if (best == result_.keyIndex) {
			pending_ = -1;
			wins_ = 0;
			accept(best, confidence);
			return;
		}

		if (pending_ == best) {
			wins_++;
		} else {
			pending_ = best;
			wins_ = 1;
		}

		const float current = scores[static_cast<size_t>(result_.keyIndex)];
		const bool strong = bestScore > std::max(0.06f, current * 1.25f);
		const bool repeated = wins_ >= 2 || recent[static_cast<size_t>(best)] >= 2;
		if (strong || repeated) {
			pending_ = -1;
			wins_ = 0;
			accept(best, confidence);
		}
	}

	void accept(int key, float confidence) {
		for (size_t i = 0; i < count_; i++) {
			const size_t idx = (head_ + votes_.size() - 1 - i) % votes_.size();
			const Vote& vote = votes_[idx];
			if (vote.valid && vote.keyIndex == key) {
				result_.keyIndex = key;
				result_.confidence = confidence;
				result_.camelot = vote.camelot;
				result_.notation = vote.notation;
				result_.valid = true;
				return;
			}
		}
	}

	std::array<Vote, 32> votes_ = {};
	size_t count_ = 0;
	size_t head_ = 0;
	int pending_ = -1;
	int wins_ = 0;
	KeySmootherResult result_;
};

} // namespace engine
