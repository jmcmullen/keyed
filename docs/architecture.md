# Audio Analysis Architecture

## The shape of it

Keyed does both BPM and key detection in a native C++ ONNX pipeline that lives in `packages/engine`. React Native receives throttled state events; the audio path stays native.

- Audio capture and processing run natively on iOS and Android
- The input stream is 44.1kHz mono PCM
- BPM and key are worked out in parallel from the same live stream

## The moving parts

- Native module: `@keyed/engine`, an Expo module wrapper over the C++ core
- BPM model: `packages/engine/models/beatnet.onnx`
- Key model: `packages/engine/models/keynet.onnx`
- Execution: ONNX Runtime with platform acceleration (CoreML or NNAPI where it is available)

## The two pipelines

### BPM (BeatNet + Autocorrelation)

1. Native audio at 44.1kHz is resampled to 22.05kHz for the BPM path.
2. Streaming mel features are extracted (272-dim input).
3. `beatnet.onnx` outputs beat and downbeat activations.
4. Autocorrelation over those activations produces a stable BPM.

Deep dive: [bpm-detection.md](bpm-detection.md)

### Key (MusicalKeyCNN + CQT)

1. The same native audio stays at 44.1kHz for key detection.
2. Streaming CQT features are extracted at `KEY_FPS = 5`.
3. `keynet.onnx` runs a 24-class classification (12 minor + 12 major).
4. The engine hands back:
   - `camelot` for DJ mixing, e.g. `8A`
   - `notation`, e.g. `Am`
   - `confidence`, a softmax probability from 0 to 1

Deep dive: [key-detection.md](key-detection.md)

## Bridge behaviour

- Native event delivery is rate-limited (`onState` at ~20Hz, `onWaveform` at ~12Hz) to spare the battery.
- The detectors themselves keep running at full native rates (`BPM_FPS = 50`, `KEY_FPS = 5`), so nothing about how they converge changes.

## Confidence and readiness

- BPM confidence is implied by how much data has arrived and how stable the activations are, so gate the UI on `getFrameCount()`.
- Key confidence is explicit (`confidence` in `KeyResult`), but still worth gating on `getKeyFrameCount()` during the early accumulation.
- Call `reset()` to clear both pipelines and start the convergence windows over.
