import { useEffect, useState } from "react";
import { Pressable, Text, useWindowDimensions, View } from "react-native";
import Animated, {
	useAnimatedStyle,
	useSharedValue,
	withRepeat,
	withTiming,
} from "react-native-reanimated";
import { StyleSheet } from "react-native-unistyles";
import { Waveform } from "@/components/waveform";
import { useEngine } from "@/hooks/use-engine";

export default function BeatNetTab() {
	const {
		status,
		isListening,
		result,
		waveformSamples,
		beatActivation,
		downbeatActivation,
		error,
		startListening,
		stopListening,
		reset,
	} = useEngine();
	const { width: windowWidth } = useWindowDimensions();
	const [waveformResetKey, setWaveformResetKey] = useState(0);

	// Pulsing animation for status dot
	const pulseOpacity = useSharedValue(1);

	useEffect(() => {
		if (isListening) {
			pulseOpacity.value = withRepeat(
				withTiming(0.3, { duration: 600 }),
				-1,
				true,
			);
		} else {
			pulseOpacity.value = withTiming(1, { duration: 200 });
		}
	}, [isListening, pulseOpacity]);

	const dotAnimatedStyle = useAnimatedStyle(() => ({
		opacity: pulseOpacity.value,
	}));

	const buttonGlowAnimatedStyle = useAnimatedStyle(
		() => {
			const beatBoost = beatActivation.value * 0.8 + downbeatActivation.value * 1.2;
			const pulseScale = isListening ? 1 + beatBoost * 0.06 : 1;
			const glowOpacity = isListening ? 0.2 + beatBoost * 0.45 : 0.12;
			const glowRadius = isListening ? 10 + beatBoost * 20 : 8;

			return {
				transform: [{ scale: pulseScale }],
				shadowOpacity: glowOpacity,
				shadowRadius: glowRadius,
			};
		},
		[isListening],
	);

	const handlePress = async () => {
		if (isListening) {
			stopListening();
		} else {
			if (status === "detected" || status === "error") {
				reset();
			}
			setWaveformResetKey((k) => k + 1);
			await startListening();
		}
	};

	const getButtonText = () => {
		if (status === "initializing") return "LOADING";
		if (isListening) return "STOP";
		return "START";
	};

	const getStatusText = () => {
		if (isListening) {
			if (!result?.bpm) return "LISTENING";
			return "ANALYZING";
		}
		if (status === "initializing") return "LOADING";
		if (status === "error") return "ERROR";
		return "READY";
	};

	const getStatusColor = () => {
		if (status === "error") return "#EF4444";
		if (isListening) return "#00D4FF";
		return "#22C55E";
	};

	const bpmDisplay = result?.bpm ? result.bpm.toFixed(1) : "---.-";

	return (
		<View style={styles.container}>
			{/* Status Row */}
			<View style={styles.statusRow}>
				<View style={styles.statusIndicator}>
					<Animated.View
						style={[
							styles.statusDot,
							{ backgroundColor: getStatusColor() },
							dotAnimatedStyle,
						]}
					/>
					<Text style={styles.statusText}>{getStatusText()}</Text>
				</View>
			</View>

			{/* BPM Display */}
			<View style={styles.bpmSection}>
				<Text style={styles.bpmLabel}>BPM</Text>
				<Text style={styles.bpmValue}>{bpmDisplay}</Text>
			</View>

			{/* Waveform */}
			<View style={styles.waveformContainer}>
				<Waveform
					samples={waveformSamples}
					width={windowWidth - 32}
					height={200}
					isActive={isListening}
					resetKey={waveformResetKey}
				/>
			</View>



			{error && <Text style={styles.errorText}>{error}</Text>}

			{/* Button */}
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
							status === "initializing" && styles.buttonDisabled,
						]}
						onPress={handlePress}
						disabled={status === "initializing"}
					>
						<View style={styles.buttonInnerRing}>
							<Text
								style={[styles.buttonText, isListening && styles.buttonTextActive]}
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
		backgroundColor: "#000000",
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
	statusDot: {
		width: 8,
		height: 8,
		borderRadius: 4,
	},
	statusText: {
		fontSize: 11,
		fontWeight: "600",
		color: "#6B7280",
		letterSpacing: 1.5,
	},
	bpmSection: {
		alignItems: "center",
		paddingVertical: theme.spacing.lg,
	},
	bpmLabel: {
		fontSize: 11,
		fontWeight: "600",
		color: "#4B5563",
		letterSpacing: 2,
		marginBottom: 4,
	},
	bpmValue: {
		fontSize: 72,
		fontWeight: "300",
		color: "#FFFFFF",
		fontVariant: ["tabular-nums"],
		letterSpacing: -2,
	},
	waveformContainer: {
		alignItems: "center",
		paddingVertical: theme.spacing.md,
	},

	errorText: {
		fontSize: theme.fontSize.sm,
		color: theme.colors.destructive,
		textAlign: "center",
		marginBottom: theme.spacing.md,
	},
	buttonContainer: {
		marginTop: theme.spacing.lg,
		alignItems: "center",
		justifyContent: "center",
		paddingBottom: theme.spacing.xl,
	},
	buttonGlow: {
		borderRadius: 9999,
		shadowColor: "#00D4FF",
		shadowOffset: { width: 0, height: 0 },
	},
	buttonGlowIdle: {
		shadowColor: "#00D4FF",
	},
	buttonGlowActive: {
		shadowColor: "#3B82F6",
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
		backgroundColor: "#0A0A0A",
		borderColor: "#00D4FF",
	},
	buttonActive: {
		backgroundColor: "#090D14",
		borderColor: "#3B82F6",
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
		borderColor: "rgba(255, 255, 255, 0.2)",
		alignItems: "center",
		justifyContent: "center",
		backgroundColor: "rgba(0, 0, 0, 0.55)",
	},
	buttonText: {
		fontSize: 18,
		fontWeight: "700",
		color: "#00D4FF",
		letterSpacing: 2.4,
	},
	buttonTextActive: {
		color: "#3B82F6",
	},
}));
