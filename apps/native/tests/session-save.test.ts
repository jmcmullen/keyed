import { describe, expect, it } from "bun:test";
import { buildSave } from "../lib/session-save";

describe("buildSave", () => {
	it("skips when session start is missing", () => {
		const result = buildSave({
			now: 10_000,
			startedAt: null,
			result: null,
			key: null,
		});
		expect(result.ok).toBe(false);
	});

	it("skips incomplete detections silently", () => {
		const result = buildSave({
			now: 10_000,
			startedAt: 5_000,
			result: null,
			key: null,
		});
		expect(result.ok).toBe(false);
	});

	it("builds a persisted row when key is not ready", () => {
		const result = buildSave({
			now: 10_000,
			startedAt: 5_000,
			result: {
				bpm: 128.2,
				bpmConfidence: 0.42,
				frameCount: 300,
				beatActivation: 0.2,
				downbeatActivation: 0.1,
			},
			key: null,
		});
		expect(result.ok).toBe(true);
		if (result.ok) {
			expect(result.row.bpm).toBe(128.2);
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
				bpmConfidence: 0.73,
				frameCount: 200,
				beatActivation: 0.5,
				downbeatActivation: 0.3,
			},
			key: { notation: "Am", camelot: "8A", confidence: 0.91, timestamp: 17.2 },
		});
		expect(result.ok).toBe(true);
		if (result.ok) {
			expect(result.row.bpm).toBe(127.6);
			expect(result.row.bpmConfidence).toBe(0.73);
			expect(result.row.key).toBe("Am");
			expect(result.row.keyConfidence).toBe(0.91);
			expect(result.row.camelotCode).toBe("8A");
			expect(result.row.duration).toBe(8);
			expect(result.row.createdAt.toISOString()).toBe(
				new Date(20_000).toISOString(),
			);
		}
	});

	it("accepts epoch start timestamps", () => {
		const result = buildSave({
			now: 8_000,
			startedAt: 0,
			result: {
				bpm: 124.8,
				bpmConfidence: 0.64,
				frameCount: 150,
				beatActivation: 0.4,
				downbeatActivation: 0.2,
			},
			key: null,
		});
		expect(result.ok).toBe(true);
		if (result.ok) {
			expect(result.row.duration).toBe(8);
		}
	});
});
