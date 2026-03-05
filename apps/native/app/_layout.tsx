import { DbProvider } from "@keyed/db";
import { Stack } from "expo-router";
import { GestureHandlerRootView } from "react-native-gesture-handler";
import { StyleSheet, useUnistyles } from "react-native-unistyles";

export const unstable_settings = {
	initialRouteName: "(drawer)",
} as const;

export default function RootLayout() {
	const { theme } = useUnistyles();

	return (
		<GestureHandlerRootView style={styles.root}>
			<DbProvider>
				<Stack
					screenOptions={{
						headerStyle: {
							backgroundColor: theme.colors.background,
						},
						headerTitleStyle: {
							color: theme.colors.foreground,
						},
						headerTintColor: theme.colors.foreground,
					}}
				>
					<Stack.Screen name="(drawer)" options={{ headerShown: false }} />
					<Stack.Screen
						name="modal"
						options={{ title: "Modal", presentation: "modal" }}
					/>
				</Stack>
			</DbProvider>
		</GestureHandlerRootView>
	);
}

const styles = StyleSheet.create((theme) => ({
	root: {
		flex: 1,
		backgroundColor: theme.colors.background,
	},
}));
