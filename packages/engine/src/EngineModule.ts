import { NativeModule, requireNativeModule } from "expo";
import type {
	EngineConstants,
	EngineModuleEvents,
	FrameResult,
	KeyResult,
	State,
	WaveformData,
} from "./Engine.types";

interface EventSubscription {
	remove(): void;
}

type EngineEventName = "onState" | "onWaveform" | "onKey";

type EngineEventPayload<T extends EngineEventName> = T extends "onState"
	? State
	: T extends "onWaveform"
		? WaveformData
		: T extends "onKey"
			? KeyResult
			: never;

declare class EngineNativeModule
	extends NativeModule<EngineModuleEvents>
	implements EngineConstants
{
	// Constants
	/** Native sample rate (44100 Hz) */
	SAMPLE_RATE: number;
	/** BPM pipeline sample rate (22050 Hz) */
	BPM_SAMPLE_RATE: number;
	/** Key detection sample rate (44100 Hz) */
	KEY_SAMPLE_RATE: number;
	/** BPM frames per second (50) */
	BPM_FPS: number;
	/** Key detection frames per second (5) */
	KEY_FPS: number;

	/** Reset processing state for both BPM and key detection */
	reset(): void;

	// =========================================================================
	// BPM Detection (BeatNet)
	// =========================================================================

	/**
	 * Load bundled BeatNet ONNX model for BPM detection
	 * @returns true if loaded successfully
	 */
	loadModel(): boolean;

	/**
	 * Check if BeatNet model is loaded and ready
	 */
	isReady(): boolean;

	// =========================================================================
	// Key Detection (MusicalKeyCNN)
	// =========================================================================

	/**
	 * Load bundled MusicalKeyCNN ONNX model for key detection
	 * @returns true if loaded successfully
	 */
	loadKeyModel(): boolean;

	/**
	 * Check if MusicalKeyCNN model is loaded and ready
	 */
	isKeyReady(): boolean;

	/**
	 * Get current key detection result
	 * @returns Key result with Camelot/notation and confidence, or null if not detected yet
	 */
	getKey(): KeyResult | null;

	/**
	 * Get number of CQT frames processed for key detection
	 * Key becomes reliable after ~100 frames (~20 seconds)
	 */
	getKeyFrameCount(): number;

	// =========================================================================
	// Native Audio Capture (recommended - no JS bridge overhead)
	// =========================================================================

	/**
	 * Request microphone permission
	 * @returns true if permission was granted
	 */
	requestPermission(): Promise<boolean>;

	/**
	 * Get current microphone permission status
	 * @returns "granted" | "denied" | "undetermined"
	 */
	getPermissionStatus(): "granted" | "denied" | "undetermined";

	/**
	 * Start native audio recording and processing
	 * Audio is captured at 44100Hz and processed through both BPM and key pipelines.
	 * Events are emitted for state updates, waveform data, and key detection.
	 * Requires microphone permission - call requestPermission() first.
	 * @param enableWaveform Whether to emit waveform data for visualization
	 * @returns true if recording started successfully
	 */
	startRecording(enableWaveform?: boolean): Promise<boolean>;

	/**
	 * Stop native audio recording
	 */
	stopRecording(): void;

	/**
	 * Check if currently recording
	 */
	isRecording(): boolean;

	// =========================================================================
	// Manual Audio Processing (use when you need custom audio handling)
	// =========================================================================

	/**
	 * Process audio through both BPM and key detection pipelines
	 * Requires loadModel() and optionally loadKeyModel() to be called first.
	 * @param samples Audio samples at 44100Hz (Float32Array or number[])
	 * @returns Array of BPM results with activations, or null if no results
	 */
	processAudio(samples: Float32Array | number[]): FrameResult[] | null;

	// =========================================================================
	// State Queries
	// =========================================================================

	/**
	 * Get BPM estimate using autocorrelation
	 * Requires sufficient audio data to be processed first (~2 seconds).
	 * @returns BPM estimate rounded to integer, or 0 if insufficient data
	 */
	getBpm(): number;

	/**
	 * Get number of frames processed
	 * Useful to know when BPM is reliable (~100 frames = 2 seconds)
	 */
	getFrameCount(): number;
}

let nativeModule: EngineNativeModule | null = null;

function getEngineModule(): EngineNativeModule {
	if (!nativeModule) {
		nativeModule = requireNativeModule<EngineNativeModule>("Engine");
	}
	return nativeModule;
}

const EngineModule = new Proxy({} as EngineNativeModule, {
	get(_target, propertyKey) {
		const module = getEngineModule();
		const value = module[propertyKey as keyof EngineNativeModule];
		if (typeof value === "function") {
			return value.bind(module);
		}
		return value;
	},
});

export function addEngineListener<T extends EngineEventName>(
	eventName: T,
	listener: (event: EngineEventPayload<T>) => void,
): EventSubscription {
	return getEngineModule().addListener(
		eventName,
		listener as EngineModuleEvents[T],
	);
}

export default EngineModule;
