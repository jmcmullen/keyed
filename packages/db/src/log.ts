import { log as evlog, initLog } from "evlog/client";

type Event = {
	action: string;
	surface: string;
	[key: string]: unknown;
};

let ready = false;

function init() {
	if (ready) return;
	initLog({ service: "keyed-native" });
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

export const log = {
	info(event: Event): void {
		init();
		evlog.info(event);
	},
	error(event: Event, error?: unknown): void {
		init();
		evlog.error(
			error === undefined ? event : { ...event, error: fields(error) },
		);
	},
};
