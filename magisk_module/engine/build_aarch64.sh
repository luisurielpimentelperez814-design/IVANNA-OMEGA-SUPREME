#!/bin/sh
# Build aarch64 del daemon ASRC. Ejecutar en el host de CI, no en el telefono.
# v2.5.397: el núcleo es el resampleador de Ruta A (asrc/routea_core.cpp, FIR
# 319 taps, tabla fir_v5 de hal_integration/) + FIFO de bloque fijo en
# asrc/ivanna_asrc_core.cpp. El core Genspark queda como legacy (no se compila).
set -e
CC=${CC:-aarch64-linux-gnu-g++}
ENG="$(dirname "$0")"
$CC -O2 -std=c++17 -static -I"$ENG" \
  -I"$ENG/../../hal_integration/moto_g85/pal_preload_probe" \
  "$ENG"/asrc/ivanna_asrc_core.cpp \
  "$ENG"/asrc/routea_core.cpp \
  "$ENG"/asrc/ivanna_asrc_ipc.cpp \
  "$ENG"/asrcd/ivanna_servo.cpp \
  "$ENG"/asrcd/ivanna_asrcd.cpp \
  -o "${1:-ivanna_asrcd}"
file "${1:-ivanna_asrcd}"
