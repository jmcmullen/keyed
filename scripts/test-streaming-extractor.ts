#!/usr/bin/env bun
/**
 * Test that StreamingMelExtractor produces identical output to MelSpectrogramExtractor
 */

import { execSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";

import {
	MEL_CONFIG,
	MelSpectrogramExtractor,
	StreamingMelExtractor,
} from "../apps/native/lib/beatnet/mel-spectrogram";

const TEST_FILE = join(import.meta.dir, "../apps/native/assets/test/125.m4a");
const SAMPLE_RATE = 22050;
const HOP_LENGTH = 441; // Same as AudioRecorder buffer size in app

function decodeAudioFile(filePath: string): Float32Array {
	if (!existsSync(filePath)) {
		throw new Error(`File not found: ${filePath}`);
	}

	const cmd = `ffmpeg -i "${filePath}" -f f32le -acodec pcm_f32le -ac 1 -ar ${SAMPLE_RATE} -`;

	const buffer = execSync(cmd, {
		maxBuffer: 50 * 1024 * 1024,
		stdio: ["pipe", "pipe", "pipe"],
	});

	const samples = new Float32Array(buffer.length / 4);
	for (let i = 0; i < samples.length; i++) {
		samples[i] = buffer.readFloatLE(i * 4);
	}

	return samples;
}

async function main() {
	console.log("=".repeat(60));
	console.log("StreamingMelExtractor vs MelSpectrogramExtractor Test");
	console.log("=".repeat(60));

	// Load audio
	console.log("\nLoading audio...");
	const samples = decodeAudioFile(TEST_FILE);
	console.log(
		`Loaded ${samples.length} samples (${(samples.length / SAMPLE_RATE).toFixed(1)}s)`,
	);

	// Method 1: Direct MelSpectrogramExtractor (like test script)
	console.log("\n--- Method 1: Direct MelSpectrogramExtractor ---");
	const directExtractor = new MelSpectrogramExtractor();
	const directFeatures: Float32Array[] = [];

	const { hopLength, winLength } = MEL_CONFIG;
	const numFrames = Math.floor((samples.length - winLength) / hopLength) + 1;

	for (let i = 0; i < numFrames; i++) {
		const start = i * hopLength;
		const frame = samples.subarray(start, start + winLength);
		const features = directExtractor.processFrame(frame);
		if (features) {
			directFeatures.push(features);
		}
	}
	console.log(`Extracted ${directFeatures.length} feature frames`);

	// Method 2: StreamingMelExtractor (like app)
	console.log("\n--- Method 2: StreamingMelExtractor ---");
	const streamingExtractor = new StreamingMelExtractor();
	const streamingFeatures: Float32Array[] = [];

	// Process in chunks of HOP_LENGTH (like AudioRecorder)
	for (let i = 0; i < samples.length; i += HOP_LENGTH) {
		const chunk = samples.subarray(i, Math.min(i + HOP_LENGTH, samples.length));
		const features = streamingExtractor.push(chunk);
		streamingFeatures.push(...features);
	}
	console.log(`Extracted ${streamingFeatures.length} feature frames`);

	// Compare
	console.log("\n--- Comparison ---");
	console.log(`Direct frames: ${directFeatures.length}`);
	console.log(`Streaming frames: ${streamingFeatures.length}`);

	if (directFeatures.length !== streamingFeatures.length) {
		console.log("❌ FRAME COUNT MISMATCH!");
	}

	const numToCompare = Math.min(
		directFeatures.length,
		streamingFeatures.length,
		10,
	);
	let maxDiff = 0;
	let totalDiff = 0;
	let numDiffs = 0;

	for (let f = 0; f < numToCompare; f++) {
		const direct = directFeatures[f];
		const streaming = streamingFeatures[f];

		let frameDiff = 0;
		for (let i = 0; i < direct.length; i++) {
			const diff = Math.abs(direct[i] - streaming[i]);
			frameDiff += diff;
			maxDiff = Math.max(maxDiff, diff);
		}
		totalDiff += frameDiff;
		numDiffs += direct.length;

		if (f < 3) {
			console.log(`\nFrame ${f + 1}:`);
			console.log(
				`  Direct first 5: [${Array.from(direct.slice(0, 5))
					.map((v) => v.toFixed(4))
					.join(", ")}]`,
			);
			console.log(
				`  Stream first 5: [${Array.from(streaming.slice(0, 5))
					.map((v) => v.toFixed(4))
					.join(", ")}]`,
			);
			console.log(
				`  Direct mean: ${(Array.from(direct).reduce((a, b) => a + b, 0) / direct.length).toFixed(4)}`,
			);
			console.log(
				`  Stream mean: ${(Array.from(streaming).reduce((a, b) => a + b, 0) / streaming.length).toFixed(4)}`,
			);
		}
	}

	console.log(`\nMax difference: ${maxDiff.toFixed(6)}`);
	console.log(`Avg difference: ${(totalDiff / numDiffs).toFixed(6)}`);

	if (maxDiff < 0.0001) {
		console.log("\n✓ StreamingMelExtractor produces IDENTICAL output");
	} else if (maxDiff < 0.01) {
		console.log(
			"\n~ StreamingMelExtractor produces SIMILAR output (numerical precision)",
		);
	} else {
		console.log("\n❌ StreamingMelExtractor produces DIFFERENT output - BUG!");
	}
}

main().catch(console.error);
