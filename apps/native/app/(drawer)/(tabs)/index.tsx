import { useDb } from "@keyed/db";
import { useCallback, useEffect, useRef, useState } from "react";
import {
	Platform,
	Pressable,
	Text,
	useWindowDimensions,
	View,
} from "react-native";
import Animated, {
	useAnimatedStyle,
	withRepeat,
	withTiming,
} from "react-native-reanimated";
import { StyleSheet } from "react-native-unistyles";
import { Waveform } from "@/components/waveform";
import { useEngine } from "@/hooks/use-engine";
import {
	buildSave,
	bpmConfidence as getBpmConfidence,
} from "@/lib/session-save";
import { silenceTick } from "@/lib/silence-stop";
import { tapClose, tapInit, tapNext } from "@/lib/tap-tempo";

const STATUS_READY = "READY";
const STATUS_ERROR = "ERROR";
const STATUS_LOADING = "LOADING";
const STATUS_LISTENING = "LISTENING";
const STATUS_ANALYZING = "ANALYZING";
const STATUS_WORKING = "WORKING";
const WAVEFORM_HEIGHT = 200;
const SILENCE_GATE = 0.01;
const SILENCE_MS = 5_000;
const TAP_TIMEOUT_MS = 3_000;

export default function BeatNetTab() {
	const db = useDb();
	const {
		status,
		isListening,
		isBusy,
		result,
		key,
		waveformSamples,
		beatActivation,
		downbeatActivation,
		audioLevel,
		error,
		startListening,
		stopListening,
		reset,
	} = useEngine();
	const { width: windowWidth } = useWindowDimensions();
	const [waveformResetKey, setWaveformResetKey] = useState(0);
	const [startedAt, setStartedAt] = useState<number | null>(null);
	const [saveErr, setSaveErr] = useState<string | null>(null);
	const [tap, setTap] = useState(() => tapInit());
	const silenceRef = useRef<number | null>(null);
	const tapTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

	const dotAnimatedStyle = useAnimatedStyle(
		() => ({
			opacity: isListening
				? withRepeat(withTiming(0.3, { duration: 600 }), -1, true)
				: withTiming(1, { duration: 200 }),
		}),
		[isListening],
	);

	const buttonGlowAnimatedStyle = useAnimatedStyle(() => {
		const beatBoost = beatActivation * 0.8 + downbeatActivation * 1.2;
		const pulseScale = isListening ? 1 + beatBoost * 0.06 : 1;
		const glowOpacity = isListening ? 0.2 + beatBoost * 0.45 : 0.12;
		const glowRadius = isListening ? 10 + beatBoost * 20 : 8;
		const clampedGlowOpacity = Math.max(0, Math.min(glowOpacity, 1));
		const androidElevation = isListening ? 4 + beatBoost * 8 : 2;

		return {
			transform: [{ scale: pulseScale }],
			shadowOpacity: clampedGlowOpacity,
			shadowRadius: glowRadius,
			elevation: Platform.OS === "android" ? androidElevation : 0,
		};
	}, [isListening, beatActivation, downbeatActivation]);

	const clearTapTimer = useCallback(() => {
		if (!tapTimerRef.current) return;
		clearTimeout(tapTimerRef.current);
		tapTimerRef.current = null;
	}, []);

	const closeTap = useCallback(() => {
		setTap((curr) => tapClose(curr));
	}, []);

	const onTap = useCallback(() => {
		setTap((curr) => tapNext(curr, Date.now()));
		clearTapTimer();
		tapTimerRef.current = setTimeout(() => {
			closeTap();
		}, TAP_TIMEOUT_MS);
	}, [clearTapTimer, closeTap]);

	const handleStop = useCallback(async () => {
		const stopped = stopListening();
		if (!stopped) return;
		silenceRef.current = null;
		setStartedAt(null);
		const save = buildSave({
			now: Date.now(),
			startedAt,
			result,
			key,
		});
		if (!save.ok) {
			if (save.err) setSaveErr(save.err);
			return;
		}
		try {
			await db.addDetection(save.row);
		} catch (err: unknown) {
			const message =
				err instanceof Error ? err.message : "Failed to save detection";
			setSaveErr(message);
		}
	}, [stopListening, startedAt, result, key, db]);

	useEffect(() => {
		const now = Date.now();
		const step = silenceTick(
			isListening,
			audioLevel,
			now,
			silenceRef.current,
			SILENCE_GATE,
			SILENCE_MS,
		);
		silenceRef.current = step.since;
		if (!step.stop) return;
		void handleStop();
	}, [isListening, audioLevel, handleStop]);

	useEffect(() => {
		if (isListening) return;
		silenceRef.current = null;
	}, [isListening]);

	useEffect(() => {
		return () => {
			clearTapTimer();
		};
	}, [clearTapTimer]);

	const handlePress = async () => {
		if (isBusy) return;
		setSaveErr(null);
		if (isListening) {
			await handleStop();
			return;
		}
		if (status === "detected" || status === "error") {
			reset();
		}
		setWaveformResetKey((k) => k + 1);
		const started = await startListening();
		if (!started) return;
		silenceRef.current = null;
		setStartedAt(Date.now());
	};

	const getButtonText = () => {
		if (status === "initializing") return STATUS_LOADING;
		if (isBusy) return "WAIT";
		if (isListening) return "STOP";
		return "START";
	};

	const getStatusText = () => {
		if (isBusy) return STATUS_WORKING;
		if (isListening) {
			if (!result?.bpm && !key) return STATUS_LISTENING;
			return STATUS_ANALYZING;
		}
		if (status === "initializing") return STATUS_LOADING;
		if (status === "error") return STATUS_ERROR;
		return STATUS_READY;
	};

	const manual = tap.active || (!result?.bpm && tap.bpm);
	const shownBpm = tap.active ? tap.bpm : result?.bpm || tap.bpm;
	const bpmDisplay = shownBpm ? shownBpm.toFixed(1) : "---.-";
	const bpmConfidence = manual
		? "MANUAL"
		: result?.bpm
			? `${Math.round(getBpmConfidence(result.frameCount) * 100)}%`
			: "--";
	const keyDisplay = key?.notation ?? "--";
	const camelotDisplay = key?.camelot ?? "--";
	const keyConfidence = key ? `${Math.round(key.confidence * 100)}%` : "--";
	const message = error || saveErr;

	return (
		<View style={styles.container}>
			<View style={styles.statusRow}>
				<View style={styles.statusIndicator}>
					<Animated.View
						style={[styles.statusDot(status, isListening), dotAnimatedStyle]}
					/>
					<Text style={styles.statusText}>{getStatusText()}</Text>
				</View>
			</View>

			<View style={styles.bpmSection}>
				<Text style={styles.bpmLabel}>BPM</Text>
				<Text style={styles.bpmValue}>{bpmDisplay}</Text>
				{tap.active ? (
					<Pressable onPress={onTap} style={styles.tapZone}>
						<Text style={styles.tapZoneLabel}>TAP</Text>
						<Text style={styles.tapZoneBpm}>
							{tap.bpm ? `${tap.bpm.toFixed(1)} BPM` : "..."}
						</Text>
					</Pressable>
				) : (
					<Pressable onPress={onTap} style={styles.tapBtn}>
						<Text style={styles.tapBtnTxt}>Tap tempo</Text>
					</Pressable>
				)}
			</View>

			<View style={styles.keySection}>
				<Text style={styles.keyLabel}>KEY</Text>
				<Text style={styles.keyValue}>{keyDisplay}</Text>
				<Text style={styles.camelotValue}>{camelotDisplay}</Text>
			</View>

			<View style={styles.metricsRow}>
				<View style={styles.metricCard}>
					<Text style={styles.metricLabel}>BPM CONF</Text>
					<Text style={styles.metricValue}>{bpmConfidence}</Text>
				</View>
				<View style={styles.metricCard}>
					<Text style={styles.metricLabel}>KEY CONF</Text>
					<Text style={styles.metricValue}>{keyConfidence}</Text>
				</View>
			</View>

			{isListening && (
				<View style={styles.waveformContainer}>
					<Waveform
						samples={waveformSamples}
						width={windowWidth - 32}
						height={WAVEFORM_HEIGHT}
						isActive={isListening}
						resetKey={waveformResetKey}
					/>
				</View>
			)}

			{message && <Text style={styles.errorText}>{message}</Text>}

			<View style={styles.buttonContainer}>
				<Animated.View
					style={[
						styles.buttonGlow,
						isListening ? styles.buttonGlowActive : styles.buttonGlowIdle,
						buttonGlowAnimatedStyle,
					]}
				>
					<Pressable
						style={({ pressed }) => [
							styles.button,
							isListening ? styles.buttonActive : styles.buttonIdle,
							pressed && styles.buttonPressed,
							(status === "initializing" || isBusy) && styles.buttonDisabled,
						]}
						onPress={handlePress}
						disabled={status === "initializing" || isBusy}
					>
						<View style={styles.buttonInnerRing}>
							<Text
								style={[
									styles.buttonText,
									isListening && styles.buttonTextActive,
								]}
							>
								{getButtonText()}
							</Text>
						</View>
					</Pressable>
				</Animated.View>
			</View>
		</View>
	);
}

const styles = StyleSheet.create((theme) => ({
	container: {
		flex: 1,
		backgroundColor: theme.colors.surface.audio,
		paddingHorizontal: theme.spacing.md,
	},
	statusRow: {
		paddingTop: theme.spacing.md,
		paddingBottom: theme.spacing.sm,
	},
	statusIndicator: {
		flexDirection: "row",
		alignItems: "center",
		gap: 8,
	},
	statusDot: (status: string, isListening: boolean) => ({
		width: 8,
		height: 8,
		borderRadius: 4,
		backgroundColor:
			status === "error"
				? theme.colors.status.error
				: isListening
					? theme.colors.status.listening
					: theme.colors.status.ready,
	}),
	statusText: {
		fontSize: 11,
		fontWeight: "600",
		color: theme.colors.text.muted,
		letterSpacing: 1.5,
	},
	bpmSection: {
		alignItems: "center",
		paddingTop: theme.spacing.lg,
	},
	bpmLabel: {
		fontSize: 11,
		fontWeight: "600",
		color: theme.colors.text.secondary,
		letterSpacing: 2,
		marginBottom: 4,
	},
	bpmValue: {
		fontSize: 72,
		fontWeight: "300",
		color: theme.colors.foreground,
		fontVariant: ["tabular-nums"],
		letterSpacing: -2,
	},
	tapBtn: {
		marginTop: theme.spacing.sm,
		paddingHorizontal: theme.spacing.md,
		paddingVertical: 6,
		borderRadius: theme.borderRadius.md,
		borderWidth: 1,
		borderColor: theme.colors.interaction.primary,
		backgroundColor: theme.colors.surface.overlayStrong,
	},
	tapBtnTxt: {
		fontSize: 13,
		fontWeight: "600",
		color: theme.colors.interaction.primary,
	},
	tapZone: {
		marginTop: theme.spacing.sm,
		width: "100%",
		maxWidth: 300,
		alignItems: "center",
		paddingVertical: theme.spacing.md,
		borderRadius: theme.borderRadius.lg,
		borderWidth: 1,
		borderColor: theme.colors.interaction.primaryActive,
		backgroundColor: theme.colors.surface.overlayStrong,
	},
	tapZoneLabel: {
		fontSize: 11,
		fontWeight: "700",
		letterSpacing: 2,
		color: theme.colors.interaction.primaryActive,
	},
	tapZoneBpm: {
		marginTop: 4,
		fontSize: 20,
		fontWeight: "700",
		color: theme.colors.foreground,
		fontVariant: ["tabular-nums"],
	},
	keySection: {
		alignItems: "center",
		paddingTop: theme.spacing.md,
	},
	keyLabel: {
		fontSize: 11,
		fontWeight: "600",
		color: theme.colors.text.secondary,
		letterSpacing: 2,
		marginBottom: 2,
	},
	keyValue: {
		fontSize: 44,
		fontWeight: "600",
		color: theme.colors.foreground,
		lineHeight: 48,
	},
	camelotValue: {
		fontSize: 20,
		fontWeight: "500",
		color: theme.colors.interaction.primary,
	},
	metricsRow: {
		flexDirection: "row",
		gap: theme.spacing.sm,
		marginTop: theme.spacing.md,
	},
	metricCard: {
		flex: 1,
		borderRadius: theme.borderRadius.md,
		backgroundColor: theme.colors.surface.overlayStrong,
		paddingVertical: theme.spacing.sm,
		alignItems: "center",
	},
	metricLabel: {
		fontSize: 10,
		fontWeight: "600",
		color: theme.colors.text.secondary,
		letterSpacing: 1.4,
	},
	metricValue: {
		marginTop: 2,
		fontSize: 16,
		fontWeight: "700",
		color: theme.colors.foreground,
	},
	waveformContainer: {
		alignItems: "center",
		paddingVertical: theme.spacing.md,
	},
	errorText: {
		fontSize: theme.fontSize.sm,
		color: theme.colors.feedback.danger,
		textAlign: "center",
		marginBottom: theme.spacing.md,
	},
	buttonContainer: {
		marginTop: theme.spacing.md,
		alignItems: "center",
		justifyContent: "center",
		paddingBottom: theme.spacing.xl,
	},
	buttonGlow: {
		borderRadius: 9999,
		shadowColor: theme.colors.interaction.primary,
		shadowOffset: { width: 0, height: 0 },
	},
	buttonGlowIdle: {
		shadowColor: theme.colors.interaction.primary,
	},
	buttonGlowActive: {
		shadowColor: theme.colors.interaction.primaryActive,
	},
	button: {
		width: 136,
		height: 136,
		borderRadius: 9999,
		borderWidth: 3,
		alignItems: "center",
		justifyContent: "center",
	},
	buttonIdle: {
		backgroundColor: theme.colors.surface.control,
		borderColor: theme.colors.interaction.primary,
	},
	buttonActive: {
		backgroundColor: theme.colors.surface.controlActive,
		borderColor: theme.colors.interaction.primaryActive,
	},
	buttonPressed: {
		opacity: 0.85,
	},
	buttonDisabled: {
		opacity: 0.4,
	},
	buttonInnerRing: {
		width: 110,
		height: 110,
		borderRadius: 9999,
		borderWidth: 2,
		borderColor: theme.colors.surface.overlaySoft,
		alignItems: "center",
		justifyContent: "center",
		backgroundColor: theme.colors.surface.overlayStrong,
	},
	buttonText: {
		fontSize: 18,
		fontWeight: "700",
		color: theme.colors.interaction.primary,
		letterSpacing: 2.4,
	},
	buttonTextActive: {
		color: theme.colors.interaction.primaryActive,
	},
}));
