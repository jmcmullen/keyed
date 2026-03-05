import { describe, expect, it } from "bun:test";
import { bpmConfidence, buildSave } from "../lib/session-save";

describe("bpmConfidence", () => {
	it("clamps to expected range", () => {
		expect(bpmConfidence(0)).toBe(0);
		expect(bpmConfidence(125)).toBe(0.5);
		expect(bpmConfidence(250)).toBe(1);
		expect(bpmConfidence(999)).toBe(1);
	});
});

describe("buildSave", () => {
	it("skips when session start is missing", () => {
		const result = buildSave({
			now: 10_000,
			startedAt: null,
			result: null,
			key: null,
		});
		expect(result.ok).toBe(false);
		if (!result.ok) {
			expect(result.err).toBe("Session not saved: start time missing");
		}
	});

	it("skips incomplete detections with explicit reason", () => {
		const result = buildSave({
			now: 10_000,
			startedAt: 5_000,
			result: null,
			key: null,
		});
		expect(result.ok).toBe(false);
		if (!result.ok) {
			expect(result.err).toBe("Session not saved: requires BPM");
		}
	});

	it("builds a persisted row when key is not ready", () => {
		const result = buildSave({
			now: 10_000,
			startedAt: 5_000,
			result: {
				bpm: 128.2,
				frameCount: 300,
				beatActivation: 0.2,
				downbeatActivation: 0.1,
			},
			key: null,
		});
		expect(result.ok).toBe(true);
		if (result.ok) {
			expect(result.row.bpm).toBe(128);
			expect(result.row.key).toBe("Unknown");
			expect(result.row.keyConfidence).toBe(0);
			expect(result.row.camelotCode).toBe("--");
		}
	});

	it("builds a persisted row for complete detections", () => {
		const result = buildSave({
			now: 20_000,
			startedAt: 12_000,
			result: {
				bpm: 127.6,
				frameCount: 200,
				beatActivation: 0.5,
				downbeatActivation: 0.3,
			},
			key: { notation: "Am", camelot: "8A", confidence: 0.91, timestamp: 17.2 },
		});
		expect(result.ok).toBe(true);
		if (result.ok) {
			expect(result.row.bpm).toBe(128);
			expect(result.row.bpmConfidence).toBe(0.8);
			expect(result.row.key).toBe("Am");
			expect(result.row.keyConfidence).toBe(0.91);
			expect(result.row.camelotCode).toBe("8A");
			expect(result.row.duration).toBe(8);
			expect(result.row.createdAt.toISOString()).toBe(
				new Date(20_000).toISOString(),
			);
		}
	});
});
