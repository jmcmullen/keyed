import { requireNativeModule } from "expo";
import type {
	EngineConstants,
	FrameResult,
	State,
	WaveformData,
} from "./Engine.types";

interface EventSubscription {
	remove(): void;
}

type EngineEventName = "onState" | "onWaveform";

type EngineEventPayload<T extends EngineEventName> = T extends "onState"
	? State
	: T extends "onWaveform"
		? WaveformData
		: never;

declare class EngineModule implements EngineConstants {
	// Constants
	SAMPLE_RATE: number;
	FPS: number;

	/** Reset processing state */
	reset(): void;

	/**
	 * Load bundled ONNX model for full native processing
	 * The model is bundled with the native module and loaded from there.
	 * @returns true if loaded successfully
	 */
	loadModel(): boolean;

	/**
	 * Check if ONNX model is loaded and ready
	 */
	isReady(): boolean;

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
	 * Audio is captured at 22050Hz and processed through the full pipeline.
	 * Events are emitted for state updates and waveform data.
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
	 * Process audio through entire pipeline natively (mel + ONNX)
	 * Requires loadModel() to be called first.
	 * @param samples Audio samples at 22050Hz (Float32Array or number[])
	 * @returns Array of results with activations, or null if no results
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

	// =========================================================================
	// Event Listeners
	// =========================================================================

	/**
	 * Add a listener for native events
	 * @param eventName The event to listen for
	 * @param listener The callback function
	 * @returns Subscription that can be removed
	 */
	addListener<T extends EngineEventName>(
		eventName: T,
		listener: (event: EngineEventPayload<T>) => void,
	): EventSubscription;
}

export default requireNativeModule<EngineModule>("Engine");
