# @keyed/engine

Native real-time BPM and key detection engine used by the Keyed app.

## What it contains

- `cpp/`: shared DSP + inference core (BPM + key detection)
- `android/`: Expo module wrapper + JNI bridge
- `ios/`: Expo module wrapper + Objective-C++ bridge
- `src/`: TypeScript module interface consumed by React Native
- `tests/`: C++ unit/integration tests (Catch2)
- `models/`: ONNX model files bundled into native builds

## Local development

From repository root:

```bash
bun run test:native:build
bun run test:native:run
```

`packages/engine/tests/CMakeLists.txt` downloads ONNX Runtime with pinned SHA-256 checksums.

## Public module API

The Expo module exposes:

- model lifecycle: `loadModel()`, `loadKeyModel()`, `isReady()`, `isKeyReady()`, `reset()`
- recording lifecycle: `requestPermission()`, `startRecording()`, `stopRecording()`
- state queries: `getBpm()`, `getFrameCount()`, `getKey()`, `getKeyFrameCount()`
- events: `onState`, `onWaveform`, `onKey`

See `src/Engine.types.ts` for the event payload contracts.
