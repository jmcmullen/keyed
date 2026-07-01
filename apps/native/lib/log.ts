import { log as evlog, initLog } from "evlog/client";

type Level = "info" | "error";

type Event = {
	action: string;
	surface: string;
	[key: string]: unknown;
};

let ready = false;

function init() {
	if (ready) return;
	initLog({ console: false, service: "keyed-native" });
	ready = true;
}

function fields(error: unknown): Record<string, unknown> {
	if (error instanceof Error) {
		return {
			name: error.name,
			message: error.message,
		};
	}
	return { message: String(error) };
}

function emit(level: Level, event: Event) {
	init();
	evlog[level](event);
	if (__DEV__) {
		console[level === "error" ? "error" : "log"](
			JSON.stringify({
				timestamp: new Date().toISOString(),
				level,
				service: "keyed-native",
				...event,
			}),
		);
	}
}

export const log = {
	info(event: Event): void {
		emit("info", event);
	},
	error(event: Event, error?: unknown): void {
		emit(
			"error",
			error === undefined ? event : { ...event, error: fields(error) },
		);
	},
};
