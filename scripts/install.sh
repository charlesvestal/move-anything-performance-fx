#!/bin/bash
# Install Performance FX module to Move
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d "dist/performance-fx" ]; then
    echo "Error: dist/performance-fx not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing Performance FX Module ==="

# Deploy to Move - tools subdirectory
echo "Copying module to Move..."
ssh ableton@move.local "mkdir -p /data/UserData/move-anything/modules/tools/performance-fx"
scp -r dist/performance-fx/* ableton@move.local:/data/UserData/move-anything/modules/tools/performance-fx/

# Set permissions so Module Store can update later
echo "Setting permissions..."
ssh ableton@move.local "chmod -R a+rw /data/UserData/move-anything/modules/tools/performance-fx"

echo ""
echo "=== Install Complete ==="
echo "Module installed to: /data/UserData/move-anything/modules/tools/performance-fx/"
echo ""
echo "Restart Move Anything to load the new module."
