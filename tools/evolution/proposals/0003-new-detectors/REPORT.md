# Propuesta 0003 — dos detectores nuevos (host_checks_red + todo_fixme)

- **Hallazgo:** el `--scan` del motor era ciego a los 6 chequeos de Fase 0
  (`scripts/check_*.py`) y a los TODO/FIXME del C++ nativo.
- **Evidencia medida (2026-11-14, HEAD 8ac65b9d):** los 6 chequeos fallan
  con exit 1 (~27 s en total): jni_wiring 12 hallazgos,
  kotlin_wrapper_wiring 13, header_wiring 20, rt_safety 6, docs_claims 18
  (README 978/978 vs telemetry 974/974), datasets_deploy 46 líneas
  `[FAIL]` (su propio resumen dice 239 hallazgos); TODO/FIXME: 28 en
  `app/src/main/cpp` (41 antes de excluir el googletest vendoreado).
- **Redacción:** LLM vía OmniRoute (modelo `ivanna-free`), formato
  código-plano (el formato ` ```diff ` devolvió dos veces un parche vacío
  `*** End Patch`; ver PIPELINE.md § lecciones). Integrada a mano con 3
  correcciones: el `error` del draft no se incluía en el resultado,
  `datasets_deploy` cuenta líneas `  [FAIL]` además de `  - ` (si no,
  sus hallazgos contarían como 0), y `scan_todo_fixme` excluye
  `tests/third_party` (sus TODOs son de googletest, no deuda de IVANNA).
- **Alcance:** solo `tools/evolution/evolution_engine.py`. Detectar no es
  arreglar: los fixes de `scripts/` quedan fuera del ámbito del motor.
- **Validación (scratch worktree, 3ª pasada, 27.2 s):** `validation.json`:
  applied True, full_suite saltada (parche sin C++, motivo registrado),
  python_checks unittest True (1 test) + scan True, tree_untouched True,
  scratch eliminado True, sin directorios residuales. Las 2 primeras
  pasadas fallaron por defectos del validador (no del parche): /tmp es un
  tmpfs de 512 MB y el repo pesa 1.5 GB; el scratch no veía los archivos
  sin seguir del motor; el tmpdir padre quedaba huérfano. Los 3 están
  corregidos en el validador (ver PIPELINE.md § lecciones).
