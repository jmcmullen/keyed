import EngineModule from "@keyed/engine";
import type {
	BeatEvent,
	StateUpdate,
	WaveformData,
} from "@keyed/engine/src/Engine.types";
import { Asset } from "expo-asset";
import { useCallback, useEffect, useRef, useState } from "react";
import { Platform } from "react-native";
import { type SharedValue, useSharedValue } from "react-native-reanimated";

export type DetectionStatus =
	| "idle"
	| "initializing"
	| "listening"
	| "detected"
	| "error";

export interface BeatNetResult {
	bpm: number;
	currentBeat: 1 | 2 | 3 | 4;
	lastBeatTime: number;
	meter: number;
	phase: number;
	confidence: number;
	beatActivation: number;
	downbeatActivation: number;
}

export interface UseEngineOptions {
	onWaveform?: (data: WaveformData) => void;
}

export interface UseEngineReturn {
	status: DetectionStatus;
	isListening: boolean;
	result: BeatNetResult | null;
	currentBeat: SharedValue<1 | 2 | 3 | 4>;
	beatActivation: SharedValue<number>;
	downbeatActivation: SharedValue<number>;
	waveformSamples: SharedValue<number[]>;
	audioLevel: SharedValue<number>;
	error: string | null;
	startListening: () => Promise<void>;
	stopListening: () => Promise<void>;
	reset: () => void;
}

// Throttle interval for result state updates (ms)
const RESULT_UPDATE_INTERVAL = 100;

// Logging config - most logging now happens in C++ (check Xcode console)
// These are for JS-side tracking only
const LOG_BEATS_JS = true;
const LOG_DEBUG_INTERVAL = 25; // Log debug state every N frames

// Tracking for inter-beat intervals (JS side)
let lastBeatLogTime = 0;
let beatCount = 0;
let recentIntervals: number[] = [];
let frameCount = 0;

function logBeatJS(
	type: "beat" | "downbeat",
	bpm: number,
	debug: {
		tempoLocked: boolean;
		tempoConfidence: number;
		wasGated: boolean;
	},
) {
	if (!LOG_BEATS_JS) return;

	beatCount++;
	const now = Date.now();
	const interval = lastBeatLogTime > 0 ? now - lastBeatLogTime : 0;
	lastBeatLogTime = now;

	if (interval > 0) {
		recentIntervals.push(interval);
		if (recentIntervals.length > 8) recentIntervals.shift();
	}

	const expectedInterval = bpm > 0 ? 60000 / bpm : 0;
	const deviation =
		expectedInterval > 0
			? ((interval - expectedInterval) / expectedInterval) * 100
			: 0;

	const avgInterval =
		recentIntervals.length > 0
			? recentIntervals.reduce((a, b) => a + b, 0) / recentIntervals.length
			: 0;
	const actualBpm = avgInterval > 0 ? Math.round(60000 / avgInterval) : 0;

	console.log(
		`[JS-BEAT] #${beatCount} ${type.toUpperCase()} | ` +
			`int=${interval}ms (${deviation > 0 ? "+" : ""}${deviation.toFixed(0)}%) | ` +
			`actual=${actualBpm} BPM | ` +
			`lock=${debug.tempoLocked ? "YES" : "no"} conf=${(debug.tempoConfidence * 100).toFixed(0)}%`,
	);
}

function logDebugState(debug: {
	bpm: number;
	tempoLocked: boolean;
	tempoConfidence: number;
	wasGated: boolean;
	isNearBeat: boolean;
	timingOk: boolean;
	beatDecision: number;
	beatActivation: number;
	downbeatActivation: number;
}) {
	frameCount++;
	if (frameCount % LOG_DEBUG_INTERVAL !== 0) return;

	const decisionLabels = ["none", "downbeat", "beat", "suppressed"];
	const decision = decisionLabels[debug.beatDecision] || "?";

	console.log(
		`[JS-STATE] BPM=${debug.bpm.toFixed(1)} | ` +
			`lock=${debug.tempoLocked ? "YES" : "no"} (${(debug.tempoConfidence * 100).toFixed(0)}%) | ` +
			`near=${debug.isNearBeat ? 1 : 0} timing=${debug.timingOk ? 1 : 0} | ` +
			`act=${(debug.beatActivation * 100).toFixed(0)}/${(debug.downbeatActivation * 100).toFixed(0)}% ` +
			`${debug.wasGated ? "[GATED]" : ""} | ` +
			`decision=${decision}`,
	);
}

export function useEngine(options: UseEngineOptions = {}): UseEngineReturn {
	const { onWaveform } = options;

	const [status, setStatus] = useState<DetectionStatus>("idle");
	const [result, setResult] = useState<BeatNetResult | null>(null);
	const [error, setError] = useState<string | null>(null);
	const [isListening, setIsListening] = useState(false);

	const currentBeat = useSharedValue<1 | 2 | 3 | 4>(1);
	const beatActivation = useSharedValue(0);
	const downbeatActivation = useSharedValue(0);
	const waveformSamples = useSharedValue<number[]>([]);
	const audioLevel = useSharedValue(0);

	const beatInBarRef = useRef<number>(1);
	const lastBeatTimeRef = useRef<number>(0);
	const latestBpmRef = useRef<number>(0);
	const lastResultUpdateRef = useRef<number>(0);
	const pendingResultRef = useRef<BeatNetResult | null>(null);
	const onWaveformRef = useRef(onWaveform);
	const resultRef = useRef(result);
	const lastDebugRef = useRef({
		tempoLocked: false,
		tempoConfidence: 0,
		wasGated: false,
	});

	// Keep refs in sync
	onWaveformRef.current = onWaveform;
	resultRef.current = result;

	useEffect(() => {
		let mounted = true;

		async function initModel() {
			try {
				setStatus("initializing");

				// Load ONNX model asset
				const modelAsset = Asset.fromModule(
					require("@/assets/models/beatnet_model_2.onnx"),
				);
				await modelAsset.downloadAsync();

				if (!mounted) return;

				if (!modelAsset.localUri) {
					throw new Error("Failed to load BeatNet model asset");
				}

				const modelPath =
					Platform.OS === "android"
						? modelAsset.localUri
						: modelAsset.localUri.replace("file://", "");

				// Load model into native engine
				const loaded = EngineModule.loadModel(modelPath);
				if (!loaded) {
					throw new Error("Failed to load ONNX model in native engine");
				}

				console.log("[useEngine] Native ONNX model loaded successfully");
				setStatus("idle");
			} catch (err) {
				console.error("[useEngine] Initialization error:", err);
				if (mounted) {
					setError(err instanceof Error ? err.message : "Failed to initialize");
					setStatus("error");
				}
			}
		}

		initModel();

		return () => {
			mounted = false;
		};
	}, []);

	// biome-ignore lint/correctness/useExhaustiveDependencies: SharedValues are stable refs
	useEffect(() => {
		const beatSubscription = EngineModule.addListener(
			"onBeat",
			(event: BeatEvent) => {
				lastBeatTimeRef.current = event.time;

				const isDownbeat = event.type === 1;
				logBeatJS(isDownbeat ? "downbeat" : "beat", latestBpmRef.current, {
					tempoLocked: lastDebugRef.current.tempoLocked,
					tempoConfidence: lastDebugRef.current.tempoConfidence,
					wasGated: lastDebugRef.current.wasGated,
				});

				if (isDownbeat) {
					currentBeat.value = 1;
					beatInBarRef.current = 1;
				} else {
					beatInBarRef.current = (beatInBarRef.current % 4) + 1;
					currentBeat.value = beatInBarRef.current as 1 | 2 | 3 | 4;
				}
			},
		);

		const stateSubscription = EngineModule.addListener(
			"onStateUpdate",
			(event: StateUpdate) => {
				beatActivation.value = event.beatActivation;
				downbeatActivation.value = event.downbeatActivation;

				// Update debug ref for beat handler
				lastDebugRef.current = {
					tempoLocked: event.tempoLocked,
					tempoConfidence: event.tempoConfidence,
					wasGated: event.wasGated,
				};

				// Log debug state periodically
				logDebugState({
					bpm: event.bpm,
					tempoLocked: event.tempoLocked,
					tempoConfidence: event.tempoConfidence,
					wasGated: event.wasGated,
					isNearBeat: event.isNearBeat,
					timingOk: event.timingOk,
					beatDecision: event.beatDecision,
					beatActivation: event.beatActivation,
					downbeatActivation: event.downbeatActivation,
				});

				if (event.bpm > 0) {
					const bpmChange = Math.abs(event.bpm - latestBpmRef.current);
					if (latestBpmRef.current === 0 || bpmChange > 2.0) {
						// Log significant BPM changes
						if (latestBpmRef.current === 0) {
							console.log(
								`[JS] Initial BPM: ${event.bpm.toFixed(1)} bpm (conf: ${(event.confidence * 100).toFixed(0)}%, tempoConf: ${(event.tempoConfidence * 100).toFixed(0)}%)`,
							);
						}
						latestBpmRef.current = event.bpm;
					}
				}

				const newResult: BeatNetResult = {
					bpm: latestBpmRef.current,
					currentBeat: currentBeat.value,
					lastBeatTime: lastBeatTimeRef.current,
					meter: event.meter,
					phase: event.phase,
					confidence: event.confidence,
					beatActivation: event.beatActivation,
					downbeatActivation: event.downbeatActivation,
				};

				const now = Date.now();
				if (now - lastResultUpdateRef.current >= RESULT_UPDATE_INTERVAL) {
					lastResultUpdateRef.current = now;
					setResult(newResult);
					if (latestBpmRef.current > 0) {
						setStatus("detected");
					}
				} else {
					pendingResultRef.current = newResult;
				}
			},
		);

		const waveformSubscription = EngineModule.addListener(
			"onWaveform",
			(event: WaveformData) => {
				waveformSamples.value = event.samples;
				audioLevel.value = event.rms;
				onWaveformRef.current?.(event);
			},
		);

		return () => {
			beatSubscription.remove();
			stateSubscription.remove();
			waveformSubscription.remove();
		};
	}, []);

	// biome-ignore lint/correctness/useExhaustiveDependencies: SharedValues are stable refs
	const startListening = useCallback(async () => {
		if (!EngineModule.isModelReady()) {
			setError("Model not initialized");
			setStatus("error");
			return;
		}

		try {
			setError(null);

			const permissionStatus = EngineModule.getPermissionStatus();
			if (permissionStatus !== "granted") {
				const granted = await EngineModule.requestPermission();
				if (!granted) {
					throw new Error("Microphone permission denied");
				}
			}

			setResult(null);
			latestBpmRef.current = 0;
			lastBeatTimeRef.current = 0;
			beatInBarRef.current = 1;
			lastResultUpdateRef.current = 0;
			pendingResultRef.current = null;
			currentBeat.value = 1;
			beatActivation.value = 0;
			downbeatActivation.value = 0;
			waveformSamples.value = [];
			audioLevel.value = 0;

			// Reset logging state
			lastBeatLogTime = 0;
			beatCount = 0;
			recentIntervals = [];
			frameCount = 0;
			console.log(
				"[JS] Starting beat detection... (C++ logs in Xcode console)",
			);

			const started = await EngineModule.startRecording(true);
			if (!started) {
				throw new Error("Failed to start native audio recording");
			}

			setIsListening(true);
			setStatus("listening");
			console.log("[useEngine] Native recording started");
		} catch (err) {
			const message =
				err instanceof Error ? err.message : "Failed to start listening";
			console.error("[useEngine] Start error:", message, err);
			setError(message);
			setStatus("error");
		}
	}, []);

	const stopListening = useCallback(async () => {
		EngineModule.stopRecording();
		setIsListening(false);
		setStatus(resultRef.current ? "detected" : "idle");

		// Log session summary
		const avgInterval =
			recentIntervals.length > 0
				? recentIntervals.reduce((a, b) => a + b, 0) / recentIntervals.length
				: 0;
		const actualBpm = avgInterval > 0 ? 60000 / avgInterval : 0;
		console.log(
			`[JS] Stopped. ${beatCount} beats | ` +
				`Reported: ${latestBpmRef.current.toFixed(1)} BPM | ` +
				`Actual from intervals: ${actualBpm.toFixed(1)} BPM`,
		);
	}, []);

	// biome-ignore lint/correctness/useExhaustiveDependencies: SharedValues are stable refs
	const reset = useCallback(() => {
		EngineModule.stopRecording();
		EngineModule.reset();

		latestBpmRef.current = 0;
		lastBeatTimeRef.current = 0;
		beatInBarRef.current = 1;
		lastResultUpdateRef.current = 0;
		pendingResultRef.current = null;
		setStatus("idle");
		setResult(null);
		setIsListening(false);
		setError(null);
		currentBeat.value = 1;
		beatActivation.value = 0;
		downbeatActivation.value = 0;
		waveformSamples.value = [];
		audioLevel.value = 0;

		// Reset logging state
		lastBeatLogTime = 0;
		beatCount = 0;
		recentIntervals = [];
		frameCount = 0;
		console.log("[JS] Reset complete");
	}, []);

	return {
		status,
		isListening,
		result,
		currentBeat,
		beatActivation,
		downbeatActivation,
		waveformSamples,
		audioLevel,
		error,
		startListening,
		stopListening,
		reset,
	};
}
