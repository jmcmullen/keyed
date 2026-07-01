import type { KeyResult, State, WaveformData } from "@keyed/engine";
import EngineModule from "@keyed/engine";
import { useFocusEffect } from "@react-navigation/native";
import { useCallback, useEffect, useRef, useState } from "react";
import { resolvePermission } from "@/lib/engine-permission";
import { log } from "@/lib/log";

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
const PERF_LOG_INTERVAL = 5_000;
const EXPECTED_BPM = 128;
const EXPECTED_CAMELOT = "10A";

type Session = {
	id: string;
	startedAt: number;
	lastLogAt: number;
	lastFrameCount: number;
	lastKeyFrameCount: number;
	lastStateCount: number;
	lastWaveformCount: number;
	lastKeyCount: number;
};

function round(value: number): number {
	return Math.round(value * 1000) / 1000;
}

export function useEngine(): UseEngineReturn {
	const [status, setStatus] = useState<DetectionStatus>("initializing");
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
	const keyRef = useRef(key);
	const isListeningRef = useRef(false);
	const busyRef = useRef(false);
	const stateCountRef = useRef(0);
	const waveformCountRef = useRef(0);
	const keyCountRef = useRef(0);
	const levelRef = useRef(0);
	const stateTimestampRef = useRef(0);
	const sessionRef = useRef<Session | null>(null);
	const perfTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);

	useEffect(() => {
		resultRef.current = result;
	}, [result]);

	useEffect(() => {
		keyRef.current = key;
	}, [key]);

	const lock = () => {
		busyRef.current = true;
		setIsBusy(true);
	};

	const unlock = () => {
		busyRef.current = false;
		setIsBusy(false);
	};

	const clear = () => {
		latestBpmRef.current = 0;
		latestBpmConfidenceRef.current = 0;
		lastResultUpdateRef.current = 0;
		lastBpmPollRef.current = 0;
		lastKeyUpdateRef.current = 0;
		stateCountRef.current = 0;
		waveformCountRef.current = 0;
		keyCountRef.current = 0;
		levelRef.current = 0;
		stateTimestampRef.current = 0;
		setResult(null);
		setKey(null);
	};

	const logPerf = useCallback((phase: string) => {
		const session = sessionRef.current;
		if (!session) return;

		const now = Date.now();
		const elapsedSec = Math.max((now - session.startedAt) / 1000, 0.001);
		const windowSec = Math.max((now - session.lastLogAt) / 1000, 0.001);
		const frameCount = EngineModule.getFrameCount();
		const keyFrameCount = EngineModule.getKeyFrameCount();
		const bpm = EngineModule.getBpm();
		const bpmConfidence = EngineModule.getBpmConfidence();
		const stateDelta = stateCountRef.current - session.lastStateCount;
		const waveformDelta = waveformCountRef.current - session.lastWaveformCount;
		const keyDelta = keyCountRef.current - session.lastKeyCount;
		const frameDelta = frameCount - session.lastFrameCount;
		const keyFrameDelta = keyFrameCount - session.lastKeyFrameCount;
		const currentKey = EngineModule.getKey() ?? keyRef.current;
		const bpmError = bpm > 0 ? round(bpm - EXPECTED_BPM) : null;

		log.info({
			action: "engine.recording.metrics",
			surface: "use-engine",
			phase,
			sessionId: session.id,
			elapsedSec: round(elapsedSec),
			windowSec: round(windowSec),
			expectedBpm: EXPECTED_BPM,
			expectedCamelot: EXPECTED_CAMELOT,
			bpm: bpm > 0 ? round(bpm) : 0,
			bpmConfidence: round(bpmConfidence),
			bpmError,
			bpmAbsError: bpmError === null ? null : Math.abs(bpmError),
			camelot: currentKey?.camelot ?? null,
			notation: currentKey?.notation ?? null,
			keyConfidence: currentKey ? round(currentKey.confidence) : null,
			keyMatchesExpected: currentKey
				? currentKey.camelot === EXPECTED_CAMELOT
				: null,
			frameCount,
			frameDelta,
			frameRate: round(frameDelta / windowSec),
			expectedFrameRate: EngineModule.BPM_FPS,
			keyFrameCount,
			keyFrameDelta,
			keyFrameRate: round(keyFrameDelta / windowSec),
			expectedKeyFrameRate: EngineModule.KEY_FPS,
			stateEvents: stateCountRef.current,
			stateEventDelta: stateDelta,
			stateEventRate: round(stateDelta / windowSec),
			waveformEvents: waveformCountRef.current,
			waveformEventDelta: waveformDelta,
			waveformEventRate: round(waveformDelta / windowSec),
			keyEvents: keyCountRef.current,
			keyEventDelta: keyDelta,
			keyEventRate: round(keyDelta / windowSec),
			rms: round(levelRef.current),
			stateClockLagSec:
				stateTimestampRef.current > 0
					? round(elapsedSec - stateTimestampRef.current)
					: null,
			recording: EngineModule.isRecording(),
			native: EngineModule.getDebugState?.() ?? null,
		});

		session.lastLogAt = now;
		session.lastFrameCount = frameCount;
		session.lastKeyFrameCount = keyFrameCount;
		session.lastStateCount = stateCountRef.current;
		session.lastWaveformCount = waveformCountRef.current;
		session.lastKeyCount = keyCountRef.current;
	}, []);

	const startPerf = useCallback(() => {
		const now = Date.now();
		if (perfTimerRef.current) {
			clearInterval(perfTimerRef.current);
		}
		sessionRef.current = {
			id: String(now),
			startedAt: now,
			lastLogAt: now,
			lastFrameCount: 0,
			lastKeyFrameCount: 0,
			lastStateCount: 0,
			lastWaveformCount: 0,
			lastKeyCount: 0,
		};
		perfTimerRef.current = setInterval(() => {
			logPerf("sample");
		}, PERF_LOG_INTERVAL);
	}, [logPerf]);

	const stopPerf = useCallback(
		(phase: string) => {
			if (perfTimerRef.current) {
				clearInterval(perfTimerRef.current);
				perfTimerRef.current = null;
			}
			logPerf(phase);
			sessionRef.current = null;
		},
		[logPerf],
	);

	useEffect(() => {
		let live = true;

		log.info({
			action: "engine.model_load.start",
			surface: "use-engine",
		});
		void Promise.all([EngineModule.loadModel(), EngineModule.loadKeyModel()])
			.then(([loaded, loadedKey]) => {
				if (!live) return;
				log.info({
					action: "engine.model_load.finish",
					surface: "use-engine",
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
				log.error(
					{
						action: "engine.model_load.error",
						surface: "use-engine",
						message,
					},
					err,
				);
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
				stateTimestampRef.current = event.timestamp;

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
			stopPerf("unmount");
			EngineModule.stopRecording();
			EngineModule.reset();
		};
	}, [stopPerf]);

	const startListening = async () => {
		if (busyRef.current || isListeningRef.current) {
			return false;
		}
		lock();
		return await (async () => {
			log.info({
				action: "engine.recording.start_requested",
				surface: "use-engine",
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
			log.info({
				action: "engine.permission.checked",
				surface: "use-engine",
				permission,
			});
			const permissionResult = await resolvePermission(
				permission,
				() => EngineModule.requestPermission() as Promise<unknown>,
			);
			if (!permissionResult.granted) {
				const message = permissionResult.err || "Microphone permission denied";
				if (permissionResult.err) {
					log.error({
						action: "engine.permission.error",
						surface: "use-engine",
						message,
					});
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
			log.info({
				action: "engine.recording.start_result",
				surface: "use-engine",
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
					log.error({
						action: "engine.recording.start_error",
						surface: "use-engine",
						message,
					});
				}
				setError(message);
				setStatus("error");
				return false;
			}

			setIsListening(true);
			setStatus("listening");
			startPerf();
			return true;
		})().finally(unlock);
	};

	const stopListening = () => {
		if (busyRef.current || !isListeningRef.current) {
			return false;
		}
		lock();
		stopPerf("stop");
		isListeningRef.current = false;
		setIsListening(false);
		setStatus(
			resultRef.current?.bpm && resultRef.current.bpm > 0 ? "detected" : "idle",
		);
		EngineModule.stopRecording();
		unlock();
		return true;
	};

	useFocusEffect(() => {
		return () => {
			if (!isListeningRef.current) return;
			stopPerf("blur");
			isListeningRef.current = false;
			EngineModule.stopRecording();
			setIsListening(false);
			setStatus(
				resultRef.current?.bpm && resultRef.current.bpm > 0
					? "detected"
					: "idle",
			);
		};
	});

	const reset = () => {
		if (busyRef.current) {
			return;
		}
		stopPerf("reset");
		isListeningRef.current = false;
		EngineModule.stopRecording();
		EngineModule.reset();
		clear();
		setStatus("idle");
		setIsListening(false);
		setError(null);
	};

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
