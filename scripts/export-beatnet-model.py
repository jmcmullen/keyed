#!/usr/bin/env python3
"""
Export BeatNet PyTorch models to ONNX format.

Usage:
    python scripts/export-beatnet-model.py [model_num]

Where model_num is 1, 2, or 3 (default: 2)
"""

import sys
import os

# Add BeatNet source to path
sys.path.insert(0, '/Users/j/Dev/beatnet/src')

import torch
import torch.nn as nn
import numpy as np

# Import BeatNet model
from BeatNet.model import BDA


class BDAWithState(nn.Module):
    """Wrapper that exposes LSTM state as explicit inputs/outputs for ONNX."""

    def __init__(self, model: BDA):
        super().__init__()
        self.model = model
        self.dim_hd = model.dim_hd
        self.num_layers = model.num_layers

    def forward(self, x, hidden_in, cell_in):
        # Set the LSTM state from inputs
        self.model.hidden = hidden_in
        self.model.cell = cell_in

        # Run forward pass
        out = self.model(x)

        # Apply softmax
        out_softmax = self.model.final_pred(out[0])

        # Return output and updated state
        return out_softmax, self.model.hidden, self.model.cell


def export_model(model_num: int, output_dir: str):
    """Export a BeatNet model to ONNX format."""

    device = "cpu"

    # Load the PyTorch model
    model = BDA(272, 150, 2, device)
    weights_path = f'/Users/j/Dev/beatnet/src/BeatNet/models/model_{model_num}_weights.pt'

    if not os.path.exists(weights_path):
        print(f"Error: Model weights not found at {weights_path}")
        return False

    model.load_state_dict(torch.load(weights_path, map_location=device), strict=False)
    model.eval()

    # Wrap model with explicit state handling
    wrapped_model = BDAWithState(model)
    wrapped_model.eval()

    # Create dummy inputs
    batch_size = 1
    seq_len = 1
    input_dim = 272
    hidden_dim = 150
    num_layers = 2

    dummy_input = torch.randn(batch_size, seq_len, input_dim)
    dummy_hidden = torch.zeros(num_layers, batch_size, hidden_dim)
    dummy_cell = torch.zeros(num_layers, batch_size, hidden_dim)

    # Output path
    output_path = os.path.join(output_dir, f'beatnet_model_{model_num}.onnx')

    print(f"Exporting model {model_num} to {output_path}...")

    # Export to ONNX (use legacy export to avoid onnxscript dependency)
    with torch.no_grad():
        torch.onnx.export(
            wrapped_model,
            (dummy_input, dummy_hidden, dummy_cell),
            output_path,
            input_names=['input', 'hidden_in', 'cell_in'],
            output_names=['output', 'hidden_out', 'cell_out'],
            dynamic_axes={
                'input': {0: 'batch', 1: 'seq_len'},
                'output': {0: 'batch', 2: 'seq_len'}
            },
            opset_version=11,
            do_constant_folding=True,
            export_params=True,
            verbose=False,
        )

    print(f"  ✓ Exported successfully")

    # Verify the export
    try:
        import onnx
        import onnxruntime as ort

        # Load and check ONNX model
        onnx_model = onnx.load(output_path)
        onnx.checker.check_model(onnx_model)
        print(f"  ✓ ONNX model validation passed")

        # Test with ONNX Runtime
        session = ort.InferenceSession(output_path)

        # Run inference
        ort_inputs = {
            'input': dummy_input.numpy(),
            'hidden_in': dummy_hidden.numpy(),
            'cell_in': dummy_cell.numpy(),
        }
        ort_outputs = session.run(None, ort_inputs)

        print(f"  ✓ ONNX Runtime inference passed")
        print(f"    Output shape: {ort_outputs[0].shape}")
        print(f"    Output sum: {ort_outputs[0].sum():.4f} (should be ~1.0 for softmax)")

        # Compare with PyTorch output
        with torch.no_grad():
            pt_output, pt_hidden, pt_cell = wrapped_model(dummy_input, dummy_hidden, dummy_cell)

        max_diff = np.abs(ort_outputs[0] - pt_output.numpy()).max()
        print(f"    Max diff from PyTorch: {max_diff:.2e}")

        if max_diff > 1e-5:
            print(f"  ⚠ Warning: Larger than expected difference from PyTorch")

    except ImportError:
        print("  ⚠ onnx/onnxruntime not available, skipping verification")

    return True


def main():
    # Default to model 2 (best performance)
    model_num = 2
    if len(sys.argv) > 1:
        try:
            model_num = int(sys.argv[1])
            if model_num not in [1, 2, 3]:
                raise ValueError()
        except ValueError:
            print(f"Error: model_num must be 1, 2, or 3")
            sys.exit(1)

    output_dir = '/Users/j/Dev/keyed/apps/native/assets/models'
    os.makedirs(output_dir, exist_ok=True)

    print(f"BeatNet ONNX Export")
    print(f"=" * 40)

    # Export requested model
    if not export_model(model_num, output_dir):
        sys.exit(1)

    # Also export all models if requested
    if len(sys.argv) > 2 and sys.argv[2] == '--all':
        for num in [1, 2, 3]:
            if num != model_num:
                export_model(num, output_dir)

    print(f"\n✓ Done!")


if __name__ == '__main__':
    main()
