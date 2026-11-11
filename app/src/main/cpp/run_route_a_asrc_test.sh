#!/bin/sh
# run_route_a_asrc_test.sh — Tests de host del ASRC magistral Ruta A.
# Compila el motor nativo + test con g++ de host (sin NEON: dot escalar)
# y corre las validaciones: (a) tasa, (b) THD+N, (c) continuidad entre
# bloques, (d) fail-closed, (e) FIR Ψ. Sin Android, sin NDK.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
HAL_PROBE="$DIR/../../../../hal_integration/moto_g85/pal_preload_probe"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
    -I"$DIR" -I"$HAL_PROBE" \
    "$DIR/ivanna_route_a_asrc.cpp" "$DIR/ivanna_route_a_asrc_test.cpp" \
    -o /tmp/route_a_asrc_test
/tmp/route_a_asrc_test
