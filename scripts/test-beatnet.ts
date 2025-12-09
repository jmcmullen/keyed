#!/usr/bin/env bun
/**
 * BeatNet Test Suite
 *
 * Tests BPM detection accuracy against known recordings.
 * Uses ffmpeg to decode audio files and runs them through the BeatNet pipeline.
 *
 * Usage: bun run scripts/test-beatnet.ts
 */

import { execSync } from "node:child_process";
import { existsSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { InferenceSession, Tensor } from "onnxruntime-node";

// Import BeatNet components (these work in Node.js)
import {
	MEL_CONFIG,
	MelSpectrogramExtractor,
} from "../apps/native/lib/beatnet/mel-spectrogram";
import {
	combineOnsetAndActivationTempo,
	OnsetDetector,
} from "../apps/native/lib/beatnet/onset-detector";
import { CascadeParticleFilter } from "../apps/native/lib/beatnet/particle-filter";
import { TempoEstimator } from "../apps/native/lib/beatnet/tempo-estimator";

// ============================================================================
// Configuration
// ============================================================================

const TEST_DIR = join(import.meta.dir, "../apps/native/assets/test");
// Use Model 2 by default (better accuracy based on testing)
const MODEL_PATH = join(
	import.meta.dir,
	"../apps/native/assets/models/beatnet_model_2.onnx",
);
// Fallback to Model 1 if Model 2 not available
const MODEL_1_PATH = join(
	import.meta.dir,
	"../apps/native/assets/models/beatnet_model_1.onnx",
);
const BPM_TOLERANCE = 2.0; // ±2 BPM (realistic for beat tracking)
const SAMPLE_RATE = 22050;

// Model constants
const INPUT_DIM = 272;
const HIDDEN_DIM = 150;
const NUM_LAYERS = 2;

// ============================================================================
// Audio Decoding (using ffmpeg)
// ============================================================================

function decodeAudioFile(filePath: string): Float32Array {
	if (!existsSync(filePath)) {
		throw new Error(`File not found: ${filePath}`);
	}

	// Use ffmpeg to decode to raw PCM float32
	// Output: mono, 22050 Hz, 32-bit float, little-endian
	const cmd = `ffmpeg -i "${filePath}" -f f32le -acodec pcm_f32le -ac 1 -ar ${SAMPLE_RATE} -`;

	try {
		const buffer = execSync(cmd, {
			maxBuffer: 50 * 1024 * 1024, // 50MB max
			stdio: ["pipe", "pipe", "pipe"],
		});

		// Convert Buffer to Float32Array
		const samples = new Float32Array(buffer.length / 4);
		for (let i = 0; i < samples.length; i++) {
			samples[i] = buffer.readFloatLE(i * 4);
		}

		return samples;
	} catch (error) {
		throw new Error(
			`Failed to decode audio: ${error}. Make sure ffmpeg is installed.`,
		);
	}
}

// ============================================================================
// ONNX Model Wrapper for Node.js
// ============================================================================

class BeatNetModelNode {
	private session: InferenceSession | null = null;
	private hidden: Float32Array;
	private cell: Float32Array;
	private inferCount = 0;

	constructor() {
		this.hidden = new Float32Array(NUM_LAYERS * 1 * HIDDEN_DIM);
		this.cell = new Float32Array(NUM_LAYERS * 1 * HIDDEN_DIM);
	}

	async load(modelPath: string): Promise<void> {
		this.session = await InferenceSession.create(modelPath);
		console.log("    Model loaded successfully");
	}

	reset(): void {
		this.hidden.fill(0);
		this.cell.fill(0);
		this.inferCount = 0;
	}

	async infer(
		features: Float32Array,
	): Promise<{ beatActivation: number; downbeatActivation: number }> {
		if (!this.session) {
			throw new Error("Model not loaded");
		}

		if (features.length !== INPUT_DIM) {
			throw new Error(`Expected ${INPUT_DIM} features, got ${features.length}`);
		}

		this.inferCount++;

		// Create input tensors
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

		// Run inference
		const feeds = {
			input: inputTensor,
			hidden_in: hiddenTensor,
			cell_in: cellTensor,
		};

		const results = await this.session.run(feeds);

		// Extract outputs
		const output = results.output.data as Float32Array;
		const hiddenOut = results.hidden_out.data as Float32Array;
		const cellOut = results.cell_out.data as Float32Array;

		// Update hidden state
		this.hidden.set(hiddenOut);
		this.cell.set(cellOut);

		// Check if softmax was applied - if not, apply it
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

		// Debug: log every 500 frames
		if (this.inferCount % 500 === 1) {
			console.log(
				`    [Model] frame=${this.inferCount}: output=[${probs[0]?.toFixed(4)}, ${probs[1]?.toFixed(4)}, ${probs[2]?.toFixed(4)}]`,
			);
		}

		// Python model.py final_pred() outputs [beat, downbeat, non-beat]
		// So: beatActivation = probs[0], downbeatActivation = probs[1]
		return {
			beatActivation: probs[0],
			downbeatActivation: probs[1],
		};
	}
}

// ============================================================================
// BeatNet Processing (offline mode)
// ============================================================================

interface ProcessingResult {
	bpm: number;
	beatCount: number;
	beats: Array<{ time: number; type: 1 | 2 }>;
	processingTimeMs: number;
}

async function processBeatNet(
	samples: Float32Array,
	model: BeatNetModelNode,
): Promise<ProcessingResult> {
	const startTime = performance.now();

	// Initialize components
	const melExtractor = new MelSpectrogramExtractor();
	const particleFilter = new CascadeParticleFilter();
	const tempoEstimator = new TempoEstimator(50, 10); // 50 FPS, 10 second history
	const onsetDetector = new OnsetDetector(); // Independent onset-based tempo
	model.reset();

	// Process audio frame by frame
	const hopLength = MEL_CONFIG.hopLength;
	const winLength = MEL_CONFIG.winLength;
	const numFrames = Math.floor((samples.length - winLength) / hopLength) + 1;

	const beats: Array<{ time: number; type: 1 | 2 }> = [];
	let _frameCount = 0;

	for (let i = 0; i < numFrames; i++) {
		const start = i * hopLength;
		const frame = samples.subarray(start, start + winLength);

		// Extract features for NN
		const features = melExtractor.processFrame(frame);

		// Also process through onset detector (independent signal)
		onsetDetector.processFrame(frame);

		if (!features) continue;

		_frameCount++;

		// Run model inference
		const { beatActivation, downbeatActivation } = await model.infer(features);

		// Run particle filter
		const beat = particleFilter.processFrame(
			beatActivation,
			downbeatActivation,
		);

		// Update tempo estimator with combined activation and PF estimate
		const combinedActivation = Math.max(beatActivation, downbeatActivation);
		const pfBpmFrame = particleFilter.getCurrentBpm();
		const pfConfidence = particleFilter.isTempoLocked() ? 0.9 : 0.6;
		tempoEstimator.update(combinedActivation, pfBpmFrame, pfConfidence);

		if (beat) {
			beats.push(beat);
		}
	}

	const processingTimeMs = performance.now() - startTime;

	// Get tempo estimates from all sources
	const activationEstimate = tempoEstimator.getBestEstimate();
	const onsetEstimate = onsetDetector.getTempoEstimate();
	const pfBpm = particleFilter.getCurrentBpm(); // Use median (more stable)

	// Log all estimates for debugging
	console.log(
		`    [Sources] PF: ${pfBpm.toFixed(1)}, Activation: ${activationEstimate?.bpm.toFixed(1) ?? "N/A"} ` +
			`(conf: ${(activationEstimate?.confidence ?? 0).toFixed(2)}), ` +
			`Onset: ${onsetEstimate?.bpm.toFixed(1) ?? "N/A"} (conf: ${(onsetEstimate?.confidence ?? 0).toFixed(2)}, ` +
			`${onsetDetector.getOnsetCount()} onsets)`,
	);

	// Combine onset-based and activation-based estimates
	const combined = combineOnsetAndActivationTempo(
		onsetEstimate?.bpm ?? null,
		onsetEstimate?.confidence ?? 0,
		activationEstimate?.bpm ?? pfBpm,
		activationEstimate?.confidence ?? 0.5,
	);

	console.log(
		`    [Tempo] Final: ${combined.bpm.toFixed(1)} (${combined.method})`,
	);

	// Analyze inter-beat intervals for additional tempo evidence
	if (beats.length >= 3) {
		const intervals: number[] = [];
		for (let i = 1; i < beats.length; i++) {
			const interval = beats[i].time - beats[i - 1].time;
			if (interval > 0.2 && interval < 2.0) {
				// 30-300 BPM range
				intervals.push(interval);
			}
		}
		if (intervals.length >= 2) {
			// Sort and get median
			intervals.sort((a, b) => a - b);
			const medianInterval = intervals[Math.floor(intervals.length / 2)];
			const intervalBpm = 60 / medianInterval;
			console.log(
				`    [IBI] Median inter-beat interval: ${medianInterval.toFixed(3)}s = ${intervalBpm.toFixed(1)} BPM ` +
					`(${intervals.length} intervals)`,
			);
		}
	}

	return {
		bpm: combined.bpm,
		beatCount: beats.length,
		beats,
		processingTimeMs,
	};
}

// ============================================================================
// Test Runner
// ============================================================================

interface TestResult {
	file: string;
	expectedBpm: number;
	detectedBpm: number;
	bpmError: number;
	expectedBeats: number;
	detectedBeats: number;
	passed: boolean;
	processingTimeMs: number;
	durationSec: number;
}

async function runTest(
	filePath: string,
	model: BeatNetModelNode,
): Promise<TestResult> {
	const fileName = filePath.split("/").pop() || "";
	const expectedBpm = Number.parseInt(fileName.replace(/\.[^.]+$/, ""), 10);

	console.log(`\n  Testing: ${fileName} (expected ${expectedBpm} BPM)`);

	// Decode audio
	console.log("    Decoding audio...");
	const samples = decodeAudioFile(filePath);
	const durationSec = samples.length / SAMPLE_RATE;
	console.log(
		`    Duration: ${durationSec.toFixed(1)}s, Samples: ${samples.length}`,
	);

	// Analyze audio amplitude
	let maxAmp = 0;
	let sumAmp = 0;
	for (let i = 0; i < samples.length; i++) {
		const abs = Math.abs(samples[i]);
		maxAmp = Math.max(maxAmp, abs);
		sumAmp += abs;
	}
	console.log(
		`    Audio: max=${maxAmp.toFixed(4)}, avgAbs=${(sumAmp / samples.length).toFixed(4)}`,
	);

	// Process with BeatNet
	console.log("    Processing...");
	const result = await processBeatNet(samples, model);

	// Calculate expected beat count
	const expectedBeats = Math.round((durationSec * expectedBpm) / 60);

	// Check if passed
	const bpmError = Math.abs(result.bpm - expectedBpm);
	const passed = bpmError <= BPM_TOLERANCE;

	console.log(`    Detected: ${result.bpm.toFixed(1)} BPM`);
	console.log(`    Beats: ${result.beatCount} (expected ~${expectedBeats})`);
	console.log(`    Error: ${bpmError.toFixed(1)} BPM`);
	console.log(`    Status: ${passed ? "✓ PASS" : "✗ FAIL"}`);
	console.log(`    Time: ${result.processingTimeMs.toFixed(0)}ms`);

	return {
		file: fileName,
		expectedBpm,
		detectedBpm: result.bpm,
		bpmError,
		expectedBeats,
		detectedBeats: result.beatCount,
		passed,
		processingTimeMs: result.processingTimeMs,
		durationSec,
	};
}

// ============================================================================
// Main
// ============================================================================

async function main() {
	console.log("=".repeat(60));
	console.log("BeatNet Test Suite");
	console.log("=".repeat(60));
	console.log(`Test directory: ${TEST_DIR}`);
	console.log(`Model path: ${MODEL_PATH}`);
	console.log(`BPM tolerance: ±${BPM_TOLERANCE}`);

	// Check ffmpeg
	try {
		execSync("ffmpeg -version", { stdio: "pipe" });
		console.log("ffmpeg: available");
	} catch {
		console.error("ERROR: ffmpeg not found. Please install ffmpeg.");
		process.exit(1);
	}

	// Check model exists
	if (!existsSync(MODEL_PATH)) {
		console.error(`ERROR: Model not found: ${MODEL_PATH}`);
		process.exit(1);
	}

	// Load model (prefer Model 2, fallback to Model 1)
	console.log("Loading ONNX model...");
	const model = new BeatNetModelNode();
	if (existsSync(MODEL_PATH)) {
		console.log("  Using Model 2 (Ballroom-trained, better accuracy)");
		await model.load(MODEL_PATH);
	} else if (existsSync(MODEL_1_PATH)) {
		console.log("  Model 2 not found, falling back to Model 1");
		await model.load(MODEL_1_PATH);
	} else {
		console.error("ERROR: No model found!");
		process.exit(1);
	}

	// Find test files
	if (!existsSync(TEST_DIR)) {
		console.error(`ERROR: Test directory not found: ${TEST_DIR}`);
		process.exit(1);
	}

	const files = readdirSync(TEST_DIR)
		.filter(
			(f) => f.endsWith(".m4a") || f.endsWith(".mp3") || f.endsWith(".wav"),
		)
		.map((f) => join(TEST_DIR, f))
		.sort();

	if (files.length === 0) {
		console.error("ERROR: No test files found");
		process.exit(1);
	}

	console.log(`Found ${files.length} test files`);

	// Run tests
	const results: TestResult[] = [];
	for (const file of files) {
		try {
			const result = await runTest(file, model);
			results.push(result);
		} catch (error) {
			console.error(`  ERROR: ${error}`);
			results.push({
				file: file.split("/").pop() || "",
				expectedBpm: 0,
				detectedBpm: 0,
				bpmError: Number.POSITIVE_INFINITY,
				expectedBeats: 0,
				detectedBeats: 0,
				passed: false,
				processingTimeMs: 0,
				durationSec: 0,
			});
		}
	}

	// Summary
	console.log(`\n${"=".repeat(60)}`);
	console.log("SUMMARY");
	console.log("=".repeat(60));

	const passed = results.filter((r) => r.passed).length;
	const failed = results.length - passed;

	console.log(`\nResults: ${passed}/${results.length} passed`);
	console.log(
		`\n${"File".padEnd(15)} ${"Expected".padEnd(10)} ${"Detected".padEnd(10)} ${"Error".padEnd(8)} Status`,
	);
	console.log("-".repeat(60));

	for (const r of results) {
		const status = r.passed ? "✓" : "✗";
		console.log(
			`${r.file.padEnd(15)} ${r.expectedBpm.toString().padEnd(10)} ${r.detectedBpm.toFixed(1).padEnd(10)} ${r.bpmError.toFixed(1).padEnd(8)} ${status}`,
		);
	}

	// Exit with error if any failed
	if (failed > 0) {
		console.log(`\n${failed} test(s) failed`);
		process.exit(1);
	}

	console.log("\nAll tests passed!");
}

main().catch(console.error);
