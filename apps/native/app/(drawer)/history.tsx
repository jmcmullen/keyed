import { type Detection, useDb } from "@keyed/db";
import { useState } from "react";
import { Alert, FlatList, Pressable, Text, View } from "react-native";
import Swipeable from "react-native-gesture-handler/Swipeable";
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
	const [busy, setBusy] = useState(false);

	const fail = (err: unknown) => {
		const message =
			err instanceof Error ? err.message : "Database action failed";
		Alert.alert("Database error", message);
	};

	const clearDb = () => {
		if (busy) return;
		setBusy(true);
		void db
			.clearHistory()
			.catch((err: unknown) => fail(err))
			.finally(() => setBusy(false));
	};

	const delDb = (id: string) => {
		if (busy) return;
		setBusy(true);
		void db
			.deleteDetection(id)
			.catch((err: unknown) => fail(err))
			.finally(() => setBusy(false));
	};

	const clear = () => {
		Alert.alert("Clear history", "Delete all saved detections?", [
			{ text: "Cancel", style: "cancel" },
			{
				text: "Clear",
				style: "destructive",
				onPress: () => {
					clearDb();
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
					delDb(item.id);
				},
			},
		]);
	};

	const right = (item: Detection) => (
		<Pressable
			disabled={busy}
			onPress={() => del(item)}
			style={[styles.swipeDelBtn, busy && styles.btnDisabled]}
		>
			<Text style={styles.swipeDelTxt}>Delete</Text>
		</Pressable>
	);

	return (
		<View style={styles.container}>
			<View style={styles.row}>
				<Text style={styles.title}>History</Text>
				<Pressable
					disabled={busy}
					onPress={clear}
					style={[styles.clearBtn, busy && styles.btnDisabled]}
				>
					<Text style={styles.clearTxt}>Clear all</Text>
				</Pressable>
			</View>
			<FlatList
				data={db.detections}
				keyExtractor={(item) => item.id}
				contentContainerStyle={[
					styles.list,
					db.detections.length === 0 && styles.emptyList,
				]}
				ListEmptyComponent={
					<View style={styles.empty}>
						<Text style={styles.emptyTxt}>No history yet</Text>
					</View>
				}
				renderItem={({ item }) => (
					<Swipeable
						enabled={!busy}
						overshootRight={false}
						renderRightActions={() => right(item)}
					>
						<View style={styles.card}>
							<View style={styles.cardTop}>
								<Text style={styles.bpm}>{item.bpm} BPM</Text>
								<Text style={styles.key}>{item.key}</Text>
							</View>
							<Text style={styles.meta}>
								{item.camelotCode} • key {Math.round(item.keyConfidence * 100)}%
								• bpm {Math.round(item.bpmConfidence * 100)}%
							</Text>
							<Text style={styles.meta}>
								{formatWhen(item.createdAt)} • {item.duration}s
							</Text>
						</View>
					</Swipeable>
				)}
			/>
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
	emptyList: {
		flexGrow: 1,
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
	swipeDelBtn: {
		justifyContent: "center",
		alignItems: "center",
		paddingHorizontal: theme.spacing.md,
		backgroundColor: theme.colors.feedback.danger,
		borderRadius: theme.borderRadius.lg,
		marginBottom: theme.spacing.sm,
	},
	swipeDelTxt: {
		color: theme.colors.feedback.onDanger,
		fontWeight: "600",
		fontSize: theme.fontSize.sm,
	},
	btnDisabled: {
		opacity: 0.5,
	},
}));
