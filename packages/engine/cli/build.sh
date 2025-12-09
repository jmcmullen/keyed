#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "Building BeatNet CLI..."
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure and build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel

echo ""
echo "Build complete!"
echo ""
echo "Usage:"
echo "  cd $BUILD_DIR"
echo "  ./beatnet_cli    # Live microphone BPM detection"
echo "  ./batch_test     # Test accuracy on test-data/"
echo ""
