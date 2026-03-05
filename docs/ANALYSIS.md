# Audio Analysis Architecture

## Overview

Keyed now uses a **native C++ ONNX pipeline** in `packages/engine` for both BPM and key detection.

- Audio capture and processing run natively (iOS/Android), not in JS/WASM
- Input stream is 44.1kHz mono PCM
- BPM and key are computed in parallel from the same live stream

This architecture replaced the previous Essentia/WASM approach (see merged PR #1 and #3 for migration context).

## Runtime Components

- Native module: `@keyed/engine` (Expo module wrapper over C++ core)
- BPM model: `packages/engine/models/beatnet.onnx`
- Key model: `packages/engine/models/keynet.onnx`
- Execution: ONNX Runtime with platform acceleration (CoreML/NNAPI where available)

## Analysis Pipelines

### BPM Analysis (BeatNet + Autocorrelation)

1. Native audio (44.1kHz) is resampled to 22.05kHz for BPM.
2. Streaming mel features are extracted (272-dim input).
3. `beatnet.onnx` outputs beat/downbeat activations.
4. Autocorrelation over activations produces a stable BPM estimate.

Operational notes:
- Frame rate: `BPM_FPS = 50`
- BPM becomes reliable after roughly `getFrameCount() >= 100` (~2 seconds)
- Current value is read via `getBpm()`

Deep dive: `docs/BEATNET.md`

### Key Analysis (MusicalKeyCNN + CQT)

1. The same native audio stream stays at 44.1kHz for key detection.
2. Streaming CQT features are extracted at `KEY_FPS = 5`.
3. `keynet.onnx` runs 24-class classification (12 minor + 12 major).
4. Engine outputs:
   - `camelot` (for DJ mixing, e.g. `8A`)
   - `notation` (e.g. `Am`)
   - `confidence` (softmax probability, 0-1)

Operational notes:
- Minimum frames before first key inference: `KEY_MIN_FRAMES = 100` (~20 seconds)
- After first result, inference runs every `KEY_INFERENCE_INTERVAL = 25` frames (~5 seconds)
- Inference uses a rolling 4-minute CQT window (1200 frames at 5 FPS) to keep memory bounded.
- Native layer emits `onKey` when notation changes or confidence changes meaningfully

Deep dive: `docs/KEY_DETECTION.md`

## Module Lifecycle

Typical startup sequence:

1. `loadModel()` for BPM
2. `loadKeyModel()` for key
3. `startRecording(true)` for native capture + processing
4. Subscribe to:
   - `onState` for beat/downbeat frame updates
   - `onKey` for key updates
   - `onWaveform` for visualization

Bridge notes:
- Native event delivery is rate-limited (`onState` ~20Hz, `onWaveform` ~12Hz) for power efficiency.
- Detector internals continue running at full native rates (`BPM_FPS = 50`, `KEY_FPS = 5`), so convergence behavior is unchanged.

## TypeScript Usage

```ts
import EngineModule from "@keyed/engine";

const bpmReady = EngineModule.loadModel();
const keyReady = EngineModule.loadKeyModel();

if (bpmReady && keyReady) {
	EngineModule.addListener("onState", (state) => {
		const bpm = EngineModule.getBpm();
		const frames = EngineModule.getFrameCount();
	});

	EngineModule.addListener("onKey", (key) => {
		// key.camelot, key.notation, key.confidence
	});

	await EngineModule.startRecording(true);
}
```

## Confidence and Readiness

- BPM confidence is implicit in data volume and activation stability; gate UI using `getFrameCount()`.
- Key confidence is explicit (`confidence` in `KeyResult`) but should still be gated by `getKeyFrameCount()` during early accumulation.
- Call `reset()` to clear both pipelines and restart convergence windows.
