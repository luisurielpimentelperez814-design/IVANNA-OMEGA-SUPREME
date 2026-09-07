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

cmake -S "$SRC" -B "$BUILD" -G "$GEN" -DCMAKE_BUILD_TYPE=Release "${SAN_FLAGS[@]}"
cmake --build "$BUILD" -j"$JOBS"
# Paralelismo de EJECUCIÓN (no solo de compilación): verificado sin
# colisiones a -j4 y -j8 (test_shm_lifecycle usa backing file propio en /tmp).
# TIMEOUT_PER_TEST: un test colgado hoy devoraría el timeout del job entero
# sin decir cuál fue; con 300 s por test, ctest mata al culpable y lo nombra.
# El más lento en la práctica: test_control_frame_bus_stress (~15 s normal,
# minutos bajo TSan — por eso el default escala con IVANNA_SAN).
CTEST_JOBS="${CTEST_JOBS:-4}"
if [[ -z "${TIMEOUT_PER_TEST:-}" ]]; then
  if [[ "${IVANNA_SAN:-0}" == "tsan" ]]; then TIMEOUT_PER_TEST=900; else TIMEOUT_PER_TEST=300; fi
fi
ctest --test-dir "$BUILD" --output-on-failure -j"$CTEST_JOBS" --timeout "$TIMEOUT_PER_TEST"
