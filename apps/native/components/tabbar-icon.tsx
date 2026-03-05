import FontAwesome from "@expo/vector-icons/FontAwesome";
import { StyleSheet } from "react-native-unistyles";

interface TabBarIconProps {
	name: React.ComponentProps<typeof FontAwesome>["name"];
	color: string;
}

export const TabBarIcon = (props: TabBarIconProps) => {
	return <FontAwesome size={24} style={styles.icon} {...props} />;
};

const styles = StyleSheet.create((_theme) => ({
	icon: {
		marginBottom: -3,
	},
}));
