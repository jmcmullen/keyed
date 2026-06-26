#!/usr/bin/env bun

import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "..");
const tests = join(root, "packages/engine/tests");

const tags = new Map([
	["bpm", "[bpm]"],
	["e2e", "[e2e]"],
	["full", "[e2e]"],
	["onnx", "[onnx]"],
	["streaming", "[mel][streaming]"],
]);

function need(path: string, label: string): void {
	if (existsSync(path)) return;
	throw new Error(`Missing ${label}: ${path}`);
}

function run(cmd: string, args: string[], cwd: string): void {
	const out = spawnSync(cmd, args, { cwd, stdio: "inherit" });
	if (out.status === 0) return;
	process.exit(out.status ?? 1);
}

const tag = tags.get(process.argv[2] ?? "");
if (!tag) {
	throw new Error(
		`Usage: bun scripts/test-engine.ts ${[...tags.keys()].join("|")}`,
	);
}

need(join(root, "packages/engine/models/beatnet.onnx"), "BeatNet model");
if (tag !== "[onnx]") {
	need(join(root, "packages/engine/models/keynet.onnx"), "Key model");
}
if (process.argv[2] === "bpm" || process.argv[2] === "full") {
	need(join(root, "packages/engine/test-data"), "engine test data directory");
}

run("bun", ["run", "test:native:build"], root);
run("./build/engine_tests", [tag], tests);
