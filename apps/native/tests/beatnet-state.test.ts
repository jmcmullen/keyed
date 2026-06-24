import { describe, expect, it } from "bun:test";
import { buttonText, shouldReset, statusText } from "../lib/beatnet-state";

describe("buttonText", () => {
	it("returns loading when initializing", () => {
		expect(buttonText("initializing", false, false)).toBe("LOADING");
	});

	it("returns stop when listening", () => {
		expect(buttonText("listening", false, true)).toBe("STOP");
	});
});

describe("statusText", () => {
	it("returns analyzing when listening with data", () => {
		expect(
			statusText(
				"listening",
				false,
				true,
				{
					bpm: 128,
					frameCount: 120,
					beatActivation: 0.5,
					downbeatActivation: 0.2,
				},
				null,
			),
		).toBe("ANALYZING");
	});

	it("returns error when status is error", () => {
		expect(statusText("error", false, false, null, null)).toBe("ERROR");
	});
});

describe("shouldReset", () => {
	it("resets only when ended", () => {
		expect(shouldReset("detected")).toBe(true);
		expect(shouldReset("error")).toBe(true);
		expect(shouldReset("listening")).toBe(false);
	});
});
