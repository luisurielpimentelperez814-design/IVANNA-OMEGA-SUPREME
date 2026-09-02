#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
cmake --preset host-release
cmake --build --preset host-release -j"${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --preset host-release
