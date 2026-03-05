import FontAwesome from "@expo/vector-icons/FontAwesome";
import { type ElementRef, forwardRef } from "react";
import { Pressable } from "react-native";
import { StyleSheet } from "react-native-unistyles";

interface HeaderButtonProps {
	onPress?: () => void;
}

export const HeaderButton = forwardRef<
	ElementRef<typeof Pressable>,
	HeaderButtonProps
>(({ onPress }, ref) => {
	return (
		<Pressable ref={ref} onPress={onPress} style={styles.button}>
			{({ pressed }) => (
				<FontAwesome
					name="history"
					size={20}
					style={[styles.icon, pressed && styles.iconPressed]}
				/>
			)}
		</Pressable>
	);
});
HeaderButton.displayName = "HeaderButton";

const styles = StyleSheet.create((theme) => ({
	button: {
		padding: theme.spacing.sm,
		marginRight: theme.spacing.sm,
		borderRadius: theme.borderRadius.lg,
		backgroundColor: `${theme.colors.secondary}80`,
	},
	icon: {
		color: theme.colors.secondaryForeground,
	},
	iconPressed: {
		opacity: 0.7,
	},
}));
