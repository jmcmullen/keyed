import { Pressable, Text, useWindowDimensions, View } from "react-native";
import { StyleSheet } from "react-native-unistyles";
import { Waveform } from "@/components/waveform";
import { useEngine } from "@/hooks/use-engine";

export default function BeatNetTab() {
	const {
		status,
		isListening,
		result,
		beatActivation,
		downbeatActivation,
		waveformSamples,
		error,
		startListening,
		stopListening,
		reset,
	} = useEngine();
	const { width: windowWidth } = useWindowDimensions();

	const handlePress = async () => {
		if (isListening) {
			await stopListening();
		} else {
			if (status === "detected" || status === "error") {
				reset();
			}
			await startListening();
		}
	};

	const getButtonText = () => {
		if (status === "initializing") return "Loading...";
		if (isListening) return "Stop";
		return "Start";
	};

	const getStatusText = () => {
		if (isListening) {
			if (!result?.bpm) return "Listening...";
			return `${result.meter}/4`;
		}
		if (status === "initializing") return "Loading...";
		if (status === "error") return "Error";
		if (result?.bpm) return "Tap to detect";
		return "Tap to start";
	};

	return (
		<View style={styles.container}>
			{/* Waveform */}
			<Waveform
				samples={waveformSamples}
				width={windowWidth}
				height={120}
				beatActivation={beatActivation}
				downbeatActivation={downbeatActivation}
				isActive={isListening}
			/>

			{/* BPM Display */}
			<View style={styles.bpmSection}>
				<Text style={styles.bpmValue}>
					{result?.bpm ? result.bpm.toFixed(0) : "---"}
				</Text>
				<Text style={styles.bpmLabel}>BPM</Text>
				{result?.confidence !== undefined && result.confidence > 0 && (
					<Text style={styles.confidenceText}>
						{Math.round(result.confidence * 100)}% confident
					</Text>
				)}
			</View>

			{/* Status */}
			<Text style={styles.statusText}>{getStatusText()}</Text>

			{/* Error */}
			{error && <Text style={styles.errorText}>{error}</Text>}

			{/* Action Button */}
			<View style={styles.buttonContainer}>
				<Pressable
					style={({ pressed }) => [
						styles.button,
						pressed && styles.buttonPressed,
						status === "initializing" && styles.buttonDisabled,
						isListening && styles.buttonStop,
					]}
					onPress={handlePress}
					disabled={status === "initializing"}
				>
					<Text
						style={[styles.buttonText, isListening && styles.buttonTextStop]}
					>
						{getButtonText()}
					</Text>
				</Pressable>
			</View>
		</View>
	);
}

const styles = StyleSheet.create((theme) => ({
	container: {
		flex: 1,
		backgroundColor: "#000000",
		alignItems: "center",
		justifyContent: "center",
		gap: theme.spacing.xl,
		paddingBottom: theme.spacing.xl,
	},
	bpmSection: {
		alignItems: "center",
	},
	bpmValue: {
		fontSize: 96,
		fontWeight: "200",
		color: "#FFFFFF",
		fontVariant: ["tabular-nums"],
	},
	bpmLabel: {
		fontSize: theme.fontSize.sm,
		color: "rgba(255, 255, 255, 0.5)",
		textTransform: "uppercase",
		letterSpacing: 4,
	},
	confidenceText: {
		fontSize: theme.fontSize.sm,
		color: "rgba(255, 255, 255, 0.3)",
		marginTop: theme.spacing.sm,
	},
	statusText: {
		fontSize: theme.fontSize.base,
		color: "rgba(255, 255, 255, 0.5)",
	},
	errorText: {
		fontSize: theme.fontSize.sm,
		color: theme.colors.destructive,
	},
	buttonContainer: {
		position: "absolute",
		bottom: theme.spacing.xl,
		left: theme.spacing.lg,
		right: theme.spacing.lg,
	},
	button: {
		backgroundColor: "rgba(255, 255, 255, 0.1)",
		paddingVertical: theme.spacing.md,
		borderRadius: theme.borderRadius.lg,
		alignItems: "center",
		borderWidth: 1,
		borderColor: "rgba(255, 255, 255, 0.2)",
	},
	buttonPressed: {
		backgroundColor: "rgba(255, 255, 255, 0.15)",
	},
	buttonDisabled: {
		opacity: 0.5,
	},
	buttonStop: {
		backgroundColor: "rgba(239, 68, 68, 0.2)",
		borderColor: "rgba(239, 68, 68, 0.4)",
	},
	buttonText: {
		fontSize: theme.fontSize.base,
		fontWeight: "500",
		color: "#FFFFFF",
	},
	buttonTextStop: {
		color: "#EF4444",
	},
}));
