/**
 * Comprehensive BeatNet Optimization Tests
 *
 * Tests all streaming accuracy optimizations:
 * 1. Model 2 vs Model 1 comparison
 * 2. Increased particle count
 * 3. Tempo locking
 * 4. Octave correction
 * 5. Lookahead buffer
 */

import fs from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { MelSpectrogramExtractor } from "../apps/native/lib/beatnet/mel-spectrogram";
import {
	CascadeParticleFilter,
	DEFAULT_PF_CONFIG,
} from "../apps/native/lib/beatnet/particle-filter";

const require = createRequire(import.meta.url);

// ONNX Runtime for Node.js
const ort = require("onnxruntime-node");

// ONNX session type
type OnnxSession = Awaited<ReturnType<typeof ort.InferenceSession.create>>;

interface TestResult {
	file: string;
	expectedBpm: number;
	detectedBpm: number;
	correctedBpm: number;
	error: number;
	correctedError: number;
	passed: boolean;
	tempoLocked: boolean;
	octaveCorrected: boolean;
	beatCount: number;
}

interface ModelTestResult {
	modelName: string;
	results: TestResult[];
	passRate: number;
	avgError: number;
}

// Test files
const TEST_DIR = path.join(import.meta.dirname, "../apps/native/assets/test");
const MODELS_DIR = path.join(
	import.meta.dirname,
	"../apps/native/assets/models",
);

// Model paths
const MODEL_1_PATH = path.join(MODELS_DIR, "beatnet_model_1.onnx");
const MODEL_2_PATH = path.join(MODELS_DIR, "beatnet_model_2.onnx");

async function decodeAudio(filepath: string): Promise<Float32Array> {
	const ffmpeg = require("fluent-ffmpeg");

	return new Promise((resolve, reject) => {
		const chunks: Buffer[] = [];

		ffmpeg(filepath)
			.audioFrequency(22050)
			.audioChannels(1)
			.format("f32le")
			.on("error", reject)
			.pipe()
			.on("data", (chunk: Buffer) => chunks.push(chunk))
			.on("end", () => {
				const buffer = Buffer.concat(chunks);
				const samples = new Float32Array(buffer.length / 4);
				for (let i = 0; i < samples.length; i++) {
					samples[i] = buffer.readFloatLE(i * 4);
				}
				resolve(samples);
			})
			.on("error", reject);
	});
}

async function runModelInference(
	session: OnnxSession,
	features: Float32Array,
	hidden: Float32Array,
	cell: Float32Array,
): Promise<{
	output: Float32Array;
	hidden: Float32Array;
	cell: Float32Array;
}> {
	const inputTensor = new ort.Tensor("float32", features, [1, 1, 272]);
	const hiddenTensor = new ort.Tensor("float32", hidden, [2, 1, 150]);
	const cellTensor = new ort.Tensor("float32", cell, [2, 1, 150]);

	const feeds = {
		input: inputTensor,
		hidden_in: hiddenTensor,
		cell_in: cellTensor,
	};

	const results = await session.run(feeds);

	return {
		output: results.output.data as Float32Array,
		hidden: results.hidden_out.data as Float32Array,
		cell: results.cell_out.data as Float32Array,
	};
}

async function testFile(
	filepath: string,
	expectedBpm: number,
	modelSession: OnnxSession,
	config: Partial<typeof DEFAULT_PF_CONFIG> = {},
): Promise<TestResult> {
	// Decode audio
	const audio = await decodeAudio(filepath);

	// Initialize components
	const extractor = new MelSpectrogramExtractor();
	const pf = new CascadeParticleFilter(config);

	// LSTM state
	let hidden = new Float32Array(2 * 1 * 150);
	let cell = new Float32Array(2 * 1 * 150);

	// Process in chunks
	const hopSize = 441; // 20ms at 22050Hz
	let beatCount = 0;

	for (let i = 0; i + hopSize <= audio.length; i += hopSize) {
		const chunk = audio.slice(i, i + hopSize);
		extractor.process(chunk);
		const features = extractor.getFeatures();

		if (!features) continue;

		// Run model
		const result = await runModelInference(
			modelSession,
			features,
			hidden,
			cell,
		);
		hidden = result.hidden;
		cell = result.cell;

		// Get activations
		const output = result.output;
		const beatAct = output[0];
		const downbeatAct = output[1];

		// Process with particle filter
		const beat = pf.processFrame(beatAct, downbeatAct);
		if (beat) beatCount++;
	}

	// Get results
	const detectedBpm = pf.getCurrentBpm();
	const correctedBpm = pf.getCorrectedBpm();
	const tempoLocked = pf.isTempoLocked();
	const octaveCorrected = pf.isOctaveCorrected();

	const error = Math.abs(detectedBpm - expectedBpm);
	const correctedError = Math.abs(correctedBpm - expectedBpm);
	const passed = correctedError <= 2;

	return {
		file: path.basename(filepath),
		expectedBpm,
		detectedBpm,
		correctedBpm,
		error,
		correctedError,
		passed,
		tempoLocked,
		octaveCorrected,
		beatCount,
	};
}

async function testModel(
	modelPath: string,
	modelName: string,
	config: Partial<typeof DEFAULT_PF_CONFIG> = {},
): Promise<ModelTestResult> {
	console.log(`\n${"=".repeat(60)}`);
	console.log(`Testing ${modelName}`);
	console.log(
		`Config: particles=${config.particleSize || DEFAULT_PF_CONFIG.particleSize}`,
	);
	console.log(`${"=".repeat(60)}\n`);

	const session = await ort.InferenceSession.create(modelPath);

	const testFiles = fs
		.readdirSync(TEST_DIR)
		.filter((f) => f.endsWith(".m4a"))
		.sort();

	const results: TestResult[] = [];

	for (const file of testFiles) {
		const expectedBpm = Number.parseFloat(file.replace(".m4a", ""));
		const filepath = path.join(TEST_DIR, file);

		try {
			console.log(`  Testing: ${file} (expected ${expectedBpm} BPM)`);
			const result = await testFile(filepath, expectedBpm, session, config);
			results.push(result);

			const status = result.passed ? "✓ PASS" : "✗ FAIL";
			console.log(
				`    Detected: ${result.detectedBpm.toFixed(1)} BPM ` +
					`(corrected: ${result.correctedBpm.toFixed(1)})`,
			);
			console.log(`    Error: ${result.correctedError.toFixed(1)} BPM`);
			console.log(`    Tempo locked: ${result.tempoLocked}`);
			console.log(`    Octave corrected: ${result.octaveCorrected}`);
			console.log(`    Status: ${status}\n`);
		} catch (error) {
			console.error(`    Error: ${error}`);
			results.push({
				file,
				expectedBpm,
				detectedBpm: 0,
				correctedBpm: 0,
				error: 999,
				correctedError: 999,
				passed: false,
				tempoLocked: false,
				octaveCorrected: false,
				beatCount: 0,
			});
		}
	}

	const passRate = results.filter((r) => r.passed).length / results.length;
	const avgError =
		results.reduce((sum, r) => sum + r.correctedError, 0) / results.length;

	return { modelName, results, passRate, avgError };
}

async function main() {
	console.log("BeatNet Optimization Test Suite");
	console.log("================================\n");

	const allResults: ModelTestResult[] = [];

	// Test 1: Model 1 with default config (baseline)
	if (fs.existsSync(MODEL_1_PATH)) {
		const result = await testModel(MODEL_1_PATH, "Model 1 (Baseline)", {
			particleSize: 1500,
			downParticleSize: 250,
			tempoLockEnabled: false,
			octaveCorrectionEnabled: false,
		});
		allResults.push(result);
	}

	// Test 2: Model 2 with default config
	if (fs.existsSync(MODEL_2_PATH)) {
		const result = await testModel(MODEL_2_PATH, "Model 2 (Improved)", {
			particleSize: 1500,
			downParticleSize: 250,
			tempoLockEnabled: false,
			octaveCorrectionEnabled: false,
		});
		allResults.push(result);
	}

	// Test 3: Model 2 with increased particles
	if (fs.existsSync(MODEL_2_PATH)) {
		const result = await testModel(MODEL_2_PATH, "Model 2 + More Particles", {
			particleSize: 2500,
			downParticleSize: 400,
			tempoLockEnabled: false,
			octaveCorrectionEnabled: false,
		});
		allResults.push(result);
	}

	// Test 4: Model 2 with tempo locking
	if (fs.existsSync(MODEL_2_PATH)) {
		const result = await testModel(MODEL_2_PATH, "Model 2 + Tempo Lock", {
			particleSize: 2500,
			downParticleSize: 400,
			tempoLockEnabled: true,
			octaveCorrectionEnabled: false,
		});
		allResults.push(result);
	}

	// Test 5: Model 2 with all optimizations
	if (fs.existsSync(MODEL_2_PATH)) {
		const result = await testModel(
			MODEL_2_PATH,
			"Model 2 + ALL Optimizations",
			DEFAULT_PF_CONFIG,
		);
		allResults.push(result);
	}

	// Summary
	console.log(`\n${"=".repeat(60)}`);
	console.log("SUMMARY");
	console.log(`${"=".repeat(60)}\n`);

	console.log("| Configuration              | Pass Rate | Avg Error |");
	console.log("|---------------------------|-----------|-----------|");

	for (const result of allResults) {
		console.log(
			`| ${result.modelName.padEnd(25)} | ${(result.passRate * 100).toFixed(0)}%       | ${result.avgError.toFixed(1)} BPM    |`,
		);
	}

	// Individual file comparison
	console.log("\n\nPer-File Comparison:");
	console.log("-".repeat(80));

	const files = allResults[0]?.results.map((r) => r.file) || [];
	for (const file of files) {
		console.log(`\n${file}:`);
		for (const modelResult of allResults) {
			const fileResult = modelResult.results.find((r) => r.file === file);
			if (fileResult) {
				const status = fileResult.passed ? "✓" : "✗";
				console.log(
					`  ${modelResult.modelName.padEnd(28)} ${status} ${fileResult.correctedBpm.toFixed(1).padStart(6)} BPM (err: ${fileResult.correctedError.toFixed(1)})`,
				);
			}
		}
	}

	console.log("\n\nTest completed!");
}

main().catch(console.error);
