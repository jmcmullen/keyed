import { describe, expect, it } from "bun:test";
import { tapClose, tapInit, tapNext } from "../lib/tap-tempo";

describe("tapNext", () => {
	it("computes bpm from valid intervals", () => {
		let state = tapInit();
		state = tapNext(state, 1_000);
		state = tapNext(state, 1_500);
		state = tapNext(state, 2_000);
		expect(state.bpm === null).toBe(false);
		if (state.bpm) {
			expect(Math.round(state.bpm)).toBe(120);
		}
	});

	it("resets intervals after long gap", () => {
		let state = tapInit();
		state = tapNext(state, 1_000);
		state = tapNext(state, 1_500);
		state = tapNext(state, 5_000);
		expect(state.gaps.length).toBe(0);
	});

	it("ignores too-fast intervals", () => {
		let state = tapInit();
		state = tapNext(state, 1_000);
		state = tapNext(state, 1_100);
		expect(state.gaps.length).toBe(0);
	});
});

describe("tapClose", () => {
	it("keeps computed bpm but closes active tap mode", () => {
		let state = tapInit();
		state = tapNext(state, 1_000);
		state = tapNext(state, 1_500);
		const next = tapClose(state);
		expect(next.active).toBe(false);
		expect(next.last).toBe(null);
		expect(next.bpm).toBe(state.bpm);
	});
});
