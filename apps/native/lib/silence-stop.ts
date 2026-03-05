export interface SilenceStep {
	since: number | null;
	stop: boolean;
}

export function silenceTick(
	listening: boolean,
	level: number,
	now: number,
	since: number | null,
	gate: number,
	limit: number,
): SilenceStep {
	if (!listening) {
		return { since: null, stop: false };
	}
	if (level > gate) {
		return { since: null, stop: false };
	}
	if (!since) {
		return { since: now, stop: false };
	}
	if (now - since < limit) {
		return { since, stop: false };
	}
	return { since: null, stop: true };
}
