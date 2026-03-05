export type PermissionState = "granted" | "denied" | "undetermined";

export interface PermissionResult {
	granted: boolean;
	err: string;
}

export function coercePermission(value: unknown): boolean {
	if (typeof value === "boolean") return value;
	if (!value || typeof value !== "object") return false;
	if (!("granted" in value)) return false;
	const granted = (value as { granted?: unknown }).granted;
	return granted === true;
}

export function resolvePermission(
	state: PermissionState,
	ask: () => Promise<unknown>,
): Promise<PermissionResult> {
	if (state === "granted") {
		return Promise.resolve({ granted: true, err: "" });
	}
	return ask()
		.then((value) => ({ granted: coercePermission(value), err: "" }))
		.catch((err: unknown) => ({
			granted: false,
			err:
				err instanceof Error
					? err.message
					: "Failed to request microphone permission",
		}));
}
