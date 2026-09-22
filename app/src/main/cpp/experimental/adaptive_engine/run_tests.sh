#!/usr/bin/env bash
# ============================================================================
# run_tests.sh — puerta de tests del flanco adaptive_engine (autocontenida)
# © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#
# Compila y corre los 3 suites del módulo con g++ de host (sin NDK, sin
# gtest — los tests usan solo la librería estándar). Pensada para correr
# ANTES de cada commit que toque adaptive_decision_engine.{hpp,cpp}.
#
# Uso:
#   bash run_tests.sh           # corrida normal (-O2)
#   IVANNA_SAN=asan bash run_tests.sh   # + AddressSanitizer/UBSan
#   IVANNA_SAN=tsan bash run_tests.sh   # + ThreadSanitizer (concurrencia)
#
# Nota de alcance: NO se engancha a app/src/main/cpp/tests/CMakeLists.txt
# porque ese archivo pertenece al flanco "Tests host (CTest)" — ver
# AGENT_CLAIMS.md. Esta puerta vive dentro del propio directorio del módulo
# para no pisar ese flanco.
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -Wall -Wextra -Wpedantic -pthread -O2"
SAN="${IVANNA_SAN:-}"
case "$SAN" in
    asan) CXXFLAGS="-std=c++17 -Wall -Wextra -pthread -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all" ;;
    tsan) CXXFLAGS="-std=c++17 -Wall -Wextra -pthread -O1 -g -fsanitize=thread" ;;
    "") ;;
    *) echo "IVANNA_SAN desconocido: '$SAN' (usar asan|tsan)"; exit 2 ;;
esac

TESTS=(test_adaptive_engine test_close_loop test_stability)
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "=== adaptive_engine — puerta de tests (san=${SAN:-ninguno}) ==="
for t in "${TESTS[@]}"; do
    echo "--- $t ---"
    "$CXX" $CXXFLAGS "tests/${t}.cpp" adaptive_decision_engine.cpp -o "$TMP/$t"
    timeout 300 "$TMP/$t"
done

echo "=== TODOS LOS SUITES PASARON (${#TESTS[@]}/3) ==="
