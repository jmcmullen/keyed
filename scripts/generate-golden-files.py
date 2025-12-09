#!/usr/bin/env python3
"""
Generate golden files for C++ unit tests from Python reference implementations.

This script generates EXACT expected outputs that the C++ implementation
must match. Any difference indicates a bug.

Usage:
    python scripts/generate-golden-files.py

Environment variables:
    BEATNET_SRC: Path to BeatNet source (default: ~/dev/BeatNet/src)

Requirements:
    pip install numpy librosa madmom

Output:
    packages/engine/tests/golden/*.json
"""

import json
import os
import sys
from pathlib import Path

import numpy as np

# Add BeatNet to path (configurable via BEATNET_SRC environment variable)
BEATNET_SRC = os.path.abspath(
    os.environ.get("BEATNET_SRC", os.path.expanduser("~/dev/BeatNet/src"))
)
sys.path.insert(0, BEATNET_SRC)

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
GOLDEN_DIR = PROJECT_ROOT / "packages/engine/tests/golden"
AUDIO_DIR = PROJECT_ROOT / "apps/native/assets/test"

GOLDEN_DIR.mkdir(parents=True, exist_ok=True)

# BeatNet configuration constants (must match MelConfig in C++)
FFT_SIZE = 1411      # 64ms at 22050 Hz
SAMPLE_RATE = 22050
HOP_SIZE = 441       # 20ms -> 50 FPS
NUM_BANDS = 24       # Bands per octave


def save_json(data: dict, filename: str):
    """Save data to JSON with high precision for floats."""
    path = GOLDEN_DIR / filename

    class HighPrecisionEncoder(json.JSONEncoder):
        def default(self, obj):
            if isinstance(obj, np.ndarray):
                return obj.tolist()
            if isinstance(obj, np.floating):
                return float(obj)
            if isinstance(obj, np.integer):
                return int(obj)
            return super().default(obj)

    with open(path, "w") as f:
        json.dump(data, f, cls=HighPrecisionEncoder, indent=2)

    print(f"  Saved: {path}")


def generate_fft_golden():
    """Generate FFT test vectors using numpy.fft with 1411-point FFT."""
    print("\n=== Generating FFT Golden Files ===")

    # Use exact BeatNet config: 1411-point FFT
    fft_size = FFT_SIZE  # 1411
    sample_rate = float(SAMPLE_RATE)

    t = np.arange(fft_size) / sample_rate
    sine_440 = np.sin(2 * np.pi * 440 * t).astype(np.float32)
    fft_result = np.fft.rfft(sine_440)

    data = {
        "description": "FFT golden files from numpy.fft.rfft (1411-point FFT matching BeatNet)",
        "numpy_version": np.__version__,
        "fft_size": fft_size,
        "sample_rate": sample_rate,
        "test_cases": {
            "sine_440hz": {
                "fft_size": fft_size,
                "sample_rate": sample_rate,
                "input": sine_440.tolist(),
                "output_real": np.real(fft_result).tolist(),
                "output_imag": np.imag(fft_result).tolist(),
                "magnitude": np.abs(fft_result).tolist(),
                "power": (np.abs(fft_result) ** 2).tolist(),
            },
            "impulse": {
                "fft_size": fft_size,
                "input": [1.0] + [0.0] * (fft_size - 1),
                "magnitude": [1.0] * (fft_size // 2 + 1),
            },
        }
    }

    save_json(data, "fft_golden.json")


def generate_filterbank_golden():
    """Generate filterbank weights golden file from madmom."""
    print("\n=== Generating Filterbank Golden File ===")

    try:
        from madmom.audio.filters import LogarithmicFilterbank
    except ImportError as e:
        print(f"  ERROR: {e}")
        print("  Install with: pip install madmom")
        return

    # Create the filterbank with exact BeatNet config
    # madmom uses num_fft_bins = fft_size // 2 (excludes Nyquist)
    # bin_frequencies formula must match C++: i * sample_rate / (num_fft_bins * 2)
    num_fft_bins = FFT_SIZE // 2  # 705
    bin_frequencies = np.fft.fftfreq(num_fft_bins * 2, 1.0 / SAMPLE_RATE)[:num_fft_bins]
    filterbank = LogarithmicFilterbank(
        bin_frequencies=bin_frequencies,
        num_bands=NUM_BANDS,
        fmin=30,
        fmax=17000,
        norm_filters=True
    )

    # filterbank is a 2D array (num_bins, num_bands)
    fb_array = np.asarray(filterbank)
    print(f"  Filterbank shape: {fb_array.shape}")  # Should be (705, 136)

    num_bins = fb_array.shape[0]
    num_bands = fb_array.shape[1]

    # Save as binary: [num_bins (int32), num_bands (int32), weights (float32 array)]
    binary_path = GOLDEN_DIR / "filterbank.bin"
    with open(binary_path, "wb") as f:
        np.array([num_bins], dtype=np.int32).tofile(f)
        np.array([num_bands], dtype=np.int32).tofile(f)
        fb_array.astype(np.float32).tofile(f)

    print(f"  Saved: {binary_path}")


def generate_mel_golden():
    """Generate mel spectrogram features using madmom, matching BeatNet's log_spect.py exactly."""
    print("\n=== Generating Mel Spectrogram Golden Files ===")

    try:
        import librosa
        from madmom.audio.signal import SignalProcessor, FramedSignalProcessor
        from madmom.audio.stft import ShortTimeFourierTransformProcessor
        from madmom.audio.spectrogram import (
            FilteredSpectrogramProcessor,
            LogarithmicSpectrogramProcessor,
            SpectrogramDifferenceProcessor,
        )
        from madmom.processors import SequentialProcessor
    except ImportError as e:
        print(f"  ERROR: {e}")
        print("  Install with: pip install librosa madmom")
        return

    # BeatNet configuration (from BeatNet.py)
    sample_rate = SAMPLE_RATE
    hop_size = HOP_SIZE
    win_length = FFT_SIZE
    num_bands = NUM_BANDS

    # Find test audio files - use all of them
    audio_files = sorted(AUDIO_DIR.glob("*.m4a"))

    if not audio_files:
        print("  No test audio files found")
        return

    # Also save raw audio for C++ tests
    raw_audio_dir = GOLDEN_DIR.parent / "audio"
    raw_audio_dir.mkdir(parents=True, exist_ok=True)

    for audio_file in audio_files:
        bpm_str = audio_file.stem.split("_")[0]
        print(f"  Processing: {audio_file.name}")

        try:
            # Load audio
            audio, sr = librosa.load(str(audio_file), sr=sample_rate, mono=True)

            # Save raw audio for C++ (float32 little-endian)
            raw_path = raw_audio_dir / f"{bpm_str}.raw"
            audio.astype(np.float32).tofile(raw_path)
            print(f"    Saved raw audio: {raw_path}")

            # Create exact BeatNet pipeline from log_spect.py
            sig = SignalProcessor(num_channels=1, sample_rate=sample_rate)
            frames = FramedSignalProcessor(frame_size=win_length, hop_size=hop_size)
            stft = ShortTimeFourierTransformProcessor()
            filt = FilteredSpectrogramProcessor(
                num_bands=num_bands, fmin=30, fmax=17000, norm_filters=True
            )
            spec = LogarithmicSpectrogramProcessor(mul=1, add=1)
            diff = SpectrogramDifferenceProcessor(
                diff_ratio=0.5, positive_diffs=True, stack_diffs=np.hstack
            )

            pipe = SequentialProcessor([sig, frames, stft, filt, spec, diff])
            features = pipe(audio)

            # Save first 100 frames for thorough comparison
            num_frames = min(100, len(features))

            # Save features as binary (float32) for easy C++ loading
            # Format: [num_frames (int32), feature_dim (int32), features (float32 array)]
            binary_path = GOLDEN_DIR / f"mel_golden_{bpm_str}bpm.bin"
            with open(binary_path, "wb") as bf:
                np.array([num_frames], dtype=np.int32).tofile(bf)
                np.array([features.shape[1]], dtype=np.int32).tofile(bf)
                features[:num_frames].astype(np.float32).tofile(bf)
            print(f"    Saved binary features: {binary_path}")

            data = {
                "description": f"Mel spectrogram golden from madmom for {audio_file.name}",
                "audio_file": audio_file.name,
                "raw_audio_file": f"{bpm_str}.raw",
                "binary_features_file": f"mel_golden_{bpm_str}bpm.bin",
                "config": {
                    "sample_rate": sample_rate,
                    "hop_size": hop_size,
                    "win_length": win_length,
                    "bands_per_octave": num_bands,
                    "fmin": 30,
                    "fmax": 17000,
                },
                "feature_dim": int(features.shape[1]) if len(features) > 0 else 0,
                "total_frames": len(features),
                "frames": [
                    {
                        "index": i,
                        "features": features[i].tolist(),
                    }
                    for i in range(num_frames)
                ],
            }

            save_json(data, f"mel_golden_{bpm_str}bpm.json")

        except Exception as e:
            import traceback
            print(f"    Error: {e}")
            traceback.print_exc()


def generate_onnx_golden():
    """Generate ONNX model outputs for test audio files."""
    print("\n=== Generating ONNX Model Golden Files ===")

    try:
        import torch
        import onnxruntime as ort
    except ImportError as e:
        print(f"  ERROR: {e}")
        print("  Install with: pip install torch onnxruntime")
        return

    # Find the ONNX model
    model_path = PROJECT_ROOT / "apps/native/assets/models/beatnet_model_2.onnx"
    if not model_path.exists():
        print(f"  ERROR: Model not found at {model_path}")
        return

    print(f"  Loading model: {model_path}")

    # Create ONNX Runtime session
    session = ort.InferenceSession(str(model_path))

    # Get input/output names
    input_names = [i.name for i in session.get_inputs()]
    output_names = [o.name for o in session.get_outputs()]
    print(f"  Inputs: {input_names}")
    print(f"  Outputs: {output_names}")

    # Process each mel golden file
    mel_files = sorted(GOLDEN_DIR.glob("mel_golden_*bpm.bin"))

    for mel_file in mel_files:
        bpm_str = mel_file.stem.replace("mel_golden_", "").replace("bpm", "")
        print(f"  Processing: {mel_file.name}")

        # Load mel features
        with open(mel_file, "rb") as f:
            num_frames = np.frombuffer(f.read(4), dtype=np.int32)[0]
            feature_dim = np.frombuffer(f.read(4), dtype=np.int32)[0]
            features = np.frombuffer(f.read(), dtype=np.float32).reshape(num_frames, feature_dim)

        print(f"    Loaded {num_frames} frames, {feature_dim} features")

        # Initialize LSTM hidden state
        hidden = np.zeros((2, 1, 150), dtype=np.float32)
        cell = np.zeros((2, 1, 150), dtype=np.float32)

        # Run inference frame by frame
        activations = []
        for i in range(num_frames):
            # Prepare input: [batch=1, seq=1, features=272]
            frame_input = features[i:i+1, :].reshape(1, 1, feature_dim).astype(np.float32)

            # Run inference
            outputs = session.run(
                output_names,
                {
                    "input": frame_input,
                    "hidden_in": hidden,
                    "cell_in": cell,
                }
            )

            # outputs[0] = model output [1, 1, 3]
            # outputs[1] = hidden_out [2, 1, 150]
            # outputs[2] = cell_out [2, 1, 150]
            output = outputs[0].reshape(3)
            hidden = outputs[1]
            cell = outputs[2]

            # Apply softmax if not already normalized
            if abs(output.sum() - 1.0) > 0.01:
                exp_output = np.exp(output - output.max())
                output = exp_output / exp_output.sum()

            # Output order: [beat, downbeat, non-beat]
            beat_activation = float(output[0])
            downbeat_activation = float(output[1])

            activations.append([beat_activation, downbeat_activation])

        activations = np.array(activations, dtype=np.float32)

        # Save as binary: [num_frames (int32), activations (float32 array)]
        binary_path = GOLDEN_DIR / f"onnx_activations_{bpm_str}bpm.bin"
        with open(binary_path, "wb") as f:
            np.array([num_frames], dtype=np.int32).tofile(f)
            activations.tofile(f)

        print(f"    Saved: {binary_path}")
        print(f"    Beat activation range: [{activations[:, 0].min():.4f}, {activations[:, 0].max():.4f}]")
        print(f"    Downbeat activation range: [{activations[:, 1].min():.4f}, {activations[:, 1].max():.4f}]")


def generate_e2e_golden():
    """Generate end-to-end golden files: audio → features → activations → BPM."""
    print("\n=== Generating E2E Golden Files ===")

    try:
        import torch
        import onnxruntime as ort
        import librosa
        from madmom.audio.signal import SignalProcessor, FramedSignalProcessor
        from madmom.audio.stft import ShortTimeFourierTransformProcessor
        from madmom.audio.spectrogram import (
            FilteredSpectrogramProcessor,
            LogarithmicSpectrogramProcessor,
            SpectrogramDifferenceProcessor,
        )
        from madmom.processors import SequentialProcessor
        from BeatNet.particle_filtering_cascade import particle_filter_cascade
    except ImportError as e:
        print(f"  ERROR: {e}")
        return

    # Find the ONNX model
    model_path = PROJECT_ROOT / "apps/native/assets/models/beatnet_model_2.onnx"
    if not model_path.exists():
        print(f"  ERROR: Model not found at {model_path}")
        return

    # Create ONNX session
    session = ort.InferenceSession(str(model_path))
    input_names = [i.name for i in session.get_inputs()]
    output_names = [o.name for o in session.get_outputs()]

    # Create mel pipeline
    sig = SignalProcessor(num_channels=1, sample_rate=SAMPLE_RATE)
    frames = FramedSignalProcessor(frame_size=FFT_SIZE, hop_size=HOP_SIZE)
    stft = ShortTimeFourierTransformProcessor()
    filt = FilteredSpectrogramProcessor(num_bands=NUM_BANDS, fmin=30, fmax=17000, norm_filters=True)
    spec = LogarithmicSpectrogramProcessor(mul=1, add=1)
    diff = SpectrogramDifferenceProcessor(diff_ratio=0.5, positive_diffs=True, stack_diffs=np.hstack)
    mel_pipeline = SequentialProcessor([sig, frames, stft, filt, spec, diff])

    # Process each audio file
    audio_files = sorted(AUDIO_DIR.glob("*.m4a"))
    e2e_results = {}

    for audio_file in audio_files:
        bpm_str = audio_file.stem.split("_")[0]
        expected_bpm = float(bpm_str)
        print(f"  Processing: {audio_file.name} (expected BPM: {expected_bpm})")

        try:
            # Load audio
            audio, sr = librosa.load(str(audio_file), sr=SAMPLE_RATE, mono=True)

            # Extract mel features
            features = mel_pipeline(audio)
            num_frames = len(features)
            print(f"    Extracted {num_frames} mel frames")

            # Run ONNX inference
            hidden = np.zeros((2, 1, 150), dtype=np.float32)
            cell = np.zeros((2, 1, 150), dtype=np.float32)
            activations = []

            for i in range(num_frames):
                frame_input = features[i:i+1, :].reshape(1, 1, 272).astype(np.float32)
                outputs = session.run(
                    output_names,
                    {"input": frame_input, "hidden_in": hidden, "cell_in": cell}
                )
                output = outputs[0].reshape(3)
                hidden = outputs[1]
                cell = outputs[2]

                if abs(output.sum() - 1.0) > 0.01:
                    exp_output = np.exp(output - output.max())
                    output = exp_output / exp_output.sum()

                activations.append([float(output[0]), float(output[1])])

            activations = np.array(activations)

            # Run particle filter
            np.random.seed(1)
            pf = particle_filter_cascade(
                particle_size=2500,
                down_particle_size=400,
                min_bpm=55.0,
                max_bpm=215.0,
                num_tempi=300,
                fps=50,
                plot=[],
                mode=None,
            )

            beats_detected = []
            for i, act in enumerate(activations):
                result = pf.process(np.array([act]))
                if result is not None and len(result) > 0:
                    beats_detected.append({
                        "frame": i,
                        "time": i / 50.0,
                        "type": int(result[0][1]) if len(result[0]) > 1 else 1
                    })

            # Get final BPM
            final_interval = np.median(pf.st.state_intervals[pf.particles])
            final_bpm = 60.0 * 50.0 / final_interval

            print(f"    Detected BPM: {final_bpm:.2f} (expected: {expected_bpm})")
            print(f"    Beats detected: {len(beats_detected)}")

            e2e_results[bpm_str] = {
                "audio_file": audio_file.name,
                "expected_bpm": expected_bpm,
                "detected_bpm": float(final_bpm),
                "bpm_error": abs(final_bpm - expected_bpm),
                "num_frames": num_frames,
                "num_beats": len(beats_detected),
                "beats": beats_detected[:50],  # First 50 beats
            }

        except Exception as e:
            import traceback
            print(f"    Error: {e}")
            traceback.print_exc()

    # Save e2e results
    save_json({
        "description": "End-to-end golden results: audio → mel → ONNX → particle filter → BPM",
        "config": {
            "sample_rate": SAMPLE_RATE,
            "hop_size": HOP_SIZE,
            "fft_size": FFT_SIZE,
            "fps": 50,
        },
        "results": e2e_results
    }, "e2e_golden.json")


def generate_particle_filter_golden():
    """Generate particle filter golden files from Python BeatNet."""
    print("\n=== Generating Particle Filter Golden Files ===")

    try:
        from BeatNet.particle_filtering_cascade import particle_filter_cascade
    except ImportError as e:
        print(f"  ERROR: {e}")
        print("  Make sure ~/dev/beatnet is accessible")
        return

    # CRITICAL: Match the seed used in C++ (config.randomSeed = 1)
    np.random.seed(1)

    # Create particle filter with same config as C++
    # Note: plot=[] avoids 'in' check on boolean
    pf = particle_filter_cascade(
        particle_size=2500,
        down_particle_size=400,
        min_bpm=55.0,
        max_bpm=215.0,
        num_tempi=300,
        fps=50,
        min_beats_per_bar=2,
        max_beats_per_bar=4,
        ig_threshold=0.4,
        lambda_b=100,
        lambda_d=0.1,
        observation_lambda_b="B56",
        observation_lambda_d="B56",
        plot=[],
        mode=None,
    )

    # Capture initial state
    initial_particles = pf.particles.copy().tolist()
    initial_down_particles = pf.down_particles.copy().tolist()

    # Test case 1: Constant 120 BPM activations
    # At 50 FPS, 120 BPM = 25 frames per beat
    fps = 50.0
    target_bpm = 120.0
    frames_per_beat = int(60.0 / target_bpm * fps)  # 25
    num_frames = 500  # 10 seconds

    activations = []
    for i in range(num_frames):
        phase = (i % frames_per_beat) / frames_per_beat
        is_beat = phase < 0.1
        is_downbeat = is_beat and (i % (frames_per_beat * 4)) < 3

        beat_act = 0.9 if is_beat else 0.1
        down_act = 0.85 if is_downbeat else (0.3 if is_beat else 0.1)
        activations.append([beat_act, down_act])

    activations_np = np.array(activations)

    # Process and capture frame-by-frame results
    # Reset seed again for reproducibility
    np.random.seed(1)
    pf2 = particle_filter_cascade(
        particle_size=2500,
        down_particle_size=400,
        min_bpm=55.0,
        max_bpm=215.0,
        num_tempi=300,
        fps=50,
        min_beats_per_bar=2,
        max_beats_per_bar=4,
        plot=[],
        mode=None,
    )

    frame_results = []
    for i, act in enumerate(activations):
        # Process single frame
        result = pf2.process(np.array([act]))

        # Capture state after this frame
        median_particle = np.median(pf2.st.state_positions[pf2.particles])
        median_interval = np.median(pf2.st.state_intervals[pf2.particles])
        bpm = 60.0 * fps / median_interval if median_interval > 0 else 0

        frame_results.append({
            "frame": i,
            "beat_activation": act[0],
            "downbeat_activation": act[1],
            "median_phase": float(median_particle),
            "median_interval": float(median_interval),
            "estimated_bpm": float(bpm),
            "beat_detected": result is not None and len(result) > 0,
        })

    # Get final BPM
    final_median_interval = np.median(pf2.st.state_intervals[pf2.particles])
    final_bpm = 60.0 * fps / final_median_interval

    data = {
        "description": "Particle filter golden from Python BeatNet",
        "random_seed": 1,
        "config": {
            "particle_size": 2500,
            "down_particle_size": 400,
            "min_bpm": 55.0,
            "max_bpm": 215.0,
            "num_tempi": 300,
            "fps": 50.0,
            "min_beats_per_bar": 2,
            "max_beats_per_bar": 4,
            "lambda_b": 100,
            "lambda_d": 0.1,
            "observation_lambda_b": "B56",
            "observation_lambda_d": "B56",
            "ig_threshold": 0.4,
        },
        "initial_state": {
            "beat_particles": initial_particles,
            "downbeat_particles": initial_down_particles,
        },
        "test_cases": {
            "constant_120bpm": {
                "target_bpm": target_bpm,
                "num_frames": num_frames,
                "activations": activations,
                "final_bpm": float(final_bpm),
                "frame_results": frame_results,
            }
        }
    }

    save_json(data, "particle_filter_golden.json")
    print(f"  Final BPM for 120 BPM input: {final_bpm:.2f}")


def main():
    print("=" * 60)
    print("Generating Golden Files for C++ Unit Tests")
    print("=" * 60)

    generate_fft_golden()
    generate_filterbank_golden()
    generate_mel_golden()
    generate_onnx_golden()
    generate_particle_filter_golden()
    generate_e2e_golden()

    print("\n" + "=" * 60)
    print("Done!")
    print(f"Golden files saved to: {GOLDEN_DIR}")
    print("=" * 60)


if __name__ == "__main__":
    main()
