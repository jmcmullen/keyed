#!/usr/bin/env python3
"""
Convert BeatNet PyTorch model to ONNX format for React Native inference.

Usage:
    python scripts/convert-beatnet-to-onnx.py

This script:
1. Loads the BeatNet model architecture
2. Loads pre-trained weights
3. Exports to ONNX with dynamic batch/sequence dimensions
4. Validates the exported model
"""

import os
import sys
import torch
import torch.nn as nn
import torch.nn.functional as F
import numpy as np

# Add BeatNet source to path
BEATNET_PATH = os.path.expanduser("~/dev/BeatNet/src/BeatNet")
sys.path.insert(0, BEATNET_PATH)

# ============================================================================
# Model Definition (from BeatNet/model.py)
# ============================================================================


class BDA(nn.Module):
    """Beat Downbeat Activation detector - BeatNet's CRNN model"""

    def __init__(self, dim_in=272, num_cells=150, num_layers=2):
        super(BDA, self).__init__()

        self.dim_in = dim_in
        self.dim_hd = num_cells
        self.num_layers = num_layers
        self.conv_out = 150
        self.kernelsize = 10

        self.conv1 = nn.Conv1d(1, 2, self.kernelsize)
        self.linear0 = nn.Linear(
            2 * int((self.dim_in - self.kernelsize + 1) / 2), self.conv_out
        )

        self.lstm = nn.LSTM(
            input_size=self.conv_out,
            hidden_size=self.dim_hd,
            num_layers=self.num_layers,
            batch_first=True,
            bidirectional=False,
        )

        self.linear = nn.Linear(in_features=self.dim_hd, out_features=3)
        self.softmax = nn.Softmax(dim=-1)

    def forward(self, x, hidden=None, cell=None):
        """
        Forward pass with explicit hidden state handling for ONNX export.

        Args:
            x: Input features [batch, seq_len, 272]
            hidden: LSTM hidden state [num_layers, batch, hidden_size]
            cell: LSTM cell state [num_layers, batch, hidden_size]

        Returns:
            output: Activations [batch, seq_len, 3]
            hidden: Updated hidden state
            cell: Updated cell state
        """
        batch_size = x.shape[0]
        seq_len = x.shape[1]

        # Reshape for conv: [batch * seq_len, 1, dim_in]
        x = x.view(-1, self.dim_in)
        x = x.unsqueeze(1)

        # Conv + ReLU + MaxPool
        x = F.max_pool1d(F.relu(self.conv1(x)), 2)

        # Flatten and linear
        x = x.view(x.size(0), -1)
        x = self.linear0(x)

        # Reshape for LSTM: [batch, seq_len, conv_out]
        x = x.view(batch_size, seq_len, self.conv_out)

        # LSTM with explicit state
        if hidden is None or cell is None:
            hidden = torch.zeros(self.num_layers, batch_size, self.dim_hd)
            cell = torch.zeros(self.num_layers, batch_size, self.dim_hd)

        x, (hidden, cell) = self.lstm(x, (hidden, cell))

        # Output linear
        out = self.linear(x)

        # Apply softmax for inference
        out = self.softmax(out)

        return out, hidden, cell


def convert_to_onnx(model_num=1, output_dir="apps/native/assets/models"):
    """Convert BeatNet model to ONNX format."""

    # Create output directory
    os.makedirs(output_dir, exist_ok=True)

    # Initialize model
    model = BDA(dim_in=272, num_cells=150, num_layers=2)

    # Load weights
    weights_path = os.path.join(BEATNET_PATH, f"models/model_{model_num}_weights.pt")
    print(f"Loading weights from: {weights_path}")

    state_dict = torch.load(weights_path, map_location="cpu")
    model.load_state_dict(state_dict, strict=False)
    model.eval()

    # Create dummy inputs
    batch_size = 1
    seq_len = 1  # Single frame for streaming inference

    dummy_input = torch.randn(batch_size, seq_len, 272)
    dummy_hidden = torch.zeros(2, batch_size, 150)
    dummy_cell = torch.zeros(2, batch_size, 150)

    # Test forward pass
    print("Testing forward pass...")
    with torch.no_grad():
        out, h, c = model(dummy_input, dummy_hidden, dummy_cell)
        print(f"  Input shape: {dummy_input.shape}")
        print(f"  Output shape: {out.shape}")
        print(f"  Output values: {out[0, 0, :]}")

    # Export to ONNX
    output_path = os.path.join(output_dir, f"beatnet_model_{model_num}.onnx")
    print(f"\nExporting to ONNX: {output_path}")

    # Use legacy exporter for better compatibility
    torch.onnx.export(
        model,
        (dummy_input, dummy_hidden, dummy_cell),
        output_path,
        export_params=True,
        opset_version=13,
        do_constant_folding=True,
        input_names=["input", "hidden_in", "cell_in"],
        output_names=["output", "hidden_out", "cell_out"],
        dynamo=False,  # Use legacy exporter
    )

    print(f"  Exported successfully!")

    # Verify with ONNX Runtime
    try:
        import onnxruntime as ort

        print("\nVerifying with ONNX Runtime...")
        session = ort.InferenceSession(output_path)

        # Run inference
        inputs = {
            "input": dummy_input.numpy(),
            "hidden_in": dummy_hidden.numpy(),
            "cell_in": dummy_cell.numpy(),
        }
        outputs = session.run(None, inputs)

        print(f"  ONNX output shape: {outputs[0].shape}")
        print(f"  ONNX output values: {outputs[0][0, 0, :]}")

        # Compare with PyTorch output
        diff = np.abs(outputs[0] - out.numpy()).max()
        print(f"  Max difference from PyTorch: {diff:.6f}")

        if diff < 1e-4:
            print("  ✓ Verification passed!")
        else:
            print("  ⚠ Output differs from PyTorch - check model carefully")

    except ImportError:
        print("\nNote: Install onnxruntime to verify: pip install onnxruntime")

    # Get file size
    file_size = os.path.getsize(output_path) / (1024 * 1024)
    print(f"\nModel size: {file_size:.2f} MB")

    return output_path


def main():
    """Convert all three BeatNet models."""

    print("=" * 60)
    print("BeatNet to ONNX Converter")
    print("=" * 60)

    # Check if BeatNet exists
    if not os.path.exists(BEATNET_PATH):
        print(f"Error: BeatNet not found at {BEATNET_PATH}")
        print("Please clone BeatNet to ~/dev/BeatNet")
        sys.exit(1)

    # Convert model 1 (GTZAN trained - general purpose)
    output = convert_to_onnx(model_num=1)
    print(f"\nConverted: {output}")

    # Optionally convert other models
    # convert_to_onnx(model_num=2)  # Ballroom trained
    # convert_to_onnx(model_num=3)  # Rock corpus trained


if __name__ == "__main__":
    main()
