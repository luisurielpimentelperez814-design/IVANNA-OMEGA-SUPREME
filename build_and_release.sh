#!/usr/bin/env bash
# ==============================================================================
# IVANNA-FUSION v2.0 ARMv8 DSP KERNEL - Termux Automated Build & Release Script
# ==============================================================================
set -e

echo "============================================================"
echo " [IVANNA-FUSION v2.0] Initiating Termux Build Sequence..."
echo "============================================================"

# Create target directories
BUILD_DIR="build"
RELEASE_DIR="release_pkg"
ZIP_NAME="IVANNA_FUSION_RELEASE.zip"

rm -rf "$BUILD_DIR" "$RELEASE_DIR" "$ZIP_NAME"
mkdir -p "$BUILD_DIR"
mkdir -p "$RELEASE_DIR"

cd "$BUILD_DIR"

echo "[1/4] Generating CMake build configuration..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a76 -march=armv8.2-a+simd+fp16 -fno-rtti -fno-exceptions -ffast-math -ftree-vectorize -fomit-frame-pointer -flto"

echo "[2/4] Compiling IVANNA-FUSION C++ DSP Kernel using nproc..."
CORES=$(nproc 2>/dev/null || echo 4)
make -j"$CORES"

echo "[3/4] Applying binary stripping to minimize file size..."
if command -v strip >/dev/null 2>&1; then
    strip ivanna_fusion
elif command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip ivanna_fusion
fi

echo "[4/4] Packaging binary release artifact..."
cd ..
cp "$BUILD_DIR/ivanna_fusion" "$RELEASE_DIR/"
cp *.hpp "$RELEASE_DIR/" 2>/dev/null || true
cp CMakeLists.txt "$RELEASE_DIR/"

if command -v zip >/dev/null 2>&1; then
    zip -r "$ZIP_NAME" "$RELEASE_DIR"
    echo "============================================================"
    echo " SUCCESS: Release artifact created at: $ZIP_NAME"
    echo " Size: $(du -h "$ZIP_NAME" | cut -f1)"
    echo "============================================================"
else
    tar -czvf "IVANNA_FUSION_RELEASE.tar.gz" -C "$RELEASE_DIR" .
    echo "============================================================"
    echo " SUCCESS: Release artifact created at: IVANNA_FUSION_RELEASE.tar.gz"
    echo " Size: $(du -h "IVANNA_FUSION_RELEASE.tar.gz" | cut -f1)"
    echo "============================================================"
fi
