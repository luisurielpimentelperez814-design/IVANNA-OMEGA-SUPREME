#!/usr/bin/env bash
# setup-hooks.sh — conecta .githooks/pre-commit (flanco Supply chain + hooks).
#
# Historial verificado: .githooks/pre-commit existía (ejecuta la puerta
# run_ctest.sh — 74 tests del DSP en ~15 s) pero core.hooksPath NUNCA estuvo
# configurado → el hook no corría para nadie: cada commit podía romper la
# puerta sin que el autor se enterara hasta el push.
#
# Uso: bash scripts/setup-hooks.sh   (idempotente — seguro re-ejecutar)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOOK="$ROOT/.githooks/pre-commit"

[ -f "$HOOK" ] || { echo "::error::no existe $HOOK"; exit 1; }
[ -x "$HOOK" ] || chmod +x "$HOOK"
bash -n "$HOOK" || { echo "::error::hook con sintaxis inválida"; exit 1; }

git -C "$ROOT" config core.hooksPath .githooks
echo "OK: core.hooksPath = .githooks — el pre-commit corre la puerta de tests host en cada commit."
echo "Para saltarla puntualmente (raro, se documenta en el commit): git commit --no-verify"
