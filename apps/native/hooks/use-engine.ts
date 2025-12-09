import EngineModule from "@keyed/engine";
import type { State, WaveformData } from "@keyed/engine/src/Engine.types";
import { useCallback, useEffect, useRef, useState } from "react";
import { type SharedValue, useSharedValue } from "react-native-reanimated";

export type DetectionStatus =
	| "idle"
	| "initializing"
	| "listening"
	| "detected"
	| "error";

export interface BeatNetResult {
	/** Current BPM from autocorrelation (0 until enough data, improves over time) */
	bpm: number;
	/** Number of frames collected (more frames = more accurate) */
	frameCount: number;
	beatActivation: number;
	downbeatActivation: number;
}

export interface UseEngineOptions {
	onWaveform?: (data: WaveformData) => void;
}

export interface FrequencyBands {
	low: number;
	mid: number;
	high: number;
}

export interface UseEngineReturn {
	status: DetectionStatus;
	isListening: boolean;
	result: BeatNetResult | null;
	beatActivation: SharedValue<number>;
	downbeatActivation: SharedValue<number>;
	waveformSamples: SharedValue<number[]>;
	waveformBands: SharedValue<FrequencyBands>;
	audioLevel: SharedValue<number>;
	error: string | null;
	startListening: () => Promise<void>;
	stopListening: () => void;
	reset: () => void;
}

const RESULT_UPDATE_INTERVAL = 100;

export function useEngine(options: UseEngineOptions = {}): UseEngineReturn {
	const { onWaveform } = options;

	const [status, setStatus] = useState<DetectionStatus>("idle");
	const [result, setResult] = useState<BeatNetResult | null>(null);
	const [error, setError] = useState<string | null>(null);
	const [isListening, setIsListening] = useState(false);

	const beatActivation = useSharedValue(0);
	const downbeatActivation = useSharedValue(0);
	const waveformSamples = useSharedValue<number[]>([]);
	const waveformBands = useSharedValue({ low: 0.33, mid: 0.33, high: 0.34 });
	const audioLevel = useSharedValue(0);

	const latestBpmRef = useRef<number>(0);
	const lastResultUpdateRef = useRef<number>(0);
	const lastBpmPollRef = useRef<number>(0);
	const pendingResultRef = useRef<BeatNetResult | null>(null);
	const onWaveformRef = useRef(onWaveform);
	const resultRef = useRef(result);
	const isListeningRef = useRef(false);

	onWaveformRef.current = onWaveform;
	resultRef.current = result;

	useEffect(() => {
		setStatus("initializing");

		// Load bundled model from native code
		const loaded = EngineModule.loadModel();
		if (!loaded) {
			setError("Failed to load ONNX model");
			setStatus("error");
			return;
		}

		setStatus("idle");
	}, []);

	// biome-ignore lint/correctness/useExhaustiveDependencies: SharedValues are stable refs
	useEffect(() => {
		const stateSubscription = EngineModule.addListener(
			"onState",
			(event: State) => {
				if (!isListeningRef.current) return;

				beatActivation.value = event.beatActivation;
				downbeatActivation.value = event.downbeatActivation;

				// Poll cached autocorrelation BPM every 500ms
				const now = Date.now();
				const shouldPollBpm = now - lastBpmPollRef.current >= 500;

				let bpm = latestBpmRef.current;
				let frameCount = 0;

				if (shouldPollBpm) {
					lastBpmPollRef.current = now;
					frameCount = EngineModule.getFrameCount();

					// Only read cached BPM if we have enough frames
					if (frameCount >= 100) {
						bpm = EngineModule.getBpm();

						if (bpm !== latestBpmRef.current && bpm > 0) {
							latestBpmRef.current = bpm;
						}
					}
				}

				const newResult: BeatNetResult = {
					bpm: bpm > 0 ? bpm : 0,
					frameCount,
					beatActivation: event.beatActivation,
					downbeatActivation: event.downbeatActivation,
				};

				if (now - lastResultUpdateRef.current >= RESULT_UPDATE_INTERVAL) {
					lastResultUpdateRef.current = now;
					setResult(newResult);
					if (bpm > 0) {
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
				if (!isListeningRef.current) return;

				waveformSamples.value = event.samples;
				waveformBands.value = {
					low: event.low,
					mid: event.mid,
					high: event.high,
				};
				audioLevel.value = event.rms;
				onWaveformRef.current?.(event);
			},
		);

		return () => {
			stateSubscription.remove();
			waveformSubscription.remove();
		};
	}, []);

	// biome-ignore lint/correctness/useExhaustiveDependencies: SharedValues are stable refs
	const startListening = useCallback(async () => {
		if (!EngineModule.isReady()) {
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
			lastResultUpdateRef.current = 0;
			lastBpmPollRef.current = 0;
			pendingResultRef.current = null;
			beatActivation.value = 0;
			downbeatActivation.value = 0;
			waveformSamples.value = [];
			waveformBands.value = { low: 0.33, mid: 0.33, high: 0.34 };
			audioLevel.value = 0;

			const started = await EngineModule.startRecording(true);
			if (!started) {
				throw new Error("Failed to start native audio recording");
			}

			isListeningRef.current = true;
			setIsListening(true);
			setStatus("listening");
		} catch (err) {
			const message =
				err instanceof Error ? err.message : "Failed to start listening";
			console.error("[useEngine] Start error:", message, err);
			setError(message);
			setStatus("error");
		}
	}, []);

	const stopListening = useCallback(() => {
		isListeningRef.current = false;
		setIsListening(false);
		setStatus(
			resultRef.current?.bpm && resultRef.current.bpm > 0 ? "detected" : "idle",
		);
		EngineModule.stopRecording();
	}, []);

	// biome-ignore lint/correctness/useExhaustiveDependencies: SharedValues are stable refs
	const reset = useCallback(() => {
		isListeningRef.current = false;
		EngineModule.stopRecording();
		EngineModule.reset();
		latestBpmRef.current = 0;
		lastResultUpdateRef.current = 0;
		lastBpmPollRef.current = 0;
		pendingResultRef.current = null;
		setStatus("idle");
		setResult(null);
		setIsListening(false);
		setError(null);
		beatActivation.value = 0;
		downbeatActivation.value = 0;
		waveformSamples.value = [];
		waveformBands.value = { low: 0.33, mid: 0.33, high: 0.34 };
		audioLevel.value = 0;
	}, []);

	return {
		status,
		isListening,
		result,
		beatActivation,
		downbeatActivation,
		waveformSamples,
		waveformBands,
		audioLevel,
		error,
		startListening,
		stopListening,
		reset,
	};
}
