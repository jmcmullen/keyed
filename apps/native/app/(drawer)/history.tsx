import { type Detection, useDb } from "@keyed/db";
import { Alert, Pressable, ScrollView, Text, View } from "react-native";
import { StyleSheet } from "react-native-unistyles";

function formatWhen(value: Date): string {
	const diff = Date.now() - value.getTime();
	const min = Math.floor(diff / 60000);
	if (min < 1) return "just now";
	if (min < 60) return `${min} min ago`;
	return value.toLocaleString();
}

export default function HistoryScreen() {
	const db = useDb();

	const clear = () => {
		Alert.alert("Clear history", "Delete all saved detections?", [
			{ text: "Cancel", style: "cancel" },
			{
				text: "Clear",
				style: "destructive",
				onPress: () => {
					void db.clearHistory();
				},
			},
		]);
	};

	const del = (item: Detection) => {
		Alert.alert("Delete entry", "Remove this detection from history?", [
			{ text: "Cancel", style: "cancel" },
			{
				text: "Delete",
				style: "destructive",
				onPress: () => {
					void db.deleteDetection(item.id);
				},
			},
		]);
	};

	return (
		<View style={styles.container}>
			<View style={styles.row}>
				<Text style={styles.title}>History</Text>
				<Pressable onPress={clear} style={styles.clearBtn}>
					<Text style={styles.clearTxt}>Clear all</Text>
				</Pressable>
			</View>
			<ScrollView contentContainerStyle={styles.list}>
				{db.detections.length === 0 && (
					<View style={styles.empty}>
						<Text style={styles.emptyTxt}>No history yet</Text>
					</View>
				)}
				{db.detections.map((item) => (
					<View key={item.id} style={styles.card}>
						<View style={styles.cardTop}>
							<Text style={styles.bpm}>{item.bpm} BPM</Text>
							<Text style={styles.key}>{item.key}</Text>
						</View>
						<Text style={styles.meta}>
							{item.camelotCode} • key {Math.round(item.keyConfidence * 100)}% •
							bpm {Math.round(item.bpmConfidence * 100)}%
						</Text>
						<Text style={styles.meta}>
							{formatWhen(item.createdAt)} • {item.duration}s
						</Text>
						<Pressable onPress={() => del(item)} style={styles.delBtn}>
							<Text style={styles.delTxt}>Delete</Text>
						</Pressable>
					</View>
				))}
			</ScrollView>
		</View>
	);
}

const styles = StyleSheet.create((theme) => ({
	container: {
		flex: 1,
		backgroundColor: theme.colors.background,
		paddingHorizontal: theme.spacing.md,
		paddingTop: theme.spacing.md,
	},
	row: {
		flexDirection: "row",
		alignItems: "center",
		justifyContent: "space-between",
		marginBottom: theme.spacing.md,
	},
	title: {
		fontSize: theme.fontSize["2xl"],
		fontWeight: "700",
		color: theme.colors.foreground,
	},
	clearBtn: {
		paddingHorizontal: theme.spacing.sm,
		paddingVertical: 6,
		borderRadius: theme.borderRadius.md,
		backgroundColor: theme.colors.muted,
	},
	clearTxt: {
		color: theme.colors.feedback.danger,
		fontWeight: "600",
	},
	list: {
		paddingBottom: theme.spacing.xl,
		gap: theme.spacing.sm,
	},
	empty: {
		paddingVertical: theme.spacing.xl,
		alignItems: "center",
	},
	emptyTxt: {
		fontSize: theme.fontSize.base,
		color: theme.colors.mutedForeground,
	},
	card: {
		backgroundColor: theme.colors.card,
		borderRadius: theme.borderRadius.lg,
		borderWidth: 1,
		borderColor: theme.colors.border,
		padding: theme.spacing.md,
		gap: 6,
	},
	cardTop: {
		flexDirection: "row",
		alignItems: "center",
		justifyContent: "space-between",
	},
	bpm: {
		color: theme.colors.foreground,
		fontWeight: "700",
		fontSize: theme.fontSize.lg,
	},
	key: {
		color: theme.colors.interaction.primary,
		fontWeight: "700",
		fontSize: theme.fontSize.lg,
	},
	meta: {
		color: theme.colors.mutedForeground,
		fontSize: theme.fontSize.sm,
	},
	delBtn: {
		marginTop: 4,
		alignSelf: "flex-end",
		paddingHorizontal: theme.spacing.sm,
		paddingVertical: 4,
		borderRadius: theme.borderRadius.sm,
		backgroundColor: theme.colors.muted,
	},
	delTxt: {
		color: theme.colors.feedback.danger,
		fontWeight: "600",
		fontSize: theme.fontSize.sm,
	},
}));
