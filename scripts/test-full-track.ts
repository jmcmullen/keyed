#!/usr/bin/env bun
/**
 * Full Track BPM Test
 *
 * Tests BPM detection on a full-length track to see if longer audio improves accuracy.
 * Uses online/streaming detection (frame-by-frame, no lookahead).
 */

import { execSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { InferenceSession, Tensor } from "onnxruntime-node";

import {
	MEL_CONFIG,
	MelSpectrogramExtractor,
} from "../apps/native/lib/beatnet/mel-spectrogram";
import { CascadeParticleFilter } from "../apps/native/lib/beatnet/particle-filter";

const SAMPLE_RATE = 22050;
const MODEL_PATH = join(
	import.meta.dir,
	"../apps/native/assets/models/beatnet_model_2.onnx",
);
const TRACK_PATH = join(
	import.meta.dir,
	"../apps/native/assets/test/125_full_track.aiff",
);
const EXPECTED_BPM = 125;

// Model constants
const INPUT_DIM = 272;
const HIDDEN_DIM = 150;
const NUM_LAYERS = 2;

// ONNX Model wrapper
class BeatNetModel {
	private session: InferenceSession | null = null;
	private hidden: Float32Array;
	private cell: Float32Array;

	constructor() {
		this.hidden = new Float32Array(NUM_LAYERS * 1 * HIDDEN_DIM);
		this.cell = new Float32Array(NUM_LAYERS * 1 * HIDDEN_DIM);
	}

	async load(modelPath: string): Promise<void> {
		this.session = await InferenceSession.create(modelPath);
	}

	reset(): void {
		this.hidden.fill(0);
		this.cell.fill(0);
	}

	async infer(
		features: Float32Array,
	): Promise<{ beatActivation: number; downbeatActivation: number }> {
		if (!this.session) throw new Error("Model not loaded");

		const inputTensor = new Tensor("float32", features, [1, 1, INPUT_DIM]);
		const hiddenTensor = new Tensor("float32", this.hidden, [
			NUM_LAYERS,
			1,
			HIDDEN_DIM,
		]);
		const cellTensor = new Tensor("float32", this.cell, [
			NUM_LAYERS,
			1,
			HIDDEN_DIM,
		]);

		const results = await this.session.run({
			input: inputTensor,
			hidden_in: hiddenTensor,
			cell_in: cellTensor,
		});

		const output = results.output.data as Float32Array;
		const hiddenOut = results.hidden_out.data as Float32Array;
		const cellOut = results.cell_out.data as Float32Array;

		this.hidden.set(hiddenOut);
		this.cell.set(cellOut);

		// Apply softmax if needed
		let probs: Float32Array;
		const sum = output[0] + output[1] + output[2];
		if (Math.abs(sum - 1.0) > 0.01) {
			const maxVal = Math.max(output[0], output[1], output[2]);
			const exp0 = Math.exp(output[0] - maxVal);
			const exp1 = Math.exp(output[1] - maxVal);
			const exp2 = Math.exp(output[2] - maxVal);
			const expSum = exp0 + exp1 + exp2;
			probs = new Float32Array([exp0 / expSum, exp1 / expSum, exp2 / expSum]);
		} else {
			probs = output;
		}

		return {
			beatActivation: probs[0],
			downbeatActivation: probs[1],
		};
	}
}

function decodeAudio(filePath: string): Float32Array {
	console.log(`Decoding: ${filePath}`);
	const cmd = `ffmpeg -i "${filePath}" -f f32le -acodec pcm_f32le -ac 1 -ar ${SAMPLE_RATE} -`;

	const buffer = execSync(cmd, {
		maxBuffer: 500 * 1024 * 1024, // 500MB for full track
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
	console.log("Full Track BPM Test (Online Detection)");
	console.log("=".repeat(60));
	console.log(`Track: ${TRACK_PATH}`);
	console.log(`Expected BPM: ${EXPECTED_BPM}`);
	console.log();

	if (!existsSync(TRACK_PATH)) {
		console.error("Track file not found!");
		process.exit(1);
	}

	// Load model
	console.log("Loading model...");
	const model = new BeatNetModel();
	await model.load(MODEL_PATH);
	model.reset();

	// Decode audio
	const samples = decodeAudio(TRACK_PATH);
	const durationSec = samples.length / SAMPLE_RATE;
	console.log(
		`Duration: ${(durationSec / 60).toFixed(1)} minutes (${durationSec.toFixed(1)}s)`,
	);
	console.log(`Samples: ${samples.length.toLocaleString()}`);
	console.log();

	// Initialize components
	const melExtractor = new MelSpectrogramExtractor();
	const particleFilter = new CascadeParticleFilter();

	const hopLength = MEL_CONFIG.hopLength;
	const winLength = MEL_CONFIG.winLength;
	const numFrames = Math.floor((samples.length - winLength) / hopLength) + 1;
	const fps = SAMPLE_RATE / hopLength;

	console.log(`Processing ${numFrames} frames at ${fps.toFixed(1)} FPS...`);
	console.log();

	// Track BPM over time
	const bpmLog: Array<{ time: number; bpm: number; locked: boolean }> = [];
	let frameCount = 0;
	const startTime = performance.now();

	// Process frame by frame (online/streaming)
	for (let i = 0; i < numFrames; i++) {
		const start = i * hopLength;
		const frame = samples.subarray(start, start + winLength);

		const features = melExtractor.processFrame(frame);
		if (!features) continue;

		frameCount++;
		const { beatActivation, downbeatActivation } = await model.infer(features);
		particleFilter.processFrame(beatActivation, downbeatActivation);

		// Log BPM every second
		const currentTime = (i * hopLength) / SAMPLE_RATE;
		if (frameCount % 50 === 0) {
			// Every 1 second at 50 FPS
			const currentBpm = particleFilter.getCurrentBpm();
			const isLocked = particleFilter.isTempoLocked();
			bpmLog.push({ time: currentTime, bpm: currentBpm, locked: isLocked });

			// Print progress every 10 seconds
			if (frameCount % 500 === 0) {
				const elapsed = (performance.now() - startTime) / 1000;
				const progress = ((i / numFrames) * 100).toFixed(1);
				console.log(
					`  ${progress}% | t=${currentTime.toFixed(0)}s | BPM=${currentBpm.toFixed(1)} | ` +
						`locked=${isLocked} | speed=${(currentTime / elapsed).toFixed(1)}x`,
				);
			}
		}
	}

	const processingTime = (performance.now() - startTime) / 1000;
	console.log();
	console.log(
		`Processing complete in ${processingTime.toFixed(1)}s (${(durationSec / processingTime).toFixed(1)}x realtime)`,
	);
	console.log();

	// Final result
	const finalBpm = particleFilter.getCurrentBpm();
	const finalBpmInterp = particleFilter.getInterpolatedBpm();
	const error = Math.abs(finalBpm - EXPECTED_BPM);
	const errorInterp = Math.abs(finalBpmInterp - EXPECTED_BPM);

	console.log("=".repeat(60));
	console.log("RESULTS");
	console.log("=".repeat(60));
	console.log(`Expected:     ${EXPECTED_BPM} BPM`);
	console.log(`Detected:     ${finalBpm.toFixed(1)} BPM (median)`);
	console.log(`Interpolated: ${finalBpmInterp.toFixed(1)} BPM`);
	console.log(
		`Error:        ${error.toFixed(1)} BPM (median), ${errorInterp.toFixed(1)} BPM (interp)`,
	);
	console.log(`Tempo Locked: ${particleFilter.isTempoLocked()}`);
	console.log();

	// Show BPM progression
	console.log("BPM over time:");
	const intervals = [10, 30, 60, 120, 180, 240, 300];
	for (const t of intervals) {
		const entry = bpmLog.find((e) => e.time >= t);
		if (entry) {
			console.log(
				`  ${t}s: ${entry.bpm.toFixed(1)} BPM ${entry.locked ? "(locked)" : ""}`,
			);
		}
	}
	console.log(`  final: ${finalBpm.toFixed(1)} BPM`);

	// Pass/fail
	console.log();
	if (error <= 1) {
		console.log("✓ PASS (within 1 BPM)");
	} else if (error <= 2) {
		console.log("~ CLOSE (within 2 BPM)");
	} else {
		console.log("✗ FAIL");
	}
}

main().catch(console.error);
