import { Tabs } from "expo-router";
import { useUnistyles } from "react-native-unistyles";

import { TabBarIcon } from "@/components/tabbar-icon";

export default function TabLayout() {
	const { theme } = useUnistyles();

	return (
		<Tabs
			screenOptions={{
				headerShown: false,
				tabBarActiveTintColor: theme.colors.primary,
				tabBarInactiveTintColor: theme.colors.mutedForeground,
				tabBarStyle: {
					backgroundColor: theme.colors.background,
					borderTopColor: theme.colors.border,
				},
			}}
		>
			<Tabs.Screen
				name="index"
				options={{
					title: "BeatNet",
					tabBarIcon: ({ color }) => (
						<TabBarIcon name="heartbeat" color={color} />
					),
				}}
			/>
		</Tabs>
	);
}
