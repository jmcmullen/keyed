#include "catch_amalgamated.hpp"
#include "KeyModel.hpp"
#include "KeySmoother.hpp"

using namespace engine;

static KeySmootherInput key(int idx, float confidence, float margin, KeyWindow window) {
	KeySmootherInput input;
	input.keyIndex = idx;
	input.confidence = confidence;
	input.margin = margin;
	input.camelot = KeyModel::CAMELOT_KEYS[idx];
	input.notation = KeyModel::NOTATION_KEYS[idx];
	input.window = window;
	return input;
}

TEST_CASE("KeySmoother accepts early provisional keys", "[key][smoother]") {
	KeySmoother smoother;

	const auto result = smoother.push(key(7, 0.30f, 0.10f, KeyWindow::Fast), 25);

	REQUIRE(result.valid);
	REQUIRE(result.keyIndex == 7);
	REQUIRE(result.camelot == "8A");
	REQUIRE(result.confidence > 0.30f);
}

TEST_CASE("KeySmoother resists weak one-off switches", "[key][smoother]") {
	KeySmoother smoother;

	auto result = smoother.push(key(7, 0.55f, 0.20f, KeyWindow::Live), 50);
	REQUIRE(result.valid);
	REQUIRE(result.keyIndex == 7);

	result = smoother.push(key(8, 0.24f, 0.04f, KeyWindow::Fast), 55);

	REQUIRE(result.valid);
	REQUIRE(result.keyIndex == 7);
}

TEST_CASE("KeySmoother switches when recent live evidence wins", "[key][smoother]") {
	KeySmoother smoother;

	auto result = smoother.push(key(7, 0.55f, 0.20f, KeyWindow::Live), 50);
	REQUIRE(result.keyIndex == 7);

	result = smoother.push(key(8, 0.35f, 0.10f, KeyWindow::Fast), 55);
	REQUIRE(result.keyIndex == 7);

	result = smoother.push(key(8, 0.55f, 0.20f, KeyWindow::Live), 60);

	REQUIRE(result.valid);
	REQUIRE(result.keyIndex == 8);
	REQUIRE(result.camelot == "9A");
}

TEST_CASE("KeySmoother lets a new mix override old stable context", "[key][smoother]") {
	KeySmoother smoother;

	auto result = smoother.push(key(7, 0.80f, 0.30f, KeyWindow::Stable), 100);
	REQUIRE(result.keyIndex == 7);

	result = smoother.push(key(8, 0.60f, 0.20f, KeyWindow::Fast), 105);
	result = smoother.push(key(8, 0.60f, 0.20f, KeyWindow::Live), 105);

	REQUIRE(result.valid);
	REQUIRE(result.keyIndex == 8);
}
