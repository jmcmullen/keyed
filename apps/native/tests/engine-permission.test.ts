import { describe, expect, it } from "bun:test";
import { coercePermission, resolvePermission } from "../lib/engine-permission";

describe("coercePermission", () => {
	it("handles booleans", () => {
		expect(coercePermission(true)).toBe(true);
		expect(coercePermission(false)).toBe(false);
	});

	it("rejects truthy non-boolean payloads", () => {
		expect(coercePermission({ granted: false })).toBe(false);
		expect(coercePermission({ granted: "yes" })).toBe(false);
		expect(coercePermission({})).toBe(false);
	});
});

describe("resolvePermission", () => {
	it("short-circuits granted state", async () => {
		let calls = 0;
		const result = await resolvePermission("granted", () => {
			calls += 1;
			return Promise.resolve(false);
		});
		expect(calls).toBe(0);
		expect(result).toEqual({ granted: true, err: "" });
	});

	it("maps legacy object payloads to strict booleans", async () => {
		const denied = await resolvePermission("denied", () =>
			Promise.resolve({ granted: false }),
		);
		expect(denied).toEqual({ granted: false, err: "" });

		const allowed = await resolvePermission("denied", () =>
			Promise.resolve(true),
		);
		expect(allowed).toEqual({ granted: true, err: "" });
	});

	it("handles request failures", async () => {
		const result = await resolvePermission("undetermined", () =>
			Promise.reject(new Error("boom")),
		);
		expect(result).toEqual({ granted: false, err: "boom" });
	});
});
