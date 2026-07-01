# @keyed/engine

The part that does the actual listening. This is the native engine behind Keyed: real-time BPM and key detection written in C++, with a thin Expo module on top so React Native can talk to it.

## What is in here

- `cpp/`: the shared DSP and inference core, where BPM and key detection actually happen
- `android/`: Expo module wrapper and JNI bridge
- `ios/`: Expo module wrapper and Objective-C++ bridge
- `src/`: the TypeScript interface React Native consumes
- `tests/`: C++ unit and integration tests (Catch2)
- `models/`: the ONNX model files bundled into native builds

## Docs

- [Architecture](../../docs/architecture.md)
- [BPM detection implementation](../../docs/bpm-detection.md)
- [Key detection implementation](../../docs/key-detection.md)

## Running it locally

From the repository root:

```bash
bun run test:native:build
bun run test:native:run
```

`packages/engine/tests/CMakeLists.txt` downloads ONNX Runtime against pinned SHA-256 checksums, so the build you get is the build everyone else gets.

## The public module API

The Expo module hands React Native three sorts of things:

- **Model lifecycle:** `loadModel()`, `loadKeyModel()`, `isReady()`, `isKeyReady()`, `reset()`
- **Recording lifecycle:** `requestPermission()`, `startRecording()`, `stopRecording()`
- **State queries:** `getBpm()` returns the stabilised decimal BPM with DJ-range half/double-time correction already applied, `getBpmConfidence()` returns a 0-1 tempo confidence, plus `getFrameCount()`, `getKey()`, and `getKeyFrameCount()`
- **Events:** `onState`, `onWaveform`, `onKey`

See `src/Engine.types.ts` for the event payload contracts.
