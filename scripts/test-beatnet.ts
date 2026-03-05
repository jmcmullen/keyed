#!/usr/bin/env bun

import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "..");
const tests = join(root, "packages/engine/tests");
const bpmModel = join(root, "packages/engine/models/beatnet.onnx");
const keyModel = join(root, "packages/engine/models/keynet.onnx");
const audio = join(root, "packages/engine/test-data");

function need(path: string, label: string): void {
	if (existsSync(path)) return;
	throw new Error(`Missing ${label}: ${path}`);
}

function run(cmd: string, args: string[], cwd: string): void {
	const out = spawnSync(cmd, args, { cwd, stdio: "inherit" });
	if (out.status === 0) return;
	process.exit(out.status ?? 1);
}

need(bpmModel, "BeatNet model");
need(keyModel, "Key model");
need(audio, "engine test data directory");
run("bun", ["run", "test:native:build"], root);
run("./build/engine_tests", ["[bpm]"], tests);
