#!/bin/sh
# Build aarch64 del daemon ASRC. Ejecutar en el host de CI, no en el telefono.
set -e
CC=${CC:-aarch64-linux-gnu-g++}
$CC -O2 -std=c++17 -static -I"$(dirname "$0")" \
  "$(dirname "$0")"/asrc/ivanna_asrc_core.cpp \
  "$(dirname "$0")"/asrc/ivanna_asrc_ipc.cpp \
  "$(dirname "$0")"/asrcd/ivanna_servo.cpp \
  "$(dirname "$0")"/asrcd/ivanna_asrcd.cpp \
  -o "${1:-ivanna_asrcd}"
file "${1:-ivanna_asrcd}"
