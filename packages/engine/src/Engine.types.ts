export interface FrameResult {
	/** Beat activation from ONNX model (0-1) */
	beatActivation: number;
	/** Downbeat activation from ONNX model (0-1) */
	downbeatActivation: number;
}

export interface KeyResult {
	/** Camelot notation: "1A" - "12B" */
	camelot: string;
	/** Musical notation: "Am", "C", etc. */
	notation: string;
	/** Confidence score (0-1) */
	confidence: number;
	/** Timestamp in seconds since recording started */
	timestamp?: number;
}

export interface ProcessResult {
	/** Detected beat type, or null if no beat */
	beat: "beat" | "downbeat" | null;
	/** Current BPM estimate with decimal precision */
	bpm: number;
	/** Current phase in the beat cycle (0-1) */
	phase: number;
	/** Detected meter (beats per bar) */
	meter: number;
	/** Confidence in the current estimate (0-1) */
	confidence: number;
}

export interface EngineConstants {
	/** Native sample rate in Hz (44100) */
	SAMPLE_RATE: number;
	/** BPM pipeline sample rate in Hz (22050) */
	BPM_SAMPLE_RATE: number;
	/** Key detection sample rate in Hz (44100) */
	KEY_SAMPLE_RATE: number;
	/** BPM frames per second (50) */
	BPM_FPS: number;
	/** Key detection frames per second (5) */
	KEY_FPS: number;
}

export interface State {
	/** Beat activation from ONNX model (0-1) */
	beatActivation: number;
	/** Downbeat activation from ONNX model (0-1) */
	downbeatActivation: number;
	/** Timestamp in seconds since recording started (for PLL phase sync) */
	timestamp: number;
}

export interface VisualState {
	/** Beat activation from ONNX model (0-1), emitted for low-latency visuals */
	beatActivation: number;
	/** Downbeat activation from ONNX model (0-1), emitted for low-latency visuals */
	downbeatActivation: number;
	/** Timestamp in seconds since recording started */
	timestamp: number;
}

export interface WaveformData {
	/** Audio samples for visualization (downsampled) */
	samples: number[];
	/** Peak amplitude in this buffer */
	peak: number;
	/** RMS amplitude in this buffer */
	rms: number;
	/** Low frequency energy (bass) 0-1 normalized */
	low: number;
	/** Mid frequency energy 0-1 normalized */
	mid: number;
	/** High frequency energy 0-1 normalized */
	high: number;
}

export type EngineDebugValue = boolean | number | string | null;

export interface EngineDebugState {
	[key: string]: EngineDebugValue;
}

export type EngineModuleEvents = {
	/** Fired with latest beat/downbeat state (bridge-throttled, ~20 Hz) */
	onState: (event: State) => void;
	/** Fired with lightweight beat/downbeat data for low-latency visuals */
	onVisual: (event: VisualState) => void;
	/** Fired with waveform data for visualization (bridge-throttled, ~12 Hz) */
	onWaveform: (event: WaveformData) => void;
	/** Fired when key detection updates after the initial key window fills */
	onKey: (event: KeyResult) => void;
};
