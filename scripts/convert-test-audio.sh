#!/bin/bash
#
# Convert test audio files to raw PCM format for C++ tests
# Output: 16-bit signed, mono, 22050 Hz, little-endian
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_DIR="$SCRIPT_DIR/../apps/native/assets/test"
OUTPUT_DIR="$SCRIPT_DIR/../packages/engine/tests/audio"

mkdir -p "$OUTPUT_DIR"

echo "Converting test audio files to raw PCM..."
echo "Input: $INPUT_DIR"
echo "Output: $OUTPUT_DIR"
echo ""

for f in "$INPUT_DIR"/*.m4a; do
    if [ -f "$f" ]; then
        base=$(basename "$f" .m4a)
        output="$OUTPUT_DIR/${base}.raw"

        echo "Converting: $base.m4a -> $base.raw"
        ffmpeg -y -i "$f" \
            -f s16le \
            -acodec pcm_s16le \
            -ar 22050 \
            -ac 1 \
            "$output" \
            2>/dev/null
    fi
done

# Also handle .aiff files
for f in "$INPUT_DIR"/*.aiff; do
    if [ -f "$f" ]; then
        base=$(basename "$f" .aiff)
        output="$OUTPUT_DIR/${base}.raw"

        echo "Converting: $base.aiff -> $base.raw"
        ffmpeg -y -i "$f" \
            -f s16le \
            -acodec pcm_s16le \
            -ar 22050 \
            -ac 1 \
            "$output" \
            2>/dev/null
    fi
done

echo ""
echo "Done! Converted files:"
ls -lh "$OUTPUT_DIR"/*.raw 2>/dev/null || echo "No files converted"
