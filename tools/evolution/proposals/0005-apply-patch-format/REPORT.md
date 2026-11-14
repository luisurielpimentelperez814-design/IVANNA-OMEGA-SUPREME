# Propuesta 0005 — aceptar el formato `*** Begin Patch` del LLM

- **Hallazgo:** el pool `ivanna-free` emite a menudo parches estilo
  `apply_patch` (`*** Begin Patch` / `*** Update File:`) aunque se le pida
  unified diff. El `extract_patch` original solo aceptaba unified diff,
  así que esos drafts se rechazaban ("el LLM no devolvió un diff
  utilizable") aunque el contenido fuera correcto.
- **Evidencia medida (2026-11-14):** 2 respuestas vacías (`*** End Patch`
  sin contenido) y 1 completa en un reintento de draft (1.2 s) para un
  cambio de 1 línea; el `run_fix.py draft` de un incidente sintético
  `dsp-sobrecarga` fue rechazado por lo mismo (63.4 s, ambos formatos
  fallidos, árbol intacto, sintético eliminado).
- **Reescritura (sin LLM: código del motor, escrito directo):**
  `llm_client._apply_patch_to_unified()` convierte `*** Update File:` /
  `*** Add File:` a unified diff ubicando cada hunk por coincidencia
  EXACTA de su contexto en el archivo actual; `extract_patch()` lo
  acepta como tercer formato; `draft_rewrite()` acepta `base_dir` y
  `evolution_engine.draft_patch()` pasa `REPO`. Fail-closed: contexto
  inexistente o ambiguo, archivo ausente o `*** Delete File:` → None.
  Test nuevo `tools/evolution/tests/test_llm_client_apply_patch.py`
  (4 tests: conversión + `git apply --check`, contexto ambiguo, archivo
  ausente/delete, add file).
- **Detalle:** el `new file mode 100644` es obligatorio en el diff
  generado para Add File: sin él, `git apply` falla con
  "dev/null: No such file or directory" (medido en repo recién creado).
- **Validación (scratch worktree, 30.8 s, 1ª pasada VERDE):**
  `validation.json`: applied True, python_checks unittest True (8 tests)
  + scan True, tree_untouched True, scratch eliminado True, sin
  directorios residuales.
- **Verificación en el árbol:** `python3 -m unittest discover` 8/8 OK;
  el texto del reintento (`/tmp/retry_raw.txt`) convierte a un unified
  diff válido con el hunk `@@ -1,3 +1,4 @@` correcto.
