import { Canvas, Path, Skia } from "@shopify/react-native-skia";
import { useEffect, useMemo, useRef } from "react";
import {
	runOnJS,
	type SharedValue,
	useAnimatedReaction,
	useDerivedValue,
	useSharedValue,
} from "react-native-reanimated";
import { StyleSheet, useUnistyles } from "react-native-unistyles";

interface WaveformProps {
	samples: SharedValue<number[]>;
	width: number;
	height: number;
	beatActivation?: SharedValue<number>;
	downbeatActivation?: SharedValue<number>;
	isActive?: boolean;
}

const SAMPLE_COUNT = 128;
const BEAT_THRESHOLD = 0.45;
const DOWNBEAT_THRESHOLD = 0.65;

export function Waveform({
	samples,
	width,
	height,
	beatActivation,
	downbeatActivation,
	isActive = false,
}: WaveformProps) {
	const { theme } = useUnistyles();

	// Cache colors for use in worklet and debug
	const primaryColor = theme.colors.primary;
	const successColor = theme.colors.success;

	// Debug: throttled logging
	const lastLogRef = useRef(0);
	const lastColorRef = useRef("");

	// Convert isActive prop to shared value for use in derived values
	const isActiveShared = useSharedValue(isActive);
	useEffect(() => {
		isActiveShared.value = isActive;
	}, [isActive, isActiveShared]);

	// Shared values for UI thread updates
	const points = useSharedValue<number[]>(new Array(SAMPLE_COUNT).fill(0));
	const peak = useSharedValue(0.01);

	const centerY = height / 2;
	const maxAmplitude = height * 0.4;
	const stepX = width / (SAMPLE_COUNT - 1);

	// Generate flat line path for idle state
	const flatLinePath = useMemo(() => {
		const path = Skia.Path.Make();
		path.moveTo(0, centerY);
		path.lineTo(width, centerY);
		return path;
	}, [width, centerY]);

	// Process samples and update normalized points
	const updatePoints = (data: number[]) => {
		if (data.length === 0) {
			points.value = new Array(SAMPLE_COUNT).fill(0);
			return;
		}

		// Find overall peak for auto-gain
		let overallPeak = 0;
		for (let i = 0; i < data.length; i++) {
			const absVal = Math.abs(data[i]);
			if (absVal > overallPeak) overallPeak = absVal;
		}

		// Smooth peak tracking (slow decay, fast attack)
		const currentPeak = peak.value;
		peak.value =
			overallPeak > currentPeak
				? overallPeak
				: currentPeak * 0.95 + overallPeak * 0.05;

		// Normalize gain
		const gain = 1 / Math.max(peak.value, 0.01);
		const samplesPerPoint = Math.floor(data.length / SAMPLE_COUNT);

		const newPoints = new Array(SAMPLE_COUNT);
		for (let i = 0; i < SAMPLE_COUNT; i++) {
			const start = i * samplesPerPoint;
			const end = Math.min(start + samplesPerPoint, data.length);

			// Average the samples for this point
			let sum = 0;
			for (let j = start; j < end; j++) {
				sum += data[j];
			}
			const avg = sum / (end - start);
			newPoints[i] = Math.max(-1, Math.min(1, avg * gain));
		}

		points.value = newPoints;
	};

	// React to sample changes
	useAnimatedReaction(
		() => samples.value,
		(data) => {
			runOnJS(updatePoints)(data);
		},
		[],
	);

	// Debug: log on color change or every 500ms
	const logDebug = (beat: number, downbeat: number, samplesLen: number) => {
		const now = Date.now();
		const color =
			downbeat > DOWNBEAT_THRESHOLD
				? "green"
				: beat > BEAT_THRESHOLD
					? "white"
					: "primary";
		const colorChanged = color !== lastColorRef.current;
		const shouldLog = colorChanged || now - lastLogRef.current > 500;

		if (shouldLog && isActive) {
			console.log(
				`[waveform] beat=${beat.toFixed(2)} downbeat=${downbeat.toFixed(2)} color=${color} samples=${samplesLen} primaryColor=${primaryColor}`,
			);
			lastLogRef.current = now;
			lastColorRef.current = color;
		}
	};

	useAnimatedReaction(
		() => ({
			beat: beatActivation?.value ?? -1,
			downbeat: downbeatActivation?.value ?? -1,
			samples: samples.value.length,
		}),
		({ beat, downbeat, samples: samplesLen }) => {
			runOnJS(logDebug)(beat, downbeat, samplesLen);
		},
		[isActive],
	);

	// Build smooth bezier curve from points - runs on UI thread
	const waveformPath = useDerivedValue(() => {
		// Always read the shared value to ensure reactivity
		const active = isActiveShared.value;
		const pts = points.value;

		if (!active) {
			return flatLinePath;
		}

		const path = Skia.Path.Make();

		// Start at first point
		const startY = centerY - pts[0] * maxAmplitude;
		path.moveTo(0, startY);

		// Draw smooth bezier curve through all points
		for (let i = 1; i < SAMPLE_COUNT; i++) {
			const x = i * stepX;
			const y = centerY - pts[i] * maxAmplitude;

			const prevX = (i - 1) * stepX;
			const prevY = centerY - pts[i - 1] * maxAmplitude;

			// Control points for smooth curve
			const cpX = (prevX + x) / 2;

			path.cubicTo(cpX, prevY, cpX, y, x, y);
		}

		return path;
	}, [flatLinePath, centerY, maxAmplitude, stepX]);

	// Derive color based on beat/downbeat activation - runs on UI thread
	const waveformColor = useDerivedValue(() => {
		const beat = beatActivation?.value ?? 0;
		const downbeat = downbeatActivation?.value ?? 0;

		// Downbeat takes priority (green) - higher threshold to avoid noise
		if (downbeat > DOWNBEAT_THRESHOLD) {
			return successColor;
		}
		// Beat detection (white flash)
		if (beat > BEAT_THRESHOLD) {
			return "#FFFFFF";
		}
		// Default primary color
		return primaryColor;
	}, [primaryColor, successColor]);

	return (
		<Canvas style={[styles.canvas, { width, height }]}>
			<Path
				path={waveformPath}
				color={waveformColor}
				style="stroke"
				strokeWidth={2}
				strokeCap="round"
				strokeJoin="round"
			/>
		</Canvas>
	);
}

const styles = StyleSheet.create(() => ({
	canvas: {
		backgroundColor: "transparent",
	},
}));
