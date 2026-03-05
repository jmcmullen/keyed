import type { KeyResult, State, WaveformData } from "@keyed/engine";
import EngineModule from "@keyed/engine";
import { useFocusEffect } from "@react-navigation/native";
import { useCallback, useEffect, useRef, useState } from "react";
import { resolvePermission } from "@/lib/engine-permission";

export type DetectionStatus =
	| "idle"
	| "initializing"
	| "listening"
	| "detected"
	| "error";

export interface BeatNetResult {
	bpm: number;
	frameCount: number;
	beatActivation: number;
	downbeatActivation: number;
}

export interface KeyState {
	camelot: string;
	notation: string;
	confidence: number;
	timestamp: number;
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
	isBusy: boolean;
	keyReady: boolean;
	result: BeatNetResult | null;
	key: KeyState | null;
	beatActivation: number;
	downbeatActivation: number;
	waveformSamples: number[];
	waveformBands: FrequencyBands;
	audioLevel: number;
	error: string | null;
	startListening: () => Promise<boolean>;
	stopListening: () => boolean;
	reset: () => void;
}

const RESULT_UPDATE_INTERVAL = 100;
const BPM_POLL_INTERVAL = 500;
const KEY_EVENT_INTERVAL = 100;

export function useEngine(options: UseEngineOptions = {}): UseEngineReturn {
	const { onWaveform } = options;

	const [status, setStatus] = useState<DetectionStatus>("idle");
	const [result, setResult] = useState<BeatNetResult | null>(null);
	const [key, setKey] = useState<KeyState | null>(null);
	const [keyReady, setKeyReady] = useState(false);
	const [isBusy, setIsBusy] = useState(false);
	const [error, setError] = useState<string | null>(null);
	const [isListening, setIsListening] = useState(false);
	const [beatActivation, setBeatActivation] = useState(0);
	const [downbeatActivation, setDownbeatActivation] = useState(0);
	const [waveformSamples, setWaveformSamples] = useState<number[]>([]);
	const [waveformBands, setWaveformBands] = useState<FrequencyBands>({
		low: 0.33,
		mid: 0.33,
		high: 0.34,
	});
	const [audioLevel, setAudioLevel] = useState(0);

	const latestBpmRef = useRef<number>(0);
	const lastResultUpdateRef = useRef<number>(0);
	const lastBpmPollRef = useRef<number>(0);
	const lastKeyUpdateRef = useRef<number>(0);
	const onWaveformRef = useRef(onWaveform);
	const resultRef = useRef(result);
	const isListeningRef = useRef(false);
	const busyRef = useRef(false);

	onWaveformRef.current = onWaveform;
	resultRef.current = result;

	const lock = useCallback(() => {
		busyRef.current = true;
		setIsBusy(true);
	}, []);

	const unlock = useCallback(() => {
		busyRef.current = false;
		setIsBusy(false);
	}, []);

	useEffect(() => {
		setStatus("initializing");

		const loaded = EngineModule.loadModel();
		const loadedKey = EngineModule.loadKeyModel();
		if (!loaded || !loadedKey) {
			setError("Failed to load native detection models");
			setStatus("error");
			return;
		}

		setKeyReady(EngineModule.isKeyReady());
		setStatus("idle");
	}, []);

	useEffect(() => {
		const stateSubscription = EngineModule.addListener(
			"onState",
			(event: State) => {
				if (!isListeningRef.current) return;

				setBeatActivation(event.beatActivation);
				setDownbeatActivation(event.downbeatActivation);

				const now = Date.now();
				const shouldPollBpm = now - lastBpmPollRef.current >= BPM_POLL_INTERVAL;

				let bpm = latestBpmRef.current;
				let frameCount = resultRef.current?.frameCount ?? 0;

				if (shouldPollBpm) {
					lastBpmPollRef.current = now;
					frameCount = EngineModule.getFrameCount();

					if (frameCount >= 100) {
						bpm = EngineModule.getBpm();
						if (bpm !== latestBpmRef.current && bpm > 0) {
							latestBpmRef.current = bpm;
						}
					}
				}

				const next: BeatNetResult = {
					bpm: bpm > 0 ? bpm : 0,
					frameCount,
					beatActivation: event.beatActivation,
					downbeatActivation: event.downbeatActivation,
				};

				if (now - lastResultUpdateRef.current < RESULT_UPDATE_INTERVAL) return;
				lastResultUpdateRef.current = now;
				setResult(next);
				if (bpm > 0) {
					setStatus("detected");
				}
			},
		);

		const waveformSubscription = EngineModule.addListener(
			"onWaveform",
			(event: WaveformData) => {
				if (!isListeningRef.current) return;

				setWaveformSamples(event.samples);
				setWaveformBands({
					low: event.low,
					mid: event.mid,
					high: event.high,
				});
				setAudioLevel(event.rms);
				onWaveformRef.current?.(event);
			},
		);

		const keySubscription = EngineModule.addListener(
			"onKey",
			(event: KeyResult) => {
				if (!isListeningRef.current) return;

				const now = Date.now();
				if (now - lastKeyUpdateRef.current < KEY_EVENT_INTERVAL) return;
				lastKeyUpdateRef.current = now;

				const next: KeyState = {
					camelot: event.camelot,
					notation: event.notation,
					confidence: event.confidence,
					timestamp:
						typeof event.timestamp === "number" ? event.timestamp : now / 1000,
				};
				setKey(next);
				if (resultRef.current?.bpm && resultRef.current.bpm > 0) {
					setStatus("detected");
				}
			},
		);

		return () => {
			stateSubscription.remove();
			waveformSubscription.remove();
			keySubscription.remove();
		};
	}, []);

	useFocusEffect(
		useCallback(() => {
			return () => {
				if (!isListeningRef.current) {
					return;
				}
				isListeningRef.current = false;
				setIsListening(false);
				setStatus(
					resultRef.current?.bpm && resultRef.current.bpm > 0
						? "detected"
						: "idle",
				);
				EngineModule.stopRecording();
			};
		}, []),
	);

	useEffect(() => {
		return () => {
			isListeningRef.current = false;
			EngineModule.stopRecording();
			EngineModule.reset();
		};
	}, []);

	const startListening = useCallback(async () => {
		if (busyRef.current || isListeningRef.current) {
			return false;
		}
		lock();
		try {
			if (!EngineModule.isReady() || !EngineModule.isKeyReady()) {
				setError("Models not initialized");
				setStatus("error");
				return false;
			}

			setError(null);

			const permissionResult = await resolvePermission(
				EngineModule.getPermissionStatus(),
				() => EngineModule.requestPermission() as Promise<unknown>,
			);
			if (!permissionResult.granted) {
				const message = permissionResult.err || "Microphone permission denied";
				if (permissionResult.err) {
					console.error("[useEngine] Permission error:", message);
				}
				setError(message);
				setStatus("error");
				return false;
			}

			setResult(null);
			setKey(null);
			latestBpmRef.current = 0;
			lastResultUpdateRef.current = 0;
			lastBpmPollRef.current = 0;
			lastKeyUpdateRef.current = 0;
			setBeatActivation(0);
			setDownbeatActivation(0);
			setWaveformSamples([]);
			setWaveformBands({ low: 0.33, mid: 0.33, high: 0.34 });
			setAudioLevel(0);

			const startResult = await EngineModule.startRecording(true)
				.then((started) => ({ started, err: "" }))
				.catch((err: unknown) => ({
					started: false,
					err:
						err instanceof Error
							? err.message
							: "Failed to start native audio recording",
				}));
			if (!startResult.started) {
				const message =
					startResult.err || "Failed to start native audio recording";
				if (startResult.err) {
					console.error("[useEngine] Start error:", message);
				}
				setError(message);
				setStatus("error");
				return false;
			}

			isListeningRef.current = true;
			setIsListening(true);
			setStatus("listening");
			return true;
		} finally {
			unlock();
		}
	}, [lock, unlock]);

	const stopListening = useCallback(() => {
		if (busyRef.current || !isListeningRef.current) {
			return false;
		}
		lock();
		try {
			isListeningRef.current = false;
			setIsListening(false);
			setStatus(
				resultRef.current?.bpm && resultRef.current.bpm > 0
					? "detected"
					: "idle",
			);
			EngineModule.stopRecording();
			return true;
		} finally {
			unlock();
		}
	}, [lock, unlock]);

	const reset = useCallback(() => {
		if (busyRef.current) {
			return;
		}
		isListeningRef.current = false;
		EngineModule.stopRecording();
		EngineModule.reset();
		latestBpmRef.current = 0;
		lastResultUpdateRef.current = 0;
		lastBpmPollRef.current = 0;
		lastKeyUpdateRef.current = 0;
		setStatus("idle");
		setResult(null);
		setKey(null);
		setIsListening(false);
		setError(null);
		setBeatActivation(0);
		setDownbeatActivation(0);
		setWaveformSamples([]);
		setWaveformBands({ low: 0.33, mid: 0.33, high: 0.34 });
		setAudioLevel(0);
	}, []);

	return {
		status,
		isListening,
		isBusy,
		keyReady,
		result,
		key,
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
