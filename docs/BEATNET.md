# BeatNet Implementation

Real-time beat detection and BPM analysis for DJ applications using a fine-tuned neural network and autocorrelation.

## Overview

This engine provides real-time beat/downbeat tracking and BPM detection. It builds on [BeatNet](https://github.com/mjhydri/BeatNet) (a CRNN for beat tracking) with a fine-tuned model and autocorrelation-based tempo estimation optimized for DJ/electronic music.

The pipeline: audio (22050 Hz) → 272-dim mel features → neural network → beat/downbeat activations → autocorrelation → BPM.

**Key Features:**
- Near-perfect BPM accuracy (100% on test set, 0.00 average error)
- Optimized for DJ music (75-165 BPM with octave correction)
- Real-time processing at 50 FPS (20ms frames)
- Hardware acceleration (CoreML on iOS, NNAPI on Android)
- Native C++ implementation for maximum performance

---

## Contributions

This implementation extends the original BeatNet architecture with several techniques:

| Component | Original BeatNet | This Implementation |
|-----------|------------------|---------------------|
| **Tempo estimation** | Dynamic Bayesian Network (discrete, non-differentiable) | Autocorrelation on activations (continuous, differentiable) |
| **Training signal** | Frame-level classification only | Dual loss: frame classification + BPM supervision (10:1 weight) |
| **Ground truth** | Manual annotations or model pseudo-labels | Metadata BPM + onset envelope phase alignment |
| **Audio domain** | Clean studio recordings | Phone microphone simulation (9 degradation types) |
| **Inference** | Python with librosa | C++ with ONNX Runtime + hardware acceleration |

The BeatNet neural network architecture (Conv1D → LSTM → Dense) is unchanged from the original paper.

---

## Hardware Acceleration

| Platform | Execution Provider | Hardware |
|----------|-------------------|----------|
| iOS | CoreML | Neural Engine (A11+) |
| Android | NNAPI | GPU/DSP/NPU |
| macOS (tests) | CPU | x86/ARM |

---

## BPM Detection: Autocorrelation Method

### Why Autocorrelation?

Traditional beat-interval methods have systematic errors. Autocorrelation directly on neural network activations achieves higher accuracy because:

1. **No quantization error**: Beat interval methods quantize to frame boundaries (20ms). A 132 BPM track has 22.727 frames per beat, causing intervals to alternate between 22 and 23 frames.

2. **Uses all activation data**: Interval methods only use detected beat times. Autocorrelation uses the full activation signal, capturing sub-threshold peaks.

3. **Robust to missed beats**: A single missed beat corrupts interval-based BPM. Autocorrelation is robust because periodicity is computed from the entire signal.

4. **Sub-frame precision**: Parabolic interpolation on autocorrelation peaks provides accuracy below the 20ms frame period.

### Algorithm

1. Sum beat + downbeat activations for each frame
2. Compute autocorrelation via FFT (O(n log n))
3. Find peak in valid tempo range (60-180 BPM) — at 50 FPS, lag 17 = 180 BPM, lag 50 = 60 BPM
4. Refine peak position with parabolic interpolation for sub-frame accuracy
5. Convert lag to BPM: `bpm = round(60 × FPS / refinedLag)`

### Octave Correction

DJ music typically falls in the 75-165 BPM range. If the detected BPM falls outside this range, we apply octave correction (double if below 75, halve if above 165) when the corrected value would fall within the DJ range.

---

## Fine-tuned Model

We use a fine-tuned BeatNet model that achieves near-perfect accuracy on DJ/electronic music.

| Model | File | Use Case |
|-------|------|----------|
| **Fine-tuned** | `packages/engine/models/beatnet.onnx` | Primary - DJ/electronic music |

### Accuracy Comparison

Evaluated on the same test set (Beatport EDM previews with curator-verified BPM):

| Method | Avg BPM Error |
|--------|---------------|
| Original BeatNet + DBN | 1.47 BPM |
| **Fine-tuned + Autocorrelation** | **0.07 BPM** |

The improvement comes from two sources: (1) autocorrelation avoids DBN quantization error, and (2) fine-tuning with BPM supervision teaches the network to produce cleaner periodic activations.

### Technical Challenges & Solutions

**Challenge: Ground Truth Generation**

A common approach for fine-tuning beat detection models is to use the model's own predictions as pseudo-labels. This creates a circular dependency: the model can only learn to replicate its existing behavior, not correct systematic errors.

We break this dependency by generating ground truth entirely from external signals:

1. **BPM from metadata**: Use the authoritative tempo from Beatport track listings (verified by human curators)

2. **Beat phase from onset detection**: Compute spectral flux onset strength at 50 FPS, then search 50 candidate phases to find where beats align strongest with detected onsets (scoring on mid-track audio to avoid unstable edges)

3. **Downbeat from onset intensity**: Assuming 4/4 meter (standard for EDM), test which beat position (1, 2, 3, or 4) has the strongest average onset—this is typically the downbeat due to kick drum emphasis

4. **Soft labels**: Convert beat times to frame-level probabilities using Gaussian weighting (σ=1 frame) to handle annotation uncertainty

This approach means the training signal comes from audio analysis + metadata, never from the model being trained.

**Challenge: Differentiable Tempo Estimation**

The original BeatNet uses a Dynamic Bayesian Network (DBN) for post-processing. The DBN quantizes tempo to discrete states and uses Viterbi decoding—neither operation has useful gradients. This means training can only optimize frame-level beat detection, not tempo accuracy directly.

To enable end-to-end tempo learning, we replace DBN post-processing with differentiable autocorrelation. The key insight is using a soft argmax (temperature-scaled softmax over lag positions) instead of hard argmax, allowing gradients to flow back through tempo estimation.

The loss uses Gaussian-weighted soft targets centered on the correct tempo lag (σ=2 frames). This provides a smooth gradient landscape—the model receives learning signal even when the estimated tempo is close but not exact. This approach also handles octave ambiguity gracefully: half-time and double-time estimates receive partial credit proportional to their distance from the target.

**Challenge: Class Imbalance**

Beats and downbeats are rare events (~2 frames per 25). We use weighted loss to upweight beat and downbeat classes relative to silence.

### Training Methodology

- **Dual-loss architecture**: Combines frame-level classification loss with BPM-specific supervision (10x weighted)
- **Transfer learning**: Starts from pretrained BeatNet weights, fine-tunes primarily output layers and LSTM

### Phone Microphone Simulation

The model is optimized for real-world DJ scenarios where audio is captured through phone microphones. To understand the acoustic differences, we analyzed spectral characteristics of clean masters vs. phone recordings of the same tracks:

| Frequency Band | Clean Master | Phone Recording |
|----------------|--------------|-----------------|
| Sub-bass (<60 Hz) | 75% of energy | 13% of energy |
| Bass (60-250 Hz) | Flat response | +4-6 dB boost (room resonance) |
| Mids (500-2000 Hz) | Flat response | -2-4 dB scoop |
| Highs (>6000 Hz) | Present | Significant rolloff |

Training includes an audio degradation pipeline that models these characteristics:

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

All parameters are randomized per training sample. This prevents overfitting to specific degradation patterns while ensuring robustness across recording conditions.

---

## Feature Extraction

### 272-Dimensional Feature Vector

The model expects **272 features per frame**:
- **136 log-mel spectrogram bands**
- **136 spectral difference bands** (half-wave rectified)

The 136 bands come from madmom's `LogarithmicFilterbank`:
- 24 bands per octave
- Frequency range: 30 Hz to 17000 Hz
- Reference frequency: 440 Hz (A4)

### Audio Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Sample rate | 22050 Hz | Required |
| Hop length | 441 samples | 20ms → 50 FPS |
| Window length | 1411 samples | 64ms |
| FFT size | 2048 | Zero-padded for efficiency |
| Mel bands | 136 | |
| Model input | 272 | 136 mel + 136 diff |

### Critical Details

- **Log10 scaling**: Uses `log10(1 + x)`, not natural log
- **Filterbank resolution**: 2048-sample FFT for speed, but frequency mapping uses original 1411-sample resolution to get exactly 136 bands

---

## ONNX Model

### Architecture

| Layer | Output Shape | Notes |
|-------|--------------|-------|
| Input | [1, 1, 272] | Mel features |
| Conv1D | [1, 2, 263] | kernel=10, ReLU |
| MaxPool1d | [1, 2, 131] | pool=2 |
| Flatten | [1, 262] | |
| Dense | [1, 150] | ReLU |
| LSTM | [1, 150] | 2 layers, stateful |
| Dense | [1, 3] | Softmax → [non-beat, beat, downbeat] |

### I/O Specification

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

Unit tests validate each component against Python reference implementations:

| Category | Tolerance |
|----------|-----------|
| FFT | < 1e-5 vs numpy |
| Filterbank | < 1e-6 vs madmom |
| Mel Features | MAE < 0.01 vs madmom |
| ONNX Model | < 5% per frame vs Python |
| E2E Pipeline | ±1 BPM |

Batch testing on 15 EDM tracks achieved 100% accuracy (0.00 average BPM error).

---

## Performance

| Component | Time |
|-----------|------|
| FFT (2048-pt) | ~0.1ms |
| Mel extraction | ~0.2ms |
| ONNX inference | ~2ms (CPU), <1ms (Neural Engine) |
| Autocorrelation | ~0.1ms |
| **Total per frame** | **~3ms** |

### Convergence Time

| Duration | Accuracy |
|----------|----------|
| < 2 seconds | Not available |
| 2-4 seconds | Good |
| 4-10 seconds | Excellent |
| 10+ seconds | Best |

---

## Limitations

1. **Sample rate must be 22050 Hz** - The model was trained at this rate.

2. **Tempo range**: 60-180 BPM detection, 75-165 BPM after octave correction.

3. **DJ music optimized**: Works best on music with clear rhythmic elements.

4. **Convergence time**: BPM requires ~2 seconds of audio.

5. **LSTM state**: Must call `reset()` when starting a new audio stream.

---

## References

- [BeatNet Paper (ISMIR 2021)](https://arxiv.org/abs/2108.03576)
- [BeatNet GitHub](https://github.com/mjhydri/BeatNet)
- [Madmom Documentation](https://madmom.readthedocs.io/)
- [ONNX Runtime](https://onnxruntime.ai/)
