# Key Detection (MusicalKeyCNN)

## Overview
Key detection is implemented in `packages/engine` as a parallel pipeline to BPM detection.

- Input audio: 44.1kHz mono PCM
- Frontend: Constant-Q Transform (CQT)
- Model: `packages/engine/models/keynet.onnx` (24-class classifier)
- Output: Camelot + musical notation + confidence

## Runtime Flow
1. Native module loads `beatnet.onnx` and `keynet.onnx`.
2. Incoming audio is processed continuously.
3. BPM path runs BeatNet + autocorrelation.
4. Key path runs CQT extraction and MusicalKeyCNN inference.
5. Native emits `onKey` events when key/confidence changes.

## Public API
TypeScript surface (`@keyed/engine`):

- `loadKeyModel(): boolean`
- `isKeyReady(): boolean`
- `getKey(): KeyResult | null`
- `getKeyFrameCount(): number`
- event: `onKey` with `{ camelot, notation, confidence }`

## Native Integration
- iOS bridge: `packages/engine/ios/EngineBridge.*`, `EngineModule.swift`
- Android bridge: `packages/engine/android/.../EngineModule.kt`, `EngineJNI.cpp`
- Core C++: `packages/engine/cpp/KeyModel.*`, `CqtExtractor.*`, `Engine.*`

## Testing
Coverage includes:

- `test_key_model.cpp`
- `test_cqt_extractor.cpp`
- `test_resampler.cpp`
- `test_autocorr_bpm.cpp`
- `test_error_handling.cpp`
- key checks in `test_e2e.cpp`

Run native tests:

```bash
bun run test:native
```

## Notes
- Key confidence improves with longer listening windows.
- `getKeyFrameCount()` can be used in UI to gate confidence messaging.
