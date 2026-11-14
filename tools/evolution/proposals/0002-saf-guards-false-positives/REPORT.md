# Propuesta 0002 — scan_saf_guards: falsos positivos

- **Hallazgo:** el detector `scan_saf_guards` listaba como "sin guardas"
  archivos sin ninguna función detectada.
- **Evidencia medida (2026-11-14, HEAD 8ac65b9d, `--scan` 7.5 s):** 3 hits,
  2 falsos positivos con `"functions": []`:
  `app/src/main/cpp/jni/saf_stimulus_jni.cpp` (solo `JNIEXPORT jboolean`,
  el regex no casa ninguna función) y `app/src/main/cpp/saf_runtime.cpp`
  (su guarda vive en el header hermano `include/saf_runtime.h`, donde está
  `SAFUpdate`, el patrón de referencia). El único hit real
  (`saf_latent_bridge.cpp`) listaba `ivanna_saf_apply_latent` duplicado
  (declaración + definición).
- **Reescritura:** LLM vía OmniRoute (modelo `ivanna-free`, 5.0 s, formato
  ` ```diff `). Integrada a mano con 2 correcciones: el draft ponía
  `header_checked` con la última ruta candidata aunque no existiera ningún
  header (su propio test lo contradecía: esperaba null), y el test
  importaba `tools.evolution.evolution_engine` (falla con discover porque
  la raíz no está en `sys.path` y `tools/` no es un paquete). Reglas nuevas:
  sin funciones no hay hit; dedup preservando orden; guarda en el header
  hermano (`include/<base>.h`, `<base>.h`, `<base>.hpp`); trazabilidad
  `guard_source` / `header_checked`.
- **Verificación (sin el validador scratch, que aún no existía):**
  `python3 -m unittest discover -s tools/evolution/tests -v` → 1 test OK;
  `--scan` antes: 3 hits (2 con `functions: []`); después: 1 hit
  (`saf_latent_bridge.cpp`, `ivanna_saf_apply_latent` una sola vez,
  `guard_source: none`, `header_checked: null`).
- **Parche:** reconstruido con `diff -u` entre la base pre-0002 y el
  archivo integrado (el diff del LLM traía hunks `@@ -1,0 +1,0 @@`
  inválidos; el código era correcto, el framing no).
