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
	bpmConfidence: number;
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

export interface UseEngineReturn {
	status: DetectionStatus;
	isListening: boolean;
	isBusy: boolean;
	result: BeatNetResult | null;
	key: KeyState | null;
	error: string | null;
	startListening: () => Promise<boolean>;
	stopListening: () => boolean;
	reset: () => void;
}

const RESULT_UPDATE_INTERVAL = 100;
const BPM_POLL_INTERVAL = 500;
const KEY_EVENT_INTERVAL = 100;
const HEALTH_DELAY = 2_500;

function log(msg: string, data?: Record<string, unknown>) {
	if (data) {
		console.info(`[useEngine] ${msg}`, data);
		return;
	}
	console.info(`[useEngine] ${msg}`);
}

export function useEngine(): UseEngineReturn {
	const [status, setStatus] = useState<DetectionStatus>("idle");
	const [result, setResult] = useState<BeatNetResult | null>(null);
	const [key, setKey] = useState<KeyState | null>(null);
	const [isBusy, setIsBusy] = useState(false);
	const [error, setError] = useState<string | null>(null);
	const [isListening, setIsListening] = useState(false);

	const latestBpmRef = useRef<number>(0);
	const latestBpmConfidenceRef = useRef<number>(0);
	const lastResultUpdateRef = useRef<number>(0);
	const lastBpmPollRef = useRef<number>(0);
	const lastKeyUpdateRef = useRef<number>(0);
	const resultRef = useRef(result);
	const isListeningRef = useRef(false);
	const busyRef = useRef(false);
	const stateCountRef = useRef(0);
	const waveformCountRef = useRef(0);
	const keyCountRef = useRef(0);
	const levelRef = useRef(0);

	resultRef.current = result;

	const lock = useCallback(() => {
		busyRef.current = true;
		setIsBusy(true);
	}, []);

	const unlock = useCallback(() => {
		busyRef.current = false;
		setIsBusy(false);
	}, []);

	const clear = useCallback(() => {
		latestBpmRef.current = 0;
		latestBpmConfidenceRef.current = 0;
		lastResultUpdateRef.current = 0;
		lastBpmPollRef.current = 0;
		lastKeyUpdateRef.current = 0;
		stateCountRef.current = 0;
		waveformCountRef.current = 0;
		keyCountRef.current = 0;
		levelRef.current = 0;
		setResult(null);
		setKey(null);
	}, []);

	useEffect(() => {
		setStatus("initializing");
		let live = true;

		log("loading models");
		void Promise.all([EngineModule.loadModel(), EngineModule.loadKeyModel()])
			.then(([loaded, loadedKey]) => {
				if (!live) return;
				log("models loaded", {
					loaded,
					loadedKey,
					ready: EngineModule.isReady(),
					keyReady: EngineModule.isKeyReady(),
				});
				if (!loaded || !loadedKey) {
					setError("Failed to load native detection models");
					setStatus("error");
					return;
				}
				setStatus("idle");
			})
			.catch((err: unknown) => {
				if (!live) return;
				const message =
					err instanceof Error
						? err.message
						: "Failed to load native detection models";
				log("model load failed", { message });
				setError(message);
				setStatus("error");
			});

		return () => {
			live = false;
		};
	}, []);

	useEffect(() => {
		const stateSubscription = EngineModule.addListener(
			"onState",
			(event: State) => {
				if (!isListeningRef.current) return;

				stateCountRef.current += 1;

				const now = Date.now();
				const shouldPollBpm = now - lastBpmPollRef.current >= BPM_POLL_INTERVAL;

				let bpm = latestBpmRef.current;
				let bpmConfidence = latestBpmConfidenceRef.current;
				let frameCount = resultRef.current?.frameCount ?? 0;

				if (shouldPollBpm) {
					lastBpmPollRef.current = now;
					frameCount = EngineModule.getFrameCount();

					if (frameCount >= 100) {
						bpm = EngineModule.getBpm();
						bpmConfidence = EngineModule.getBpmConfidence();
						if (bpm !== latestBpmRef.current && bpm > 0) {
							latestBpmRef.current = bpm;
						}
						latestBpmConfidenceRef.current = bpm > 0 ? bpmConfidence : 0;
					}
				}

				const next: BeatNetResult = {
					bpm: bpm > 0 ? bpm : 0,
					bpmConfidence: bpm > 0 ? bpmConfidence : 0,
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

				waveformCountRef.current += 1;
				levelRef.current = event.rms;
			},
		);

		const keySubscription = EngineModule.addListener(
			"onKey",
			(event: KeyResult) => {
				if (!isListeningRef.current) return;

				keyCountRef.current += 1;
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
			log("start requested", {
				ready: EngineModule.isReady(),
				keyReady: EngineModule.isKeyReady(),
				recording: EngineModule.isRecording(),
				native: EngineModule.getDebugState?.() ?? null,
			});
			if (!EngineModule.isReady() || !EngineModule.isKeyReady()) {
				setError("Models not initialized");
				setStatus("error");
				return false;
			}

			setError(null);

			const permission = EngineModule.getPermissionStatus();
			log("permission checked", { permission });
			const permissionResult = await resolvePermission(
				permission,
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

			clear();
			isListeningRef.current = true;

			const startResult = await EngineModule.startRecording(true)
				.then((started) => ({ started, err: "" }))
				.catch((err: unknown) => ({
					started: false,
					err:
						err instanceof Error
							? err.message
							: "Failed to start native audio recording",
				}));
			log("start result", {
				started: startResult.started,
				err: startResult.err,
				recording: EngineModule.isRecording(),
				native: EngineModule.getDebugState?.() ?? null,
			});
			if (!startResult.started) {
				isListeningRef.current = false;
				const message =
					startResult.err || "Failed to start native audio recording";
				if (startResult.err) {
					console.error("[useEngine] Start error:", message);
				}
				setError(message);
				setStatus("error");
				return false;
			}

			setIsListening(true);
			setStatus("listening");
			setTimeout(() => {
				if (!isListeningRef.current) return;
				log("health", {
					recording: EngineModule.isRecording(),
					frameCount: EngineModule.getFrameCount(),
					bpm: EngineModule.getBpm(),
					bpmConfidence: EngineModule.getBpmConfidence(),
					stateEvents: stateCountRef.current,
					waveformEvents: waveformCountRef.current,
					keyEvents: keyCountRef.current,
					rms: levelRef.current,
					native: EngineModule.getDebugState?.() ?? null,
				});
			}, HEALTH_DELAY);
			return true;
		} finally {
			unlock();
		}
	}, [clear, lock, unlock]);

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

	useFocusEffect(
		useCallback(() => {
			return () => {
				if (!isListeningRef.current) return;
				isListeningRef.current = false;
				EngineModule.stopRecording();
				setIsListening(false);
				setStatus(
					resultRef.current?.bpm && resultRef.current.bpm > 0
						? "detected"
						: "idle",
				);
			};
		}, []),
	);

	const reset = useCallback(() => {
		if (busyRef.current) {
			return;
		}
		isListeningRef.current = false;
		EngineModule.stopRecording();
		EngineModule.reset();
		clear();
		setStatus("idle");
		setIsListening(false);
		setError(null);
	}, [clear]);

	return {
		status,
		isListening,
		isBusy,
		result,
		key,
		error,
		startListening,
		stopListening,
		reset,
	};
}
