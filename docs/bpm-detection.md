# BPM Detection

Real-time BPM detection for DJs, built on a fine-tuned BeatNet neural network and autocorrelation.

## Overview

This is the part of the engine that tracks beats and downbeats and works out the tempo, live. It builds on [BeatNet](https://github.com/mjhydri/BeatNet), a CRNN for beat tracking, then adds a fine-tuned model and autocorrelation-based tempo estimation aimed squarely at DJ and electronic music.

The pipeline, end to end: audio (22050 Hz) → 272-dim mel features → neural network → beat/downbeat activations → autocorrelation → BPM.

**What it gives you:**
- 0.07 BPM average error on an internal Beatport EDM evaluation set
- Tuned for DJ music (75-165 BPM with octave correction)
- Real-time at 50 FPS (20ms frames)
- Hardware acceleration (CoreML on iOS, NNAPI on Android)
- Native C++, for the speed

---

## What this adds to BeatNet

The original BeatNet is a solid starting point. This implementation takes it several steps further:

| Component | Original BeatNet | This Implementation |
|-----------|------------------|---------------------|
| **Tempo estimation** | Dynamic Bayesian Network (discrete, non-differentiable) | Autocorrelation on activations (continuous, differentiable) |
| **Training signal** | Frame-level classification only | Dual loss: frame classification + BPM supervision (10:1 weight) |
| **Ground truth** | Manual annotations or model pseudo-labels | Metadata BPM + onset envelope phase alignment |
| **Audio domain** | Clean studio recordings | Phone microphone simulation (9 degradation types) |
| **Inference** | Python with librosa | C++ with ONNX Runtime + hardware acceleration |

The neural network architecture itself (Conv1D → LSTM → Dense) is left exactly as the original paper had it.

---

## Hardware Acceleration

| Platform | Execution Provider | Hardware |
|----------|-------------------|----------|
| iOS | CoreML | Neural Engine (A11+) |
| Android | NNAPI | GPU/DSP/NPU |
| macOS (tests) | CPU | x86/ARM |

---

## BPM Detection: the autocorrelation method

### Why autocorrelation?

The obvious way to get tempo is to measure the gaps between detected beats. It also happens to be the wrong way, for four reasons. Running autocorrelation directly on the network's activations does better because:

1. **No quantisation error.** Beat-interval methods snap to frame boundaries (20ms). A 132 BPM track has 22.727 frames per beat, so the intervals end up alternating between 22 and 23 frames and the tempo wobbles.

2. **It uses all the data.** Interval methods only look at the beats they detected. Autocorrelation uses the whole activation signal, including the peaks that never crossed the threshold.

3. **It survives a missed beat.** Drop a single beat and an interval-based estimate falls apart. Autocorrelation reads periodicity from the entire signal, so one miss barely registers.

4. **Sub-frame precision.** Parabolic interpolation on the autocorrelation peaks gets you finer than the 20ms frame period.

### Algorithm

1. Sum beat + downbeat activations for each frame
2. Compute autocorrelation via FFT (O(n log n))
3. Find peak in valid tempo range (60-180 BPM). At 50 FPS, lag 50 = 60 BPM and lag 17 ≈ 176 BPM (the fast end of the range)
4. Refine the peak position with parabolic interpolation for sub-frame accuracy
5. Convert lag to BPM: `bpm = round(60 × FPS / refinedLag)`

### Octave correction

DJ music mostly sits between 75 and 165 BPM. If the detected tempo lands outside that, we apply octave correction (double it below 75, halve it above 165) whenever doing so brings it back into the DJ range. This is what stops a 174 turning up as an 87.

---

## The fine-tuned model

We ship a fine-tuned BeatNet model that does noticeably better on DJ and electronic music.

| Model | File | Use Case |
|-------|------|----------|
| **Fine-tuned** | `packages/engine/models/beatnet.onnx` | Primary - DJ/electronic music |

### Accuracy

Measured on the same test set (Beatport EDM previews with curator-verified BPM):

| Method | Avg BPM Error |
|--------|---------------|
| Original BeatNet + DBN | 1.47 BPM |
| **Fine-tuned + Autocorrelation** | **0.07 BPM** |

Two things drive the improvement: autocorrelation sidesteps the DBN's quantisation error, and fine-tuning with BPM supervision teaches the network to produce cleaner periodic activations in the first place.

### The hard parts, and how we got around them

**Ground truth.**

The lazy way to fine-tune a beat detector is to feed it its own predictions as labels. The problem is circular: the model can only learn to copy what it already does, never to fix what it gets systematically wrong.

So we build the ground truth from signals the model has no say in:

1. **BPM from metadata:** the authoritative tempo from Beatport listings, verified by human curators
2. **Beat phase from onset detection:** compute spectral flux onset strength at 50 FPS, then try 50 candidate phases and keep the one where beats line up most strongly with the detected onsets (scored on mid-track audio, where things are steadier)
3. **Downbeat from onset intensity:** assuming 4/4 meter (standard for EDM), test which of the four beat positions has the strongest average onset. That is almost always the downbeat, thanks to the kick
4. **Soft labels:** convert beat times to frame-level probabilities with Gaussian weighting (σ=1 frame) to allow for a little annotation slop

The training signal comes from audio analysis and metadata, never from the model being trained.

**Differentiable tempo estimation.**

The original BeatNet post-processes with a Dynamic Bayesian Network. The DBN quantises tempo to discrete states and decodes with Viterbi, and neither step has useful gradients, so training can only improve frame-level beat detection, not tempo directly.

To train tempo end to end, we swap the DBN for differentiable autocorrelation. The trick is a soft argmax (a temperature-scaled softmax over lag positions) in place of a hard argmax, which lets gradients flow back through the tempo estimate.

The loss uses Gaussian-weighted soft targets centred on the correct tempo lag (σ=2 frames), so the gradient stays smooth and the model still learns when its estimate is close but not exact. It also handles octave ambiguity gracefully: half-time and double-time guesses get partial credit in proportion to how far off they are.

**Class imbalance.**

Beats and downbeats are rare (~2 frames in every 25). A weighted loss upweights the beat and downbeat classes against all that silence.

### Training methodology

- **Dual-loss architecture:** frame-level classification loss combined with BPM-specific supervision (10x weighted)
- **Transfer learning:** starts from pretrained BeatNet weights, then fine-tunes mainly the output layers and the LSTM

### Phone microphone simulation

The model is built for the real world, where the audio arrives through a phone mic in a room rather than off a clean master. To see what that does to the sound, we compared the spectra of clean masters against phone recordings of the same tracks:

| Frequency Band | Clean Master | Phone Recording |
|----------------|--------------|-----------------|
| Sub-bass (<60 Hz) | 75% of energy | 13% of energy |
| Bass (60-250 Hz) | Flat response | +4-6 dB boost (room resonance) |
| Mids (500-2000 Hz) | Flat response | -2-4 dB scoop |
| Highs (>6000 Hz) | Present | Significant rolloff |

Training then runs every sample through a degradation pipeline that recreates those characteristics:

| Degradation | Parameters | Acoustic Basis |
|-------------|------------|----------------|
| High-pass filter | 80-150 Hz cutoff | Phone mics physically cannot capture sub-bass |
| Low-pass filter | 4000-6000 Hz cutoff | Measured HF rolloff in phone recordings |
| Bass resonance | 100-180 Hz, +3-8 dB | Room modes and proximity effect |
| Mid-scoop | 600-900 Hz, -2-5 dB | Small diaphragm characteristic |
| Room reverb | 15-40ms RT, early reflections | Typical club/venue acoustics |
| Noise floor | Shaped pink noise | Mic self-noise and ambient |
| Compression | 2-4:1 ratio, soft knee | Limited dynamic range of phone ADC |
| Volume modulation | 0.2-0.8 Hz sinusoid | Hand movement and distance variation |

Every parameter is randomised per sample, so the model learns to cope with the whole range of recording conditions instead of overfitting to one.

---

## Feature extraction

### The 272-dimensional feature vector

The model wants **272 features per frame**:
- **136 log-mel spectrogram bands**
- **136 spectral difference bands** (half-wave rectified)

The 136 bands come from madmom's `LogarithmicFilterbank`:
- 24 bands per octave
- Frequency range: 30 Hz to 17000 Hz
- Reference frequency: 440 Hz (A4)

### Audio parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Sample rate | 22050 Hz | Required |
| Hop length | 441 samples | 20ms → 50 FPS |
| Window length | 1411 samples | 64ms |
| FFT size | 1411 | Matches window length (no zero-padding) |
| Mel bands | 136 | |
| Model input | 272 | 136 mel + 136 diff |

### Details that matter

- **Log10 scaling:** uses `log10(1 + x)`, not natural log
- **Filterbank resolution:** a 1411-sample FFT (no zero-padding) with madmom-compatible bin mapping, to land on exactly 136 bands

---

## The ONNX model

### Architecture

| Layer | Output Shape | Notes |
|-------|--------------|-------|
| Input | [1, 1, 272] | Mel features |
| Conv1D | [1, 2, 263] | kernel=10, ReLU |
| MaxPool1d | [1, 2, 131] | pool=2 |
| Flatten | [1, 262] | |
| Dense | [1, 150] | ReLU |
| LSTM | [1, 150] | 2 layers, stateful |
| Dense | [1, 3] | Softmax → [beat, downbeat, non-beat] |

### I/O specification

| Name | Shape | Description |
|------|-------|-------------|
| `input` | `[1, 1, 272]` | Mel features for one frame |
| `hidden_in` | `[2, 1, 150]` | LSTM hidden state |
| `cell_in` | `[2, 1, 150]` | LSTM cell state |
| `output` | `[1, 1, 3]` | Softmax probabilities |
| `hidden_out` | `[2, 1, 150]` | Updated hidden state |
| `cell_out` | `[2, 1, 150]` | Updated cell state |

---

## Testing

Unit tests check each component against a Python reference:

| Category | Tolerance |
|----------|-----------|
| FFT | < 1e-5 vs numpy |
| Filterbank | < 1e-6 vs madmom |
| Mel Features | MAE < 0.01 vs madmom |
| ONNX Model | < 5% per frame vs Python |
| E2E Pipeline | ±1 BPM |

A 15-track EDM smoke batch came in within ±1 BPM on every track. The headline 0.07 BPM figure above is the average error across the shared evaluation set.

---

## Performance

| Component | Time |
|-----------|------|
| FFT (1411-pt) | ~0.1ms |
| Mel extraction | ~0.2ms |
| ONNX inference | ~2ms (CPU), <1ms (Neural Engine) |
| Autocorrelation | ~0.1ms |
| **Total per frame** | **~3ms** |

### Convergence time

| Duration | Accuracy |
|----------|----------|
| < 2 seconds | Not available |
| 2-4 seconds | Good |
| 4-10 seconds | Excellent |
| 10+ seconds | Best |

---

## Limitations

1. **Sample rate must be 22050 Hz.** The model was trained at that rate.
2. **Tempo range:** 60-180 BPM detected, 75-165 BPM after octave correction.
3. **DJ music first.** It works best on music with clear rhythmic elements. A track with no real beat gives it nothing to lock onto.
4. **Convergence:** BPM needs about 2 seconds of audio before it means anything.
5. **LSTM state:** call `reset()` when you start a new audio stream.

---

## References

- [BeatNet Paper (ISMIR 2021)](https://arxiv.org/abs/2108.03576)
- [BeatNet GitHub](https://github.com/mjhydri/BeatNet)
- [Madmom Documentation](https://madmom.readthedocs.io/)
- [ONNX Runtime](https://onnxruntime.ai/)
