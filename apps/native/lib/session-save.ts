import type { NewDetection } from "@keyed/db";
import type { BeatNetResult, KeyState } from "@/hooks/use-engine";

export interface SaveInput {
	now: number;
	startedAt: number | null;
	result: BeatNetResult | null;
	key: KeyState | null;
}

type SaveSkip = {
	ok: false;
};

type SaveReady = {
	ok: true;
	row: NewDetection;
};

export type SaveDecision = SaveSkip | SaveReady;

export function buildSave(input: SaveInput): SaveDecision {
	if (input.startedAt === null) {
		return { ok: false };
	}
	if (!input.result?.bpm) {
		return { ok: false };
	}
	const duration = Math.max(
		1,
		Math.round((input.now - input.startedAt) / 1000),
	);
	const key = input.key;
	return {
		ok: true,
		row: {
			bpm: input.result.bpm,
			bpmConfidence: input.result.bpmConfidence,
			key: key?.notation || "Unknown",
			keyConfidence: key?.confidence || 0,
			camelotCode: key?.camelot || "--",
			duration,
			createdAt: new Date(input.now),
		},
	};
}
