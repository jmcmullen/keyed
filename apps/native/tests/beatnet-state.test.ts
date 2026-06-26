import { describe, expect, it } from "bun:test";
import { buttonText, shouldReset } from "../lib/beatnet-state";

describe("buttonText", () => {
	it("returns start when not listening", () => {
		expect(buttonText(false)).toBe("START");
	});

	it("returns stop when listening", () => {
		expect(buttonText(true)).toBe("STOP");
	});
});

describe("shouldReset", () => {
	it("resets only when ended", () => {
		expect(shouldReset("detected")).toBe(true);
		expect(shouldReset("error")).toBe(true);
		expect(shouldReset("listening")).toBe(false);
	});
});
