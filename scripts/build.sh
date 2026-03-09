#!/usr/bin/env bash
# Build Performance FX module for Move Anything (ARM64)
#
# Automatically uses Docker for cross-compilation if needed.
# Set CROSS_PREFIX to skip Docker (e.g., for native ARM builds).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="move-anything-builder"

# Check if we need Docker
if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Performance FX Module Build (via Docker) ==="
    echo ""

    # Build Docker image if needed
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
        echo ""
    fi

    # Run build inside container
    echo "Running build..."
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh

    echo ""
    echo "=== Done ==="
    exit 0
fi

# === Actual build (runs in Docker or with cross-compiler) ===
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"

cd "$REPO_ROOT"

echo "=== Building Performance FX Module ==="
echo "Cross prefix: $CROSS_PREFIX"

# Create build directories
mkdir -p build/bungee
mkdir -p dist/performance-fx

BUNGEE_DIR="src/dsp/bungee"

# Step 1: Build PFFFT (FFT library)
echo "Building PFFFT..."
${CROSS_PREFIX}gcc -O3 -fPIC -ffast-math -fno-finite-math-only \
    -c ${BUNGEE_DIR}/submodules/pffft/pffft.c -o build/bungee/pffft.o
${CROSS_PREFIX}gcc -O3 -fPIC -ffast-math -fno-finite-math-only \
    -c ${BUNGEE_DIR}/submodules/pffft/fftpack.c -o build/bungee/fftpack.o

# Step 2: Build Bungee library (C++20)
echo "Building Bungee..."
for src in ${BUNGEE_DIR}/src/*.cpp; do
    obj="build/bungee/$(basename "$src" .cpp).o"
    ${CROSS_PREFIX}g++ -O3 -fPIC -std=c++20 -fwrapv \
        -I"${BUNGEE_DIR}/submodules/eigen" \
        -I"${BUNGEE_DIR}/submodules" \
        -I"${BUNGEE_DIR}" \
        '-DBUNGEE_VISIBILITY=__attribute__((visibility("default")))' \
        -DBUNGEE_SELF_TEST=0 \
        -Deigen_assert=BUNGEE_ASSERT1 \
        -DEIGEN_DONT_PARALLELIZE=1 \
        '-DBUNGEE_VERSION="0.0.0"' \
        -c "$src" -o "$obj"
done

# Step 3: Create static archive
echo "Creating Bungee static library..."
${CROSS_PREFIX}ar rcs build/bungee/libbungee.a build/bungee/*.o

# Step 4: Build the C++ bungee wrapper
echo "Compiling Bungee wrapper..."
${CROSS_PREFIX}g++ -O3 -fPIC -std=c++20 \
    -I"${BUNGEE_DIR}" \
    -Isrc/dsp \
    -c src/dsp/pfx_bungee.cpp -o build/pfx_bungee.o

# Step 5: Compile DSP plugin (C) and link everything
echo "Compiling DSP plugin..."
${CROSS_PREFIX}gcc -Ofast -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -fomit-frame-pointer -fno-stack-protector \
    -DNDEBUG \
    -c src/dsp/perf_fx_plugin.c -o build/perf_fx_plugin.o \
    -Isrc/dsp
${CROSS_PREFIX}gcc -Ofast -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -fomit-frame-pointer -fno-stack-protector \
    -DNDEBUG \
    -c src/dsp/perf_fx_dsp.c -o build/perf_fx_dsp.o \
    -Isrc/dsp

# Step 6: Link final shared library (use g++ for C++ runtime)
echo "Linking..."
${CROSS_PREFIX}g++ -shared -o build/dsp.so \
    build/perf_fx_plugin.o \
    build/perf_fx_dsp.o \
    build/pfx_bungee.o \
    build/bungee/libbungee.a \
    -lm -lrt

# Copy files to dist (use cat to avoid ExtFS deallocation issues with Docker)
echo "Packaging..."
cat src/module.json > dist/performance-fx/module.json
cat src/ui.js > dist/performance-fx/ui.js
[ -f src/help.json ] && cat src/help.json > dist/performance-fx/help.json
cat build/dsp.so > dist/performance-fx/dsp.so
chmod +x dist/performance-fx/dsp.so
[ -f src/assets/vinyl_crackle.wav ] && cat src/assets/vinyl_crackle.wav > dist/performance-fx/vinyl_crackle.wav

# Create tarball for release
cd dist
tar -czvf performance-fx-module.tar.gz performance-fx/
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output: dist/performance-fx/"
echo "Tarball: dist/performance-fx-module.tar.gz"
echo ""
echo "To install on Move:"
echo "  ./scripts/install.sh"
