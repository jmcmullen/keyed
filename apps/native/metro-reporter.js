const fs = require("node:fs");
const path = require("node:path");
const util = require("node:util");
const { Terminal, TerminalReporter } = require("metro");

const root = path.resolve(__dirname, "../..");
const dir = path.join(root, ".logs");
const metroDir = path.join(dir, "metro");
const evlogDir = path.join(dir, "evlog");
let warned = false;

function stamp() {
	return new Date().toISOString();
}

function day() {
	return stamp().slice(0, 10);
}

function clean(value) {
	if (value instanceof Error) {
		return {
			name: value.name,
			message: value.message,
			stack: value.stack,
		};
	}
	if (typeof value === "bigint") return value.toString();
	if (Array.isArray(value)) return value.map(clean);
	if (!value || typeof value !== "object") return value;
	return Object.fromEntries(
		Object.entries(value).map(([key, item]) => [key, clean(item)]),
	);
}

function line(file, value) {
	try {
		fs.mkdirSync(path.dirname(file), { recursive: true });
		fs.appendFileSync(file, `${JSON.stringify(clean(value))}\n`);
	} catch (error) {
		if (warned) return;
		warned = true;
		process.stderr.write(
			`[keyed] Failed to write Metro log file: ${error instanceof Error ? error.message : String(error)}\n`,
		);
	}
}

function text(value) {
	if (typeof value === "string") return value;
	return util.inspect(value, { depth: 6, colors: false, breakLength: 120 });
}

function appEvent(event) {
	if (event.type !== "client_log") return null;
	if (
		!event.data ||
		event.data.length !== 1 ||
		typeof event.data[0] !== "string"
	) {
		return null;
	}

	try {
		const value = JSON.parse(event.data[0]);
		if (!value || typeof value !== "object" || Array.isArray(value)) {
			return null;
		}
		if (value.service !== "keyed-native" || typeof value.action !== "string") {
			return null;
		}
		return value;
	} catch {
		return null;
	}
}

function level(event) {
	if (event.level === "warn") return "warn";
	if (event.level === "error") return "error";
	if (event.type.includes("failed") || event.type.includes("error"))
		return "error";
	return "info";
}

function diskEvent(event) {
	if (event.type === "client_log") {
		return {
			timestamp: stamp(),
			level: level(event),
			service: "keyed-native",
			action: "metro.client_log",
			mode: event.mode,
			data: event.data.map(text),
		};
	}

	return {
		timestamp: stamp(),
		service: "keyed-native",
		action: `metro.${event.type}`,
		...event,
		level: level(event),
	};
}

module.exports = function createReporter(base) {
	const fallback = base ?? new TerminalReporter(new Terminal(process.stdout));

	return {
		update(event) {
			fallback.update(event);

			const app = appEvent(event);
			if (app) {
				line(path.join(evlogDir, `${day()}.jsonl`), {
					timestamp: stamp(),
					service: "keyed-native",
					...app,
				});
				return;
			}

			line(path.join(metroDir, `${day()}.jsonl`), diskEvent(event));
		},
	};
};
