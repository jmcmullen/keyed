# Key Detection (MusicalKeyCNN)

Real-time musical key detection for DJs, built around a streaming CQT frontend, a MusicalKeyCNN ONNX model, Camelot-wheel output, and a smoother that keeps the live result from jumping around.

## Overview

This is the part of the engine that answers the question a DJ actually cares about: "What key is this track in, and how safe is it to mix from here?"

The pipeline, end to end: audio (44100 Hz) -> CQT frames -> rolling spectrogram window -> MusicalKeyCNN -> softmax over 24 keys -> Camelot mapping -> multi-window smoothing.

**What it gives you:**
- 24-class key detection: 12 minor keys and 12 major keys
- Camelot codes alongside conventional notation
- A first provisional result after about 5 seconds
- Longer live and stable windows at about 10 and 20 seconds
- A bounded 4-minute rolling CQT history
- Native C++ inference, so key detection does not compete with React Native UI work

---

## What this adds around MusicalKeyCNN

The model is only one part of the feature. The app also has to turn live microphone audio into model-shaped input, keep memory bounded, and avoid showing a key that flickers every time the classifier has a close second choice.

| Component | Model expects | This implementation |
|-----------|---------------|---------------------|
| **Input** | CQT spectrogram | Streaming CQT extracted from 44.1kHz microphone audio |
| **Time context** | 105 x N feature map | Fast, live, and stable windows over the same rolling buffer |
| **Output** | 24 logits | Softmax, confidence, margin, Camelot code, and notation |
| **Stability** | Single prediction | Recency-weighted smoother with confidence and margin voting |
| **Runtime** | Batch tensor | Native C++ ONNX Runtime with dynamic time dimension |

The goal is not to pretend key detection is instant. The engine gives an early answer, then lets more harmonic context improve the result without blocking the BPM path.

---

## The CQT frontend

### Why CQT?

Musical key is mostly a harmonic problem, so a linear-frequency FFT is awkward: the bins are evenly spaced in hertz, while musical notes are spaced by ratios. The Constant-Q Transform uses logarithmically spaced frequency bins, which line up much more naturally with pitch.

That matters for three reasons:

1. **Pitch spacing is musical.** Each octave is divided into the same number of bins, so the distance from C to E has the same shape in low and high registers.
2. **Bass notes get enough window.** Lower frequencies need longer windows to resolve pitch cleanly. CQT gives each bin its own filter length.
3. **The model sees familiar input.** The native extractor matches the model preprocessing parameters instead of feeding it a visually similar but numerically different spectrogram.

### Parameters

The extractor matches the MusicalKeyCNN preprocessing shape:

| Parameter | Value | Notes |
|-----------|-------|-------|
| Sample rate | 44100 Hz | Native microphone stream |
| Hop length | 8820 samples | About 200ms per frame |
| Frame rate | ~5 FPS | `44100 / 8820` |
| Bins | 105 | Frequency bins per frame |
| Bins per octave | 24 | Two bins per semitone |
| Lowest frequency | 65 Hz | Around C2 |
| Longest filter | ~23200 samples | Needed for the lowest bin |

The C++ extractor precomputes one complex Hann-windowed kernel per CQT bin. At runtime it applies each kernel, takes magnitude, and stores `log1p(magnitude)` features.

### Details that matter

- **Centered framing:** the streaming extractor keeps enough past and future context for the longest filter, with zero padding for the first frame.
- **Variable filter lengths:** low bins use long windows, high bins use shorter ones.
- **librosa-compatible spacing:** bin centers follow `fmin * 2^(k / bins_per_octave)`, which keeps the native CQT aligned with the training frontend.
- **Time-major storage:** the engine stores CQT frames as `[time][freq]` because frames arrive over time, then transposes to `[freq][time]` before ONNX inference.

---

## The ONNX model

### Input and output

The key model is wrapped by `KeyModel` and run through ONNX Runtime.

| Name | Shape | Description |
|------|-------|-------------|
| `input` | `[1, 1, 105, N]` | CQT spectrogram, one channel, variable time frames |
| `output` | `[1, 24]` | Key logits |

The model uses an adaptive pooling architecture, so the time dimension can vary. The engine uses that to run different windows without maintaining separate models.

### Class mapping

The 24 classes are already in Camelot wheel order, not chromatic order:

| Class range | Camelot range | Notation order |
|-------------|---------------|----------------|
| 0-11 | `1A`-`12A` | `G#m`, `Ebm`, `Bbm`, `Fm`, `Cm`, `Gm`, `Dm`, `Am`, `Em`, `Bm`, `F#m`, `C#m` |
| 12-23 | `1B`-`12B` | `B`, `F#`, `Db`, `Ab`, `Eb`, `Bb`, `F`, `C`, `G`, `D`, `A`, `E` |

After inference, the wrapper applies softmax, picks the top class, records the confidence, and keeps the margin between the top two classes. That margin is useful because key detection often has musically plausible near-misses: relative majors, relative minors, and neighbouring Camelot keys can all look convincing over short windows.

---

## Multi-window inference

The engine does not rely on a single fixed chunk of audio. It runs the same model over three contexts:

| Window | Frames | Duration | Role |
|--------|--------|----------|------|
| Fast | 25 | ~5 seconds | First provisional answer |
| Live | 50 | ~10 seconds | Primary live estimate |
| Stable | 100 | ~20 seconds | Longer harmonic context |

After the first result, inference runs every 25 CQT frames, about every 5 seconds. The CQT history is kept in a ring buffer capped at 1200 frames, which is about 4 minutes at 5 FPS. That is enough context for long blends and full-track listening without letting memory grow for the whole app session.

The key path runs before the BPM resampling path inside `Engine::processAudio()`, but it is cadence-limited. CQT extraction keeps up with incoming audio, while CNN inference only runs when enough new frames have arrived.

---

## Smoothing

Raw classifier output is too jumpy for a live DJ tool. The smoother keeps a small history of recent predictions and scores each vote by:

- softmax confidence
- margin between the top two classes
- window type (`Live` votes weigh more than `Stable`, and both weigh more than `Fast`)
- recency, with a 3-second half-life

The accepted key changes immediately when there is strong evidence, but weak one-off switches are ignored. Repeated recent evidence can still override an older stable result, which matters when the DJ moves into a new track during a mix.

This is why the UI can show a provisional key early without treating the first classifier result as permanent truth.

---

## Native integration

The main implementation lives here:

- CQT frontend: `packages/engine/cpp/CqtExtractor.*`
- ONNX wrapper: `packages/engine/cpp/KeyModel.*`
- Smoothing: `packages/engine/cpp/KeySmoother.hpp`
- Pipeline orchestration: `packages/engine/cpp/Engine.*`
- TypeScript bridge contract: `packages/engine/src/Engine.types.ts`

The public TypeScript surface stays intentionally small:

- `loadKeyModel()`
- `isKeyReady()`
- `getKey()`
- `getKeyFrameCount()`
- event: `onKey` with `{ camelot, notation, confidence }`

---

## Testing

Unit tests cover the parts that are easiest to get subtly wrong:

| Test area | What it checks |
|-----------|----------------|
| CQT constants | Sample rate, hop length, bin count, bins per octave, and frame rate |
| CQT spacing | `65 Hz`, octave doubling, and decreasing filter lengths |
| CQT signal response | A 440 Hz sine wave peaks around the expected CQT bin |
| Streaming CQT | Hop scheduling and frame counts over pushed audio |
| ONNX wrapper | Model loading, inference, output range, and Camelot mapping |
| Smoother | Early acceptance, weak-switch rejection, repeated-switch acceptance, and new-track override |

Run the native tests:

```bash
bun run test:native
```

---

## Limitations

1. **It needs harmonic context.** Five seconds is enough for a provisional answer, not a final verdict.
2. **Confidence is not certainty.** A high score means the model preferred one class, not that the room recording was clean or the track stayed in one key.
3. **Ambiguous music is genuinely ambiguous.** Relative major/minor, modal tracks, sparse breakdowns, and key changes can produce plausible competing classes.
4. **The frontend assumes 44.1kHz audio.** That keeps the CQT aligned with the model preprocessing.
5. **Reset matters.** Call `reset()` when starting a fresh stream so old harmonic context does not bleed into the next reading.
