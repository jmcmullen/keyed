import { describe, expect, it } from "bun:test";
import { silenceTick } from "../lib/silence-stop";

describe("silenceTick", () => {
	it("tracks silence start while listening", () => {
		const out = silenceTick(true, 0.001, 10_000, null, 0.01, 5_000);
		expect(out).toEqual({ since: 10_000, stop: false });
	});

	it("resets silence when audio is above gate", () => {
		const out = silenceTick(true, 0.02, 10_000, 9_000, 0.01, 5_000);
		expect(out).toEqual({ since: null, stop: false });
	});

	it("triggers stop after silence limit", () => {
		const out = silenceTick(true, 0.001, 15_100, 10_000, 0.01, 5_000);
		expect(out).toEqual({ since: null, stop: true });
	});

	it("stays idle when not listening", () => {
		const out = silenceTick(false, 0, 10_000, 9_000, 0.01, 5_000);
		expect(out).toEqual({ since: null, stop: false });
	});
});
