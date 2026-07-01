import { useDb } from "@keyed/db";
import { addEngineListener } from "@keyed/engine";
import { useEffect, useRef } from "react";
import { Pressable, Text, View } from "react-native";
import Animated, {
	useAnimatedStyle,
	useSharedValue,
	withSequence,
	withTiming,
} from "react-native-reanimated";
import { SafeAreaView } from "react-native-safe-area-context";
import { StyleSheet } from "react-native-unistyles";
import { scheduleOnUI } from "react-native-worklets";
import type { BeatAuraPalette } from "@/components/beat-aura";
import { BeatAura } from "@/components/beat-aura";
import { useEngine } from "@/hooks/use-engine";
import { buttonText, shouldReset } from "@/lib/beatnet-state";
import { log } from "@/lib/log";
import { buildSave } from "@/lib/session-save";

type Tone = {
	fill: string;
	border: string;
	text: string;
	aura: BeatAuraPalette;
};

const TONE: Tone = {
	fill: "#001018",
	border: "#00D4FF",
	text: "#8DEAFF",
	aura: {
		idle: [
			"rgba(141, 234, 255, 0.38)",
			"rgba(0, 212, 255, 0.18)",
			"rgba(0, 212, 255, 0)",
		],
		pulse: [
			"rgba(230, 252, 255, 1)",
			"rgba(0, 212, 255, 0.74)",
			"rgba(0, 212, 255, 0)",
		],
		drop: [
			"rgba(59, 130, 246, 1)",
			"rgba(0, 212, 255, 0.62)",
			"rgba(0, 212, 255, 0)",
		],
		flash: [
			"rgba(255, 255, 255, 0.98)",
			"rgba(0, 212, 255, 0.92)",
			"rgba(0, 212, 255, 0)",
		],
	},
};

const ALT_LOW = 100;
const ALT_HIGH = 150;

function alt(bpm: number): string {
	if (bpm <= 0) return "";
	if (bpm < ALT_LOW) return (bpm * 2).toFixed(1);
	if (bpm > ALT_HIGH) return (bpm / 2).toFixed(1);
	return "";
}

export default function BeatNetScreen() {
	const db = useDb();
	const {
		status,
		isListening,
		isBusy,
		result,
		key,
		error,
		startListening,
		stopListening,
		reset,
	} = useEngine();
	const startedAt = useRef<number | null>(null);
	const active = useSharedValue(0);
	const beat = useSharedValue(0);
	const down = useSharedValue(0);
	const colors = TONE;

	useEffect(() => {
		active.set(withTiming(isListening ? 1 : 0, { duration: 220 }));
		if (!isListening) {
			beat.set(withTiming(0, { duration: 180 }));
			down.set(withTiming(0, { duration: 220 }));
		}
	}, [active, beat, down, isListening]);

	useEffect(() => {
		const sub = addEngineListener("onVisual", (event) => {
			scheduleOnUI(
				(raw: number, deep: number) => {
					"worklet";
					const hit = Math.min(1, raw * 2.15);
					const drop = Math.min(1, deep * 3);
					if (hit >= 0.015) {
						beat.set(
							withSequence(
								withTiming(hit, { duration: 18 }),
								withTiming(hit * 0.58, { duration: 110 }),
								withTiming(0, { duration: 320 }),
							),
						);
					}
					if (drop < 0.015) return;
					down.set(
						withSequence(
							withTiming(drop, { duration: 22 }),
							withTiming(drop * 0.52, { duration: 150 }),
							withTiming(0, { duration: 470 }),
						),
					);
				},
				event.beatActivation,
				event.downbeatActivation,
			);
		});
		return () => {
			sub.remove();
		};
	}, [beat, down]);

	const buttonAnimatedStyle = useAnimatedStyle(() => {
		const on = active.get();
		const hit = Math.max(beat.get() * 0.006, down.get() * 0.01);
		return {
			transform: [{ scale: 1 + on * hit }],
		};
	});

	const readoutStyle = useAnimatedStyle(() => {
		const pulse = Math.max(beat.get(), down.get() * 1.2);
		return {
			transform: [{ scale: 1 + active.get() * pulse * 0.04 }],
		};
	});

	const handleStop = async () => {
		const stopped = stopListening();
		if (!stopped) return;
		const save = buildSave({
			now: Date.now(),
			startedAt: startedAt.current,
			result,
			key,
		});
		startedAt.current = null;
		if (!save.ok) {
			return;
		}
		try {
			await db.addDetection(save.row);
		} catch (err: unknown) {
			log.error(
				{
					action: "history.detection_save.error",
					surface: "beatnet-screen",
					duration: save.row.duration,
					bpm: save.row.bpm,
					key: save.row.key,
					camelotCode: save.row.camelotCode,
				},
				err,
			);
		}
	};

	const handlePress = async () => {
		if (isBusy || status === "initializing") return;
		if (isListening) {
			await handleStop();
			return;
		}
		if (shouldReset(status)) {
			reset();
		}
		const started = await startListening();
		if (!started) return;
		startedAt.current = Date.now();
	};

	const bpmDisplay = result?.bpm ? result.bpm.toFixed(1) : "---.-";
	const alternate = alt(result?.bpm ?? 0) || "--";
	const bpmConfidence = result?.bpm
		? `${Math.round(result.bpmConfidence * 100)}%`
		: "--";
	const keyDisplay = key?.notation ?? "--";
	const camelotDisplay = key?.camelot ?? "--";
	const keyConfidence = key ? `${Math.round(key.confidence * 100)}%` : "--";

	return (
		<SafeAreaView edges={["top"]} style={styles.container}>
			<View style={styles.readouts}>
				<View style={styles.readout}>
					<Text style={styles.label}>BPM</Text>
					<Animated.Text selectable style={[styles.bpmValue, readoutStyle]}>
						{bpmDisplay}
					</Animated.Text>
					<View style={styles.bpmAlternates}>
						<Text selectable style={styles.bpmAlternate}>
							{alternate}
						</Text>
					</View>
				</View>
				<View style={styles.readout}>
					<Text style={styles.label}>KEY</Text>
					<Animated.Text selectable style={[styles.keyValue, readoutStyle]}>
						{keyDisplay}
					</Animated.Text>
					<Text selectable style={styles.camelotValue}>
						{camelotDisplay}
					</Text>
				</View>
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

			{error ? <Text style={styles.errorText}>{error}</Text> : null}

			<View style={styles.buttonContainer}>
				<View style={styles.buttonFrame}>
					<BeatAura
						active={active}
						beat={beat}
						down={down}
						listening={isListening}
						palette={colors.aura}
					/>
					<Animated.View style={[styles.buttonCore, buttonAnimatedStyle]}>
						<Pressable
							style={({ pressed }) => [
								styles.button(colors),
								pressed && styles.buttonPressed,
								isBusy && styles.buttonDisabled,
							]}
							onPress={handlePress}
							disabled={isBusy}
						>
							<Text style={styles.buttonText(colors)}>
								{buttonText(isListening)}
							</Text>
						</Pressable>
					</Animated.View>
				</View>
			</View>
		</SafeAreaView>
	);
}

const styles = StyleSheet.create((theme) => ({
	container: {
		flex: 1,
		backgroundColor: theme.colors.surface.audio,
		paddingHorizontal: theme.spacing.md,
	},
	readouts: {
		flexDirection: "row",
		alignItems: "center",
		gap: theme.spacing.sm,
		paddingTop: theme.spacing.xxl,
		paddingBottom: theme.spacing.sm,
	},
	readout: {
		flex: 1,
		minWidth: 0,
		alignItems: "center",
	},
	label: {
		fontSize: 13,
		fontWeight: "600",
		color: theme.colors.text.secondary,
		letterSpacing: 0,
		marginBottom: 6,
	},
	bpmValue: {
		fontSize: 52,
		fontWeight: "600",
		color: theme.colors.foreground,
		lineHeight: 58,
	},
	bpmAlternates: {
		minHeight: 32,
		flexDirection: "row",
		alignItems: "center",
		justifyContent: "center",
		gap: theme.spacing.sm,
	},
	bpmAlternate: {
		fontSize: 26,
		fontWeight: "500",
		color: theme.colors.interaction.primary,
		lineHeight: 30,
	},
	keyValue: {
		fontSize: 52,
		fontWeight: "600",
		color: theme.colors.foreground,
		lineHeight: 58,
	},
	camelotValue: {
		fontSize: 26,
		fontWeight: "500",
		color: theme.colors.interaction.primary,
		lineHeight: 30,
	},
	metricsRow: {
		flexDirection: "row",
		gap: theme.spacing.sm,
		marginTop: theme.spacing.md,
	},
	metricCard: {
		flex: 1,
		paddingVertical: theme.spacing.md,
		alignItems: "center",
	},
	metricLabel: {
		fontSize: 12,
		fontWeight: "600",
		color: theme.colors.text.secondary,
		letterSpacing: 1.4,
	},
	metricValue: {
		marginTop: 4,
		fontSize: 22,
		fontWeight: "700",
		color: theme.colors.foreground,
	},
	errorText: {
		marginTop: theme.spacing.md,
		color: theme.colors.feedback.danger,
		fontSize: theme.fontSize.sm,
		textAlign: "center",
	},
	buttonContainer: {
		flex: 1,
		alignItems: "center",
		justifyContent: "center",
	},
	buttonFrame: {
		width: 324,
		height: 324,
		alignItems: "center",
		justifyContent: "center",
	},
	buttonCore: {
		borderRadius: 9999,
	},
	button: (colors: Tone) => ({
		width: 176,
		height: 176,
		borderRadius: 9999,
		borderWidth: 2,
		alignItems: "center",
		justifyContent: "center",
		backgroundColor: colors.fill,
		borderColor: colors.border,
	}),
	buttonPressed: {
		opacity: 0.85,
	},
	buttonDisabled: {
		opacity: 0.62,
	},
	buttonText: (colors: Tone) => ({
		fontSize: 30,
		fontWeight: "800",
		color: colors.text,
		letterSpacing: 4,
	}),
}));
