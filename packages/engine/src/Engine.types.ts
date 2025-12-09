/**
 * Engine module types
 */

export interface FrameResult {
	/** Beat activation from ONNX model (0-1) */
	beatActivation: number;
	/** Downbeat activation from ONNX model (0-1) */
	downbeatActivation: number;
}

export interface ProcessResult {
	/** Detected beat type, or null if no beat */
	beat: "beat" | "downbeat" | null;
	/** Current BPM estimate */
	bpm: number;
	/** Current phase in the beat cycle (0-1) */
	phase: number;
	/** Detected meter (beats per bar) */
	meter: number;
	/** Confidence in the current estimate (0-1) */
	confidence: number;
}

export interface EngineConstants {
	/** Sample rate in Hz */
	SAMPLE_RATE: number;
	/** Frames per second */
	FPS: number;
}

export interface State {
	/** Beat activation from ONNX model (0-1) */
	beatActivation: number;
	/** Downbeat activation from ONNX model (0-1) */
	downbeatActivation: number;
	/** Timestamp in seconds since recording started (for PLL phase sync) */
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

export interface EngineModuleEvents {
	/** Fired every frame with current state (50 FPS) */
	onState: (event: State) => void;
	/** Fired with waveform data for visualization (throttled) */
	onWaveform: (event: WaveformData) => void;
}
