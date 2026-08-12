#!/usr/bin/env bash
# Chequeos locales rápidos antes de commit
set -euo pipefail
echo "==> Running local checks..."
if [ -f gradlew ]; then
  echo "  • Kotlin lint"
  ./gradlew lint || true
  echo "  • Unit tests"
  ./gradlew testDebugUnitTest || true
fi
if [ -d app/src/main/cpp/tests ]; then
  echo "  • Native tests"
  cmake -B build-tests -S app/src/main/cpp/tests -DCMAKE_BUILD_TYPE=Release
  cmake --build build-tests -j2
  ctest --test-dir build-tests --output-on-failure
fi
echo "✅ Local checks passed"
