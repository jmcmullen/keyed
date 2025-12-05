# Audio Analysis Approach

## Overview

Keyed uses **essentia.js** (WASM-compiled C++ library) for offline audio analysis, paired with **@siteed/expo-audio-studio** for microphone capture in React Native.

## Libraries

### Audio Capture: @siteed/expo-audio-studio
- Expo-compatible microphone capture
- Real-time PCM streaming
- Waveform data for visualisation

### Audio Analysis: essentia.js
- WASM build runs in JS thread (no native module required)
- AGPLv3 license (free, open source)
- Academic backing from Music Technology Group, Barcelona

## Analysis Algorithms

### BPM Detection: PercivalBpmEstimator
- Input: mono audio signal (Float32Array)
- Output: BPM estimate
- Parameters: `minBPM=50`, `maxBPM=210`, `sampleRate=44100`
- No native confidence output; derive from signal energy or beat consistency

### Key Detection: KeyExtractor
- Input: mono audio signal (Float32Array)
- Output: key (e.g. "A"), scale ("major"/"minor"), strength (0-1)
- Parameters: `profileType='edma'` (tuned for electronic/dance music), `sampleRate=44100`
- Strength value (0-1) maps directly to confidence percentage

## Real-time Processing Strategy

1. **Accumulate audio** in a circular buffer as mic streams PCM data
2. **Analyze every N seconds** (e.g. every 3-5 seconds) using the accumulated buffer
3. **Update UI** with latest BPM/key estimates and confidence values
4. **Continue accumulating** until user stops or silence detected

Minimum buffer for reliable analysis: ~5 seconds of audio at 44.1kHz.

## Confidence Thresholds

Based on essentia's `key_strength` output (0-1 range):

| Level | Range | UI Colour |
|-------|-------|-----------|
| High | > 0.6 | Green |
| Medium | 0.3 - 0.6 | Amber |
| Low | < 0.3 | Red |

Note: These thresholds are tuned for the `edma` profile with electronic music. Adjust based on real-world testing.

## Code Example

```ts
import Essentia from "essentia.js"
import { EssentiaWASM } from "essentia.js/dist/essentia-wasm.module"

const essentia = new Essentia(EssentiaWASM)

// BPM detection
const bpmResult = essentia.PercivalBpmEstimator(audioSignal, 1024, 2048, 128, 128, 210, 50, 44100)
const bpm = bpmResult.bpm

// Key detection (using KeyExtractor for full signal)
const keyResult = essentia.KeyExtractor(audioSignal, true, 4096, 4096, 12, 3500, 60, 25, 0.2, "edma", 44100, 0.0001, 440, "cosine", "hann")
const key = keyResult.key        // e.g. "A"
const scale = keyResult.scale    // "major" or "minor"
const strength = keyResult.strength // 0-1 confidence
```

## Accuracy Notes

- Essentia uses algorithms similar to Mixxx (open-source DJ software)
- `edma` profile specifically tuned for electronic/dance music
- Expected accuracy: ~80% for key detection on electronic music
- BPM detection generally reliable; watch for half/double time on slower tracks
