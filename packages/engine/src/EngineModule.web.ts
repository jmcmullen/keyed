import { NativeModule, registerWebModule } from "expo";
import type { EngineModuleEvents, ProcessResult } from "./Engine.types";

class EngineModuleWeb extends NativeModule<EngineModuleEvents> {
	SAMPLE_RATE = 44100;
	BPM_SAMPLE_RATE = 22050;
	KEY_SAMPLE_RATE = 44100;
	BPM_FPS = 50;
	KEY_FPS = 5;

	hello(): string {
		return "Engine web module (stub)";
	}

	getFFTSize(): number {
		return 1411;
	}

	async process(_samples: number[]): Promise<ProcessResult> {
		return {
			beat: null,
			bpm: 0,
			phase: 0,
			meter: 4,
			confidence: 0,
		};
	}

	reset(): void {}

	getCurrentBpm(): number {
		return 0;
	}

	getCurrentPhase(): number {
		return 0;
	}

	getCurrentMeter(): number {
		return 4;
	}

	getConfidence(): number {
		return 0;
	}
}

export default registerWebModule(EngineModuleWeb, "Engine");
