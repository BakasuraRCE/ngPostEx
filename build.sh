#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== ngPost Local Build ==="
echo "Build directory: $BUILD_DIR"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Running qmake..."
qmake "$SCRIPT_DIR/src/ngPost.pro"

echo "Compiling with $(nproc) threads..."
make -j$(nproc)

echo ""
echo "=== Build complete ==="
echo "Binary: $BUILD_DIR/ngPostEx"
