export interface TapState {
	active: boolean;
	last: number | null;
	gaps: number[];
	bpm: number | null;
}

const TAP_MIN_MS = 250;
const TAP_MAX_MS = 2_000;
const TAP_KEEP = 8;

export function tapInit(): TapState {
	return {
		active: false,
		last: null,
		gaps: [],
		bpm: null,
	};
}

export function tapNext(state: TapState, now: number): TapState {
	let gaps = state.gaps;
	if (state.last) {
		const gap = now - state.last;
		if (gap > TAP_MAX_MS) {
			gaps = [];
		} else if (gap >= TAP_MIN_MS) {
			gaps = [...gaps.slice(-(TAP_KEEP - 1)), gap];
		}
	}

	if (gaps.length === 0) {
		return {
			...state,
			active: true,
			last: now,
			gaps,
		};
	}

	const avg = gaps.reduce((sum, gap) => sum + gap, 0) / gaps.length;
	return {
		active: true,
		last: now,
		gaps,
		bpm: 60_000 / avg,
	};
}

export function tapClose(state: TapState): TapState {
	return {
		...state,
		active: false,
		last: null,
	};
}

export function tapReset(): TapState {
	return tapInit();
}
