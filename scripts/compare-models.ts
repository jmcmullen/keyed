#!/usr/bin/env bun
/**
 * Compare Model 1 vs Model 2 outputs to quantify differences
 */

import { execSync } from "node:child_process";
import { join } from "node:path";
import { InferenceSession, Tensor } from "onnxruntime-node";

import {
	MEL_CONFIG,
	MelSpectrogramExtractor,
} from "../apps/native/lib/beatnet/mel-spectrogram";

const TEST_FILE = join(import.meta.dir, "../apps/native/assets/test/125.m4a");
const MODEL_1_PATH = join(
	import.meta.dir,
	"../apps/native/assets/models/beatnet_model_1.onnx",
);
const MODEL_2_PATH = join(
	import.meta.dir,
	"../apps/native/assets/models/beatnet_model_2.onnx",
);
const SAMPLE_RATE = 22050;
const INPUT_DIM = 272;
const HIDDEN_DIM = 150;
const NUM_LAYERS = 2;

function decodeAudioFile(filePath: string): Float32Array {
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

async function runModel(
	session: InferenceSession,
	features: Float32Array,
	hidden: Float32Array,
	cell: Float32Array,
): Promise<{
	probs: Float32Array;
	hidden: Float32Array;
	cell: Float32Array;
}> {
	const inputTensor = new Tensor("float32", features, [1, 1, INPUT_DIM]);
	const hiddenTensor = new Tensor("float32", hidden, [
		NUM_LAYERS,
		1,
		HIDDEN_DIM,
	]);
	const cellTensor = new Tensor("float32", cell, [NUM_LAYERS, 1, HIDDEN_DIM]);

	const results = await session.run({
		input: inputTensor,
		hidden_in: hiddenTensor,
		cell_in: cellTensor,
	});

	const output = results.output.data as Float32Array;
	const hiddenOut = results.hidden_out.data as Float32Array;
	const cellOut = results.cell_out.data as Float32Array;

	// Apply softmax if needed
	const sum = output[0] + output[1] + output[2];
	let probs: Float32Array;
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
		probs,
		hidden: new Float32Array(hiddenOut),
		cell: new Float32Array(cellOut),
	};
}

async function main() {
	console.log("=".repeat(60));
	console.log("Model 1 vs Model 2 Comparison");
	console.log("=".repeat(60));

	// Load audio
	console.log("\nLoading audio...");
	const samples = decodeAudioFile(TEST_FILE);
	console.log(
		`Loaded ${samples.length} samples (${(samples.length / SAMPLE_RATE).toFixed(1)}s)`,
	);

	// Load models
	console.log("\nLoading models...");
	const session1 = await InferenceSession.create(MODEL_1_PATH);
	const session2 = await InferenceSession.create(MODEL_2_PATH);
	console.log("Both models loaded");

	// Extract features
	const extractor = new MelSpectrogramExtractor();
	const { hopLength, winLength } = MEL_CONFIG;
	const numFrames = Math.floor((samples.length - winLength) / hopLength) + 1;

	// Process first 500 frames (~10 seconds)
	const framesToProcess = Math.min(numFrames, 500);

	let hidden1 = new Float32Array(NUM_LAYERS * HIDDEN_DIM);
	let cell1 = new Float32Array(NUM_LAYERS * HIDDEN_DIM);
	let hidden2 = new Float32Array(NUM_LAYERS * HIDDEN_DIM);
	let cell2 = new Float32Array(NUM_LAYERS * HIDDEN_DIM);

	// Store all activations for analysis
	const model1Activations: number[] = [];
	const model2Activations: number[] = [];

	console.log(`\nProcessing ${framesToProcess} frames...`);

	for (let i = 0; i < framesToProcess; i++) {
		const start = i * hopLength;
		const frame = samples.subarray(start, start + winLength);
		const features = extractor.processFrame(frame);
		if (!features) continue;

		// Run both models
		const result1 = await runModel(session1, features, hidden1, cell1);
		const result2 = await runModel(session2, features, hidden2, cell2);

		hidden1 = result1.hidden;
		cell1 = result1.cell;
		hidden2 = result2.hidden;
		cell2 = result2.cell;

		const beat1 = Math.max(result1.probs[0], result1.probs[1]);
		const beat2 = Math.max(result2.probs[0], result2.probs[1]);

		model1Activations.push(beat1);
		model2Activations.push(beat2);

		// Log comparison every 100 frames
		if (i > 0 && i % 100 === 0) {
			console.log(`\n--- Frame ${i} (${(i / 50).toFixed(1)}s) ---`);
			console.log(
				`Model 1: beat=${result1.probs[0].toFixed(3)}, down=${result1.probs[1].toFixed(3)}`,
			);
			console.log(
				`Model 2: beat=${result2.probs[0].toFixed(3)}, down=${result2.probs[1].toFixed(3)}`,
			);
		}
	}

	// Find local maxima (peaks) using proper peak detection
	function findPeaks(values: number[], minHeight: number): number[] {
		const peaks: number[] = [];
		for (let i = 2; i < values.length - 2; i++) {
			if (
				values[i] > minHeight &&
				values[i] > values[i - 1] &&
				values[i] > values[i - 2] &&
				values[i] >= values[i + 1] &&
				values[i] >= values[i + 2]
			) {
				peaks.push(i);
			}
		}
		return peaks;
	}

	// Find peaks with adaptive threshold (mean + 2*stddev)
	function calcThreshold(values: number[]): number {
		const avg = values.reduce((a, b) => a + b, 0) / values.length;
		const variance =
			values.reduce((sum, v) => sum + (v - avg) ** 2, 0) / values.length;
		return Math.max(0.5, avg + 2 * Math.sqrt(variance));
	}

	const threshold1 = calcThreshold(model1Activations);
	const threshold2 = calcThreshold(model2Activations);

	console.log(`\nModel 1 threshold: ${threshold1.toFixed(3)}`);
	console.log(`Model 2 threshold: ${threshold2.toFixed(3)}`);

	const model1Peaks = findPeaks(model1Activations, threshold1);
	const model2Peaks = findPeaks(model2Activations, threshold2);

	// Calculate intervals
	function calcIntervals(peaks: number[]): number[] {
		const intervals: number[] = [];
		for (let i = 1; i < peaks.length; i++) {
			intervals.push(peaks[i] - peaks[i - 1]);
		}
		return intervals;
	}

	const model1Intervals = calcIntervals(model1Peaks);
	const model2Intervals = calcIntervals(model2Peaks);

	// Analyze results
	console.log(`\n${"=".repeat(60)}`);
	console.log("RESULTS");
	console.log("=".repeat(60));

	console.log(`\nModel 1 peak count: ${model1Peaks.length}`);
	console.log(`Model 2 peak count: ${model2Peaks.length}`);

	// Show peak positions
	console.log(
		`\nModel 1 peak frames: ${model1Peaks.slice(0, 15).join(", ")}...`,
	);
	console.log(`Model 2 peak frames: ${model2Peaks.slice(0, 15).join(", ")}...`);

	if (model1Intervals.length > 2) {
		const sorted = [...model1Intervals].sort((a, b) => a - b);
		const medianInterval1 = sorted[Math.floor(sorted.length / 2)];
		const bpm1 = 3000 / medianInterval1; // 50 FPS * 60 sec
		console.log(
			`\nModel 1 median interval: ${medianInterval1} frames = ${bpm1.toFixed(1)} BPM`,
		);
		console.log(`  Intervals: ${model1Intervals.slice(0, 10).join(", ")}...`);
	}

	if (model2Intervals.length > 2) {
		const sorted = [...model2Intervals].sort((a, b) => a - b);
		const medianInterval2 = sorted[Math.floor(sorted.length / 2)];
		const bpm2 = 3000 / medianInterval2;
		console.log(
			`\nModel 2 median interval: ${medianInterval2} frames = ${bpm2.toFixed(1)} BPM`,
		);
		console.log(`  Intervals: ${model2Intervals.slice(0, 10).join(", ")}...`);
	}

	// Analyze activation distribution
	console.log("\n--- Activation Distribution ---");
	const stats = (arr: number[]) => {
		const sorted = [...arr].sort((a, b) => a - b);
		return {
			min: sorted[0],
			max: sorted[sorted.length - 1],
			median: sorted[Math.floor(sorted.length / 2)],
			p90: sorted[Math.floor(sorted.length * 0.9)],
		};
	};

	const s1 = stats(model1Activations);
	const s2 = stats(model2Activations);
	console.log(
		`Model 1: min=${s1.min.toFixed(3)}, median=${s1.median.toFixed(3)}, p90=${s1.p90.toFixed(3)}, max=${s1.max.toFixed(3)}`,
	);
	console.log(
		`Model 2: min=${s2.min.toFixed(3)}, median=${s2.median.toFixed(3)}, p90=${s2.p90.toFixed(3)}, max=${s2.max.toFixed(3)}`,
	);

	console.log("\n✓ Expected BPM: 125");
	console.log(
		"✓ Expected interval at 125 BPM: 24 frames (~4 beats per 100 frames)",
	);
}

main().catch(console.error);
