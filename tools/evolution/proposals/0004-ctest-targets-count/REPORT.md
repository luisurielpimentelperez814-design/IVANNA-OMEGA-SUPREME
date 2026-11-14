# Propuesta 0004 — repo_state() subestima los targets CTest (~3x)

- **Hallazgo:** `repo_state()` contaba `len(re.findall(r"add_test\(NAME", ...))`
  = 36, pero los tests se registran sobre todo con el helper
  `ivanna_add_test(name)` (68 invocaciones, usa `gtest_discover_tests`)
  más 36 `add_test(NAME` directos: total real ~104, no 36. El campo
  `ctest_targets` de `knowledge.json` subestimaba ~3x.
- **Evidencia medida (2026-11-14, HEAD 8ac65b9d):** `grep -c` sobre
  `app/src/main/cpp/tests/CMakeLists.txt`: `ivanna_add_test(` 69 (1 es la
  definición de la función en la línea 75), `add_test(NAME` 36.
- **Redacción:** ciclo `--evolve` completo con el LLM vía OmniRoute
  (modelo `ivanna-free`, latencia de redacción 79.6 s).
- **1ª pasada: ROJO (el gate funcionó).** El draft del LLM traía un diff
  corrupto (`git apply --check`: "corrupt patch at line 9" — un hunk
  `@@ -451,3 +531,90 @@` truncado que tragaba el diff del test nuevo) y
  3 defectos de código que su propio test no habría cazado: contaba el
  nombre fantasma `name` de la línea `function(ivanna_add_test name)`;
  el test usaba funciones sueltas estilo pytest (que `unittest discover`
  no recoge: 0 tests); y cargaba el motor con `parents[2]` (eso es
  `tools/`, no la raíz). El validador rechazó el parche, el árbol quedó
  intacto y el veredicto fue ROJO — no se publicó. Es la prueba de que el
  fail-closed funciona. (El REPORT.md original de esa pasada queda en el
  historial del directorio; este documento lo sustituye con el ciclo
  completo. El `validation.json` actual corresponde a la 3ª pasada.)
- **Reparación e integración a mano:** contador con exclusión de la línea
  `function(` + comentarios, test reescrito como `unittest.TestCase` con
  el patrón de importación de la 0002, docstrings en español, sin `shlex`.
  El test quedó en `tools/evolution/tests/test_count_ctest_targets.py`
  (el draft lo llamaba `test_evolution_engine.py`).
- **2ª pasada: ROJO también (defectos del validador, no del parche).**
  El parche corregido falló 2 veces con "already exists in working
  directory": el scratch copiaba el archivo de test nuevo ANTES de aplicar
  el parche. Causa raíz (2 niveles): el bucle de archivos tocados no
  saltaba los archivos nuevos, y la detección de archivos nuevos comparaba
  el string exacto `--- /dev/null\n+++ b/...` cuando la línea `---` lleva
  TAB + timestamp. Corregido en el validador (detección por líneas +
  salto en ambos bucles de copia). Ver PIPELINE.md § lecciones.
- **3ª pasada: VERDE (25.2 s).** `validation.json`: applied True,
  python_checks unittest True (4 tests, incluidos los 3 nuevos del parche)
  + scan True, tree_untouched True, scratch eliminado True, sin
  directorios residuales.
- **Verificación en el árbol:** `--scan` tras integrar reporta
  `ctest_targets: 104`; `python3 -m unittest discover` 4/4 OK.
