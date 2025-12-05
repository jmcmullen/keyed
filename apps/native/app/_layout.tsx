import { DbProvider } from "@keyed/db";
import { Stack } from "expo-router";
import { GestureHandlerRootView } from "react-native-gesture-handler";
import { useUnistyles } from "react-native-unistyles";

export const unstable_settings = {
	initialRouteName: "(drawer)",
};

export default function RootLayout() {
	const { theme } = useUnistyles();

	return (
		<DbProvider>
			<GestureHandlerRootView style={{ flex: 1 }}>
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
			</GestureHandlerRootView>
		</DbProvider>
	);
}
