# Keyed

**On-device BPM and key detection for DJs.**

Keyed listens through the microphone and returns live BPM, musical key, Camelot code, and confidence readings without sending audio off the device. It is built for DJs checking tempo and harmonic compatibility while mixing vinyl, CDJs, or digital sources.

## Demo

<img src="docs/keyed.gif" alt="Keyed app demo" width="320">

## Highlights

- Real-time BPM detection with decimal output, confidence scoring, and half/double-time correction for DJ tempo ranges
- Real-time key detection with standard notation and Camelot codes
- Beat-reactive visual UI driven by native audio analysis events
- Local detection history backed by SQLite
- Fully offline audio processing
- Shared native C++ engine with iOS and Android bridges

## Architecture

Keyed is a Bun monorepo with an Expo React Native app and a native analysis engine.

```text
apps/native       Expo Router mobile app
packages/engine   Expo native module, C++ DSP, ONNX inference, iOS/Android bridges
packages/db       Drizzle schema and Expo SQLite hooks
packages/config   Shared TypeScript configuration
docs              Product and engine notes
```

The native engine owns the real-time audio path so raw microphone buffers do not cross the JavaScript bridge.

- BPM pipeline: microphone audio -> resampling -> mel features -> BeatNet ONNX -> autocorrelation tempo stabilization -> BPM confidence
- Key pipeline: microphone audio -> CQT features -> MusicalKeyCNN ONNX -> key smoothing -> notation, Camelot code, and confidence
- Visual pipeline: native beat/downbeat activations -> throttled app events -> Skia/Reanimated beat aura
- Persistence: detections are saved locally with Drizzle and Expo SQLite

## Tech Stack

- Expo 54, React Native 0.81, React 19, Expo Router
- TypeScript, Bun workspaces, Turborepo
- React Native Reanimated, Skia, and `react-native-unistyles`
- Native C++ DSP and ONNX Runtime
- Drizzle ORM with Expo SQLite

## Local Setup

```bash
git clone https://github.com/jmcmullen/keyed.git
cd keyed
bun install
```

Native development requires the usual iOS or Android toolchain for the target platform.

## Verification

```bash
bun run check
bun run check-types
cd apps/native && bun test tests
bun run test:native
```

## License

Keyed is licensed under the [MIT License](LICENSE).
