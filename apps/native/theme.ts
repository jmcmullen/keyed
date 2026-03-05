const semanticColors = {
	feedback: {
		success: "#22C55E",
		warning: "#F59E0B",
		info: "#3B82F6",
		danger: "#EF4444",
		onDanger: "#FFFFFF",
	},
	status: {
		ready: "#22C55E",
		listening: "#00D4FF",
		error: "#EF4444",
	},
	text: {
		muted: "#4A5568",
		secondary: "#718096",
	},
	surface: {
		audio: "#000408",
		control: "#0A0A0A",
		controlActive: "#090D14",
		overlaySoft: "rgba(255, 255, 255, 0.2)",
		overlayStrong: "rgba(0, 0, 0, 0.55)",
	},
	interaction: {
		primary: "#00D4FF",
		primaryActive: "#3B82F6",
	},
	visualizer: {
		grid: "rgba(255, 255, 255, 0.3)",
		audio: {
			low: "#1A4A5C",
			mid: "#4AA8D8",
			high: "#8DD4F0",
			peak: "#B8E8FF",
		},
	},
} as const;

const lightCoreColors = {
	typography: "#000000",
	background: "#FFFFFF",
	foreground: "#000000",
	card: "#FAFAFA",
	cardForeground: "#000000",
	primary: "#1A1A1A",
	primaryForeground: "#FFFFFF",
	secondary: "#F2F2F2",
	secondaryForeground: "#000000",
	muted: "#F5F5F5",
	mutedForeground: "#737373",
	accent: "#F5F5F5",
	accentForeground: "#000000",
	border: "#E6E6E6",
	input: "#E6E6E6",
	ring: "#333333",
} as const;

const darkCoreColors = {
	typography: "#FFFFFF",
	background: "#000000",
	foreground: "#FFFFFF",
	card: "#050505",
	cardForeground: "#FFFFFF",
	primary: "#E6E6E6",
	primaryForeground: "#000000",
	secondary: "#1A1A1A",
	secondaryForeground: "#FFFFFF",
	muted: "#141414",
	mutedForeground: "#A6A6A6",
	accent: "#141414",
	accentForeground: "#FFFFFF",
	border: "#262626",
	input: "#262626",
	ring: "#CCCCCC",
} as const;

const spacing = {
	xs: 4,
	sm: 8,
	md: 16,
	lg: 24,
	xl: 32,
	xxl: 48,
} as const;

const borderRadius = {
	sm: 6,
	md: 8,
	lg: 12,
	xl: 16,
} as const;

const fontSize = {
	xs: 12,
	sm: 14,
	base: 16,
	lg: 18,
	xl: 20,
	"2xl": 24,
	"3xl": 30,
	"4xl": 36,
} as const;

export const lightTheme = {
	colors: {
		...semanticColors,
		...lightCoreColors,
	},
	spacing,
	borderRadius,
	fontSize,
} as const;

export const darkTheme = {
	colors: {
		...semanticColors,
		...darkCoreColors,
	},
	spacing,
	borderRadius,
	fontSize,
} as const;
