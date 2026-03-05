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
	err: string;
};

type SaveReady = {
	ok: true;
	row: NewDetection;
};

export type SaveDecision = SaveSkip | SaveReady;

export function bpmConfidence(frames: number): number {
	if (frames <= 0) return 0;
	if (frames >= 250) return 1;
	return frames / 250;
}

export function buildSave(input: SaveInput): SaveDecision {
	if (!input.startedAt) {
		return { ok: false, err: "" };
	}
	if (!input.result?.bpm || !input.key) {
		return { ok: false, err: "Session not saved: requires both BPM and key" };
	}
	const duration = Math.max(
		1,
		Math.round((input.now - input.startedAt) / 1000),
	);
	return {
		ok: true,
		row: {
			bpm: Math.round(input.result.bpm),
			bpmConfidence: bpmConfidence(input.result.frameCount),
			key: input.key.notation,
			keyConfidence: input.key.confidence,
			camelotCode: input.key.camelot,
			duration,
			createdAt: new Date(input.now),
		},
	};
}
