import {
	Blur,
	Canvas,
	Group,
	LinearGradient,
	Paint,
	Path,
	Skia,
	vec,
} from "@shopify/react-native-skia";
import { useEffect, useMemo } from "react";
import { View } from "react-native";
import { useDerivedValue, useSharedValue } from "react-native-reanimated";
import { StyleSheet, useUnistyles } from "react-native-unistyles";

interface WaveformProps {
	samples: number[];
	width: number;
	height: number;
	isActive?: boolean;
	resetKey?: number;
	gridMarkers?: number[];
	cuePoints?: number[];
}

const LINE_WIDTH = 1;
const LINE_GAP = 1;
const LINE_SPACING = LINE_WIDTH + LINE_GAP;
const PEAK_DECAY = 0.85;
const MIN_PEAK = 0.01;

const EMPTY_GRID_MARKERS: number[] = [];
const EMPTY_CUE_POINTS: number[] = [];

interface HistoryEntry {
	bass: number;
	mid: number;
	high: number;
}

function createPathForLayer(
	history: HistoryEntry[],
	layerKey: keyof HistoryEntry,
	centerY: number,
): ReturnType<typeof Skia.Path.Make> {
	"worklet";
	const path = Skia.Path.Make();
	const len = history.length;

	for (let i = 0; i < len; i++) {
		const val = history[i][layerKey];
		if (val < 0.5) continue;

		const x = i * LINE_SPACING;
		path.moveTo(x, centerY - val);
		path.lineTo(x, centerY + val);
	}
	return path;
}

export function Waveform({
	samples,
	width,
	height,
	isActive = false,
	resetKey = 0,
	gridMarkers = EMPTY_GRID_MARKERS,
	cuePoints = EMPTY_CUE_POINTS,
}: WaveformProps) {
	const { theme } = useUnistyles();
	const lineCount = useMemo(() => Math.floor(width / LINE_SPACING), [width]);

	const history = useSharedValue<HistoryEntry[]>(
		Array.from({ length: lineCount }, () => ({ bass: 0, mid: 0, high: 0 })),
	);
	const peak = useSharedValue(MIN_PEAK);
	const signal = useSharedValue<number[]>(samples);
	const isActiveShared = useSharedValue(isActive);

	useEffect(() => {
		isActiveShared.value = isActive;
	}, [isActive, isActiveShared]);

	useEffect(() => {
		signal.value = samples;
	}, [samples, signal]);

	// Reset on key change
	// biome-ignore lint/correctness/useExhaustiveDependencies: resetKey triggers intentional reset
	useEffect(() => {
		history.value = Array.from({ length: lineCount }, () => ({
			bass: 0,
			mid: 0,
			high: 0,
		}));
		peak.value = MIN_PEAK;
	}, [resetKey, lineCount]);

	const centerY = height / 2;
	const maxBarHeight = height * 0.45;

	useDerivedValue(() => {
		"worklet";
		if (!isActiveShared.value) return;

		const data = signal.value;
		const sampleLen = data.length;
		if (sampleLen === 0) return;

		let framePeak = 0;
		for (let i = 0; i < sampleLen; i++) {
			const absVal = Math.abs(data[i]);
			if (absVal > framePeak) framePeak = absVal;
		}

		peak.value =
			framePeak > peak.value
				? framePeak
				: peak.value * PEAK_DECAY + framePeak * (1 - PEAK_DECAY);

		const gain = 1 / Math.max(peak.value, MIN_PEAK);

		let sumAbs = 0;
		let sumDiff = 0;
		for (let i = 0; i < sampleLen; i++) {
			sumAbs += Math.abs(data[i]);
			if (i > 0) sumDiff += Math.abs(data[i] - data[i - 1]);
		}

		const avgAbs = (sumAbs / sampleLen) * gain;
		const avgDiff = (sumDiff / (sampleLen - 1)) * gain;

		const rawHigh = Math.min(1, Math.max(0, avgDiff * 5.0));
		const rawBass = Math.min(1, Math.max(0, (avgAbs - avgDiff * 0.5) * 1.5));
		const rawMid = Math.min(1, Math.max(0, avgAbs * 1.2));

		const highH = rawHigh * maxBarHeight * 0.5;
		const midH = rawMid * maxBarHeight * 0.75;
		const bassH = rawBass * maxBarHeight * 1.0;

		const currentLength = history.value.length;
		const hist = history.value.slice();
		hist.shift();
		hist.push({ bass: bassH, mid: midH, high: highH });

		if (hist.length !== currentLength) {
			history.value = Array.from({ length: currentLength }, () => ({
				bass: 0,
				mid: 0,
				high: 0,
			}));
			return;
		}
		history.value = hist;
	}, [maxBarHeight, lineCount]);

	const bassPath = useDerivedValue(
		() => createPathForLayer(history.value, "bass", centerY),
		[centerY],
	);
	const midPath = useDerivedValue(
		() => createPathForLayer(history.value, "mid", centerY),
		[centerY],
	);
	const highPath = useDerivedValue(
		() => createPathForLayer(history.value, "high", centerY),
		[centerY],
	);

	const gradientStart = vec(0, 0);
	const gradientEnd = vec(0, height);
	const waveformColors = theme.colors.visualizer.audio;

	const staticGridPath = useMemo(() => {
		const p = Skia.Path.Make();
		gridMarkers.forEach((x) => {
			p.moveTo(x, 0);
			p.lineTo(x, height);
		});
		return p;
	}, [gridMarkers, height]);

	const cuePath = useMemo(() => {
		const p = Skia.Path.Make();
		const size = 6;
		cuePoints.forEach((x) => {
			p.moveTo(x - size, height);
			p.lineTo(x + size, height);
			p.lineTo(x, height - size * 1.5);
			p.close();
		});
		return p;
	}, [cuePoints, height]);

	return (
		<View style={styles.container(width, height)}>
			<Canvas style={styles.canvas(width, height)}>
				<Paint color={theme.colors.surface.audio} />

				<Group>
					<Path
						path={staticGridPath}
						style="stroke"
						strokeWidth={1}
						color={theme.colors.visualizer.grid}
					/>
				</Group>

				<Path
					path={bassPath}
					style="stroke"
					strokeWidth={LINE_WIDTH}
					strokeCap="round"
				>
					<LinearGradient
						start={gradientStart}
						end={gradientEnd}
						colors={[
							"transparent",
							waveformColors.low,
							waveformColors.low,
							"transparent",
						]}
						positions={[0, 0.45, 0.55, 1]}
					/>
				</Path>

				<Path
					path={midPath}
					style="stroke"
					strokeWidth={LINE_WIDTH}
					strokeCap="round"
				>
					<LinearGradient
						start={gradientStart}
						end={gradientEnd}
						colors={[
							"transparent",
							waveformColors.mid,
							waveformColors.mid,
							"transparent",
						]}
						positions={[0, 0.45, 0.55, 1]}
					/>
				</Path>

				<Path
					path={highPath}
					style="stroke"
					strokeWidth={LINE_WIDTH}
					strokeCap="round"
				>
					<LinearGradient
						start={gradientStart}
						end={gradientEnd}
						colors={[
							"transparent",
							waveformColors.peak,
							waveformColors.peak,
							"transparent",
						]}
						positions={[0.3, 0.48, 0.52, 0.7]}
					/>
				</Path>

				<Group
					layer={
						<Paint>
							<Blur blur={4} />
						</Paint>
					}
					opacity={0.6}
				>
					<Path
						path={highPath}
						color={waveformColors.peak}
						style="stroke"
						strokeWidth={LINE_WIDTH * 3}
					/>
				</Group>

				<Path
					path={cuePath}
					color={theme.colors.feedback.danger}
					style="fill"
				/>
			</Canvas>
		</View>
	);
}

const styles = StyleSheet.create((theme) => ({
	container: (width: number, height: number) => ({
		width,
		height,
		backgroundColor: theme.colors.surface.audio,
		position: "relative",
	}),
	canvas: (width: number, height: number) => ({
		width,
		height,
	}),
}));
