import { NativeModule, registerWebModule } from "expo";
import type {
	EngineModuleEvents,
	FrameResult,
	KeyResult,
} from "./Engine.types";

class EngineModuleWeb extends NativeModule<EngineModuleEvents> {
	SAMPLE_RATE = 44100;
	BPM_SAMPLE_RATE = 22050;
	KEY_SAMPLE_RATE = 44100;
	BPM_FPS = 50;
	KEY_FPS = 5;

	loadModel(): boolean {
		return false;
	}

	isReady(): boolean {
		return false;
	}

	loadKeyModel(): boolean {
		return false;
	}

	isKeyReady(): boolean {
		return false;
	}

	getKey(): KeyResult | null {
		return null;
	}

	getKeyFrameCount(): number {
		return 0;
	}

	async requestPermission(): Promise<boolean> {
		return false;
	}

	getPermissionStatus(): "granted" | "denied" | "undetermined" {
		return "denied";
	}

	async startRecording(_enableWaveform = true): Promise<boolean> {
		return false;
	}

	stopRecording(): void {}

	isRecording(): boolean {
		return false;
	}

	processAudio(_samples: Float32Array | number[]): FrameResult[] | null {
		return null;
	}

	getBpm(): number {
		return 0;
	}

	getFrameCount(): number {
		return 0;
	}

	reset(): void {}
}

export default registerWebModule(EngineModuleWeb, "Engine");
