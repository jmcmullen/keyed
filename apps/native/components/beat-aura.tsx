import { addEngineListener } from "@keyed/engine";
import {
	Blur,
	Canvas,
	Group,
	Paint,
	Path,
	RadialGradient,
	Skia,
	vec,
} from "@shopify/react-native-skia";
import { useEffect } from "react";
import type { SharedValue } from "react-native-reanimated";
import {
	useDerivedValue,
	useFrameCallback,
	useSharedValue,
} from "react-native-reanimated";
import { StyleSheet } from "react-native-unistyles";

const FRAME = 324;
const SIZE = 520;
const CENTER = SIZE / 2;
const CORE = vec(CENTER, CENTER);
const TAU = Math.PI * 2;
const TOP = -Math.PI / 2;

const BARS = 72;
const HALF = BARS / 2;
const RIN = 104; // inner radius, just outside the button rim
const BARMIN = 4;
const BARSPAN = 66;
const WIDTH = 5;

// Loudness mapping: lift raw rms and clip the noise floor so silence reads as a
// thin, dim ring instead of a full one.
const GAIN = 26;
const FLOOR = 0.06;

export type BeatAuraPalette = {
	idle: [string, string, string];
	pulse: [string, string, string];
	drop: [string, string, string];
	flash: [string, string, string];
};

function zeros(n: number): number[] {
	"worklet";
	const a: number[] = [];
	for (let i = 0; i < n; i++) a.push(0);
	return a;
}

export function BeatAura({
	active,
	beat,
	down,
	listening,
	palette,
}: {
	active: SharedValue<number>;
	beat: SharedValue<number>;
	down: SharedValue<number>;
	listening: boolean;
	palette: BeatAuraPalette;
}) {
	const signal = useSharedValue<number[]>([]);
	const lo = useSharedValue(0.33);
	const mi = useSharedValue(0.33);
	const hi = useSharedValue(0.34);
	const rms = useSharedValue(0);
	const peak = useSharedValue(0.01);
	const vol = useSharedValue(0);
	const heights = useSharedValue<number[]>(zeros(BARS));

	// Feed the visualiser straight from the audio listener into shared values,
	// so new frames never re-render React or ride the JS thread.
	useEffect(() => {
		const sub = addEngineListener("onWaveform", (e) => {
			signal.set(e.samples);
			lo.set(e.low);
			mi.set(e.mid);
			hi.set(e.high);
			rms.set(e.rms);
		});
		return () => sub.remove();
	}, [signal, lo, mi, hi, rms]);

	const frame = useFrameCallback(() => {
		"worklet";
		const raw = Math.min(1, rms.get() * GAIN);
		const target = Math.max(0, (raw - FLOOR) / (1 - FLOOR));
		const value = vol.get();
		vol.set(value + (target - value) * (target > value ? 0.35 : 0.08));

		const s = signal.get();
		const n = s.length;
		if (n === 0) return;

		let pk = 0;
		for (let i = 0; i < n; i++) {
			const a = Math.abs(s[i]);
			if (a > pk) pk = a;
		}
		const current = peak.get();
		const nextPeak = pk > current ? pk : current * 0.9 + pk * 0.1;
		peak.set(nextPeak);
		const gain = 1 / Math.max(nextPeak, 0.01);

		const next = heights.get().slice();
		for (let k = 0; k < HALF; k++) {
			const start = Math.floor((n * k) / HALF);
			const end = Math.floor((n * (k + 1)) / HALF);
			let sum = 0;
			let c = 0;
			for (let j = start; j < end; j++) {
				sum += Math.abs(s[j]);
				c += 1;
			}
			// Weight by where this bucket sits: bass swells the lower bars, highs
			// shimmer the upper ones, so the ring reads as a spectrum.
			const f = k / HALF;
			const band = lo.get() * (1 - f) + mi.get() * 0.8 + hi.get() * f;
			const amp = Math.min(
				1,
				(c > 0 ? (sum / c) * gain : 0) * (0.6 + 0.9 * band),
			);

			for (const idx of [k, BARS - 1 - k]) {
				const prev = next[idx];
				next[idx] = prev + (amp - prev) * (amp > prev ? 0.4 : 0.12);
			}
		}

		// Blend each bar toward its neighbours so the ring flows as a contour
		// rather than a row of spikes.
		const out = next.slice();
		for (let i = 0; i < BARS; i++) {
			const lft = next[(i - 1 + BARS) % BARS];
			const rgt = next[(i + 1) % BARS];
			out[i] = next[i] * 0.7 + (lft + rgt) * 0.15;
		}
		heights.set(out);
	}, false);

	useEffect(() => {
		frame.setActive(listening);
	}, [frame, listening]);

	const bars = useDerivedValue(() => {
		const h = heights.get();
		const hit = beat.get();
		const drop = down.get();
		const punch = hit * 10 + drop * 16;
		const p = Skia.Path.Make();
		for (let i = 0; i < BARS; i++) {
			const a = (i / BARS) * TAU + TOP;
			const len = BARMIN + h[i] * BARSPAN * (1 + hit * 0.4) + punch;
			const cos = Math.cos(a);
			const sin = Math.sin(a);
			p.moveTo(CENTER + cos * RIN, CENTER + sin * RIN);
			p.lineTo(CENTER + cos * (RIN + len), CENTER + sin * (RIN + len));
		}
		return p;
	});

	const disc = useDerivedValue(() => {
		const r = 96 + vol.get() * 22 + beat.get() * 10 + down.get() * 16;
		const p = Skia.Path.Make();
		p.addCircle(CENTER, CENTER, r);
		return p;
	});
	const coreO = useDerivedValue(
		() =>
			active.get() *
			Math.min(
				0.7,
				0.2 + vol.get() * 0.45 + beat.get() * 0.2 + down.get() * 0.3,
			),
	);
	const glowO = useDerivedValue(
		() =>
			active.get() * Math.min(0.6, 0.14 + vol.get() * 0.4 + beat.get() * 0.3),
	);
	const barsO = useDerivedValue(
		() =>
			active.get() * Math.min(0.96, 0.25 + vol.get() * 0.7 + beat.get() * 0.2),
	);

	// Hub the bars sit on, so they read as one instrument. Flashes on downbeats.
	const ring = Skia.Path.Make();
	ring.addCircle(CENTER, CENTER, RIN);
	const ringO = useDerivedValue(
		() =>
			active.get() * Math.min(0.55, 0.12 + vol.get() * 0.2 + down.get() * 0.4),
	);

	return (
		<Canvas pointerEvents="none" style={styles.aura}>
			<Group
				blendMode="screen"
				opacity={coreO}
				layer={
					<Paint>
						<Blur blur={10} />
					</Paint>
				}
			>
				<Path path={disc}>
					<RadialGradient
						c={CORE}
						r={150}
						colors={[palette.pulse[0], palette.drop[0], "transparent"]}
						positions={[0, 0.5, 1]}
					/>
				</Path>
			</Group>

			<Group blendMode="screen" opacity={ringO}>
				<Path
					path={ring}
					style="stroke"
					strokeWidth={2}
					color={palette.drop[0]}
				/>
			</Group>

			<Group
				blendMode="screen"
				opacity={glowO}
				layer={
					<Paint>
						<Blur blur={12} />
					</Paint>
				}
			>
				<Path
					path={bars}
					style="stroke"
					strokeWidth={WIDTH * 2.4}
					strokeCap="round"
				>
					<RadialGradient
						c={CORE}
						r={200}
						colors={[
							palette.flash[0],
							palette.pulse[0],
							palette.drop[0],
							"transparent",
						]}
						positions={[0, 0.5, 0.9, 1]}
					/>
				</Path>
			</Group>

			<Group blendMode="screen" opacity={barsO}>
				<Path path={bars} style="stroke" strokeWidth={WIDTH} strokeCap="round">
					<RadialGradient
						c={CORE}
						r={200}
						colors={[
							palette.flash[0],
							palette.pulse[0],
							palette.drop[0],
							"transparent",
						]}
						positions={[0, 0.5, 0.9, 1]}
					/>
				</Path>
			</Group>
		</Canvas>
	);
}

const styles = StyleSheet.create(() => ({
	aura: {
		position: "absolute",
		left: (FRAME - SIZE) / 2,
		top: (FRAME - SIZE) / 2,
		width: SIZE,
		height: SIZE,
	},
}));
