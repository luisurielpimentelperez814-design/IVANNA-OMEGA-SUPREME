#!/usr/bin/env bash
# deploy_module1.sh — Módulo 1 (PredictiveAudioEngine): verifica, commitea y publica.
#
# Uso:
#   bash scripts/deploy_module1.sh            # verifica + commit local (sin push)
#   GH_TOKEN=... bash scripts/deploy_module1.sh --push   # además hace push a origin/main
#
# Seguridad: el token (si se usa) se pasa SOLO como cabecera HTTP efímera de ese
# `git push`: no se escribe en .git/config, ni en la URL del remoto, ni en disco.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

echo "[1/4] Test del módulo (host, Release)"
cmake -S app/src/main/cpp/tests -B build/tests-host -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/tests-host --target test_predictive_audio_engine -j"$(nproc)" >/dev/null
./build/tests-host/test_predictive_audio_engine | grep -E "^\[(medido|  PASSED|  FAILED)"

echo "[2/4] Puertas estáticas"
# SKIP_GATES="check_x.py ..." permite saltar puertas con fallo PREVIO ajeno a este
# cambio; cada salto se imprime. Por defecto todas son obligatorias.
for g in scripts/check_*.py; do
  if [[ " ${SKIP_GATES:-} " == *" $(basename "$g") "* ]]; then echo "SALTADA (declarada): $g"; continue; fi
  python3 "$g" >/dev/null || { echo "FALLA: $g"; exit 1; }
done

echo "[3/4] Commit"
git add app/src/main/cpp/predictive_audio_engine.hpp \
        app/src/main/cpp/predictive_audio_engine.cpp \
        app/src/main/cpp/tests/test_predictive_audio_engine.cpp \
        app/src/main/cpp/tests/CMakeLists.txt \
        app/src/main/cpp/CMakeLists.txt \
        telemetry/ctest_summary.json README.md "LÉAME.md" CHANGELOG.md \
        scripts/deploy_module1.sh
git commit -m "feat(predictive): pronosticador LPC 5-10 ms con validación en lazo cerrado" \
           -m "Burg orden<=32 + recursión en punto fijo entero; confidence = 1 - NMSE medido; onset por residuo. Analysis-only. Medido en host (11 pruebas, ASan/UBSan limpio); sin cablear a la ruta de audio ni probado en dispositivo. CTest 248 -> 259." \
           -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013MnaeHnKvr6uiHwL2UARdo"

if [[ "${1:-}" == "--push" ]]; then
  : "${GH_TOKEN:?define GH_TOKEN para hacer push}"
  echo "[4/4] Push a origin/main"
  B64="$(printf 'x-access-token:%s' "$GH_TOKEN" | base64 -w0)"
  git -c "http.extraheader=Authorization: Basic ${B64}" push origin main
else
  echo "[4/4] Push omitido (usa --push con GH_TOKEN)."
fi
