import type { DetectionStatus } from "@/hooks/use-engine";

export function buttonText(listening: boolean): string {
	if (listening) return "STOP";
	return "START";
}

export function shouldReset(status: DetectionStatus): boolean {
	return status === "detected" || status === "error";
}
