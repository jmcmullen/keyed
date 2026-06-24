import type {
	BeatNetResult,
	DetectionStatus,
	KeyState,
} from "@/hooks/use-engine";

const READY = "READY";
const ERROR = "ERROR";
const LOADING = "LOADING";
const LISTENING = "LISTENING";
const ANALYZING = "ANALYZING";
const WORKING = "WORKING";

export function buttonText(listening: boolean): string {
	if (listening) return "STOP";
	return "START";
}

export function statusText(
	status: DetectionStatus,
	busy: boolean,
	listening: boolean,
	result: BeatNetResult | null,
	key: KeyState | null,
): string {
	if (busy) return WORKING;
	if (listening) {
		if (!result?.bpm && !key) return LISTENING;
		return ANALYZING;
	}
	if (status === "initializing") return LOADING;
	if (status === "error") return ERROR;
	return READY;
}

export function shouldReset(status: DetectionStatus): boolean {
	return status === "detected" || status === "error";
}
