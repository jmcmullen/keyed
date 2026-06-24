import { DbProvider } from "@keyed/db";
import { Stack } from "expo-router";
import { GestureHandlerRootView } from "react-native-gesture-handler";
import { SafeAreaProvider } from "react-native-safe-area-context";
import { StyleSheet } from "react-native-unistyles";

export const unstable_settings = {
	initialRouteName: "index",
} as const;

export default function RootLayout() {
	return (
		<GestureHandlerRootView style={styles.root}>
			<SafeAreaProvider>
				<DbProvider>
					<Stack
						screenOptions={{
							headerShown: false,
						}}
					>
						<Stack.Screen name="index" />
						<Stack.Screen name="history" />
						<Stack.Screen name="modal" options={{ presentation: "modal" }} />
					</Stack>
				</DbProvider>
			</SafeAreaProvider>
		</GestureHandlerRootView>
	);
}

const styles = StyleSheet.create((theme) => ({
	root: {
		flex: 1,
		backgroundColor: theme.colors.background,
	},
}));
