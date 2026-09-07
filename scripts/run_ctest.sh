#!/usr/bin/env bash
# run_ctest.sh — puerta de tests nativos host (flanco Tests host/CTest).
#
# Historial verificado:
#   - Antes: invocaba `cmake --preset host-release` pero CMakePresets.json
#     NUNCA existió en la raíz → fallaba garantizado con
#     "Could not read presets ... File not found: CMakePresets.json".
#   - Ahora: configura directamente el proyecto de tests host
#     (app/src/main/cpp/tests — GoogleTest vendoreado, cero red) sin
#     depender de presets inexistentes. ASan/UBSan opcional via
#     IVANNA_SAN=1.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/app/src/main/cpp/tests"
BUILD="$ROOT/build/tests-host"

# build/ ya está en .gitignore; el directorio de tests no colisiona con el
# build del APK (que vive en app/build) ni con el del daemon.
GEN="${CMAKE_GENERATOR:-Unix Makefiles}"
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-2}"

SAN_FLAGS=(-DIVANNA_TEST_ENABLE_ASAN=OFF -DIVANNA_TEST_ENABLE_TSAN=OFF)
if [[ "${IVANNA_SAN:-0}" == "asan" ]]; then
  SAN_FLAGS=(-DIVANNA_TEST_ENABLE_ASAN=ON)
elif [[ "${IVANNA_SAN:-0}" == "tsan" ]]; then
  SAN_FLAGS=(-DIVANNA_TEST_ENABLE_TSAN=ON)
fi

cmake -S "$SRC" -B "$BUILD" -G "$GEN" "${SAN_FLAGS[@]}"
cmake --build "$BUILD" -j"$JOBS"
ctest --test-dir "$BUILD" --output-on-failure
