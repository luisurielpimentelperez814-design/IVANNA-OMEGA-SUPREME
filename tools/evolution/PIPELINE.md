# Motor de evolución / auto-reparación — PIPELINE (2026-11-14)

> Detectar → reescribir (LLM vía OmniRoute) → verificar en scratch →
> solo entonces publicar. Si la verificación falla, NO se publica.
> Un commit por cambio, directo a main. Nunca fabricar métricas.

## 1. Piezas

| Archivo | Qué hace |
|---|---|
| `tools/evolution/evolution_engine.py` | Motor: estado, conocimiento, detección, draft (LLM), validación en scratch, `--evolve`. |
| `tools/evolution/llm_client.py` | Cliente OmniRoute: `chat()`, `draft_rewrite()`, `extract_patch()`, `health()`. Solo stdlib. |
| `tools/evolution/tests/test_scan_saf_guards.py` | Unit tests del motor (`python3 -m unittest discover -s tools/evolution/tests -v`). |
| `tools/evolution/knowledge/knowledge.json` | Generado por `--scan`: estado + detectores. |
| `tools/evolution/proposals/<id>/` | `<id>.patch` + `DRAFT.md` + `REPORT.md` + `validation.json`. |
| `tools/autorepair/rules.py` | Reglas de clasificación de telemetría (7 tipos, umbrales documentados). |
| `tools/autorepair/watcher.py` | Polling del puente + encolado (cron 5', inactivo sin puente). |
| `tools/autorepair/run_fix.py` | Orquestador: `next` / `draft` / `validate` / `gates` / `publish` / `report` / `pending` / `ack`. |

## 2. Cómo detecta cada tipo de problema

### 2.1 Desde el repo (evolution_engine.py `--scan`, ~30 s)

| Detector | Qué detecta | Cómo |
|---|---|---|
| `saf_without_guards` | Módulos SAF sin guarda `isfinite` (patrón SAF_GUARD 2026-09-17) | `.cpp` `saf_*` + header hermano; sin funciones no hay hit; dedup (0002) |
| `dead_functions` | Funciones definidas sin llamadores en `app/src/main` | `git grep` por función (lento; falso negativo si solo se menciona en comentario) |
| `untested_top_level_modules` | `.cpp` de primer nivel sin target en `tests/CMakeLists.txt` | Comparación literal de nombres |
| `host_checks_red` (0003) | Los 6 chequeos de Fase 0 en rojo | Ejecuta `scripts/check_{jni_wiring,kotlin_wrapper_wiring,header_wiring,rt_safety,docs_claims,datasets_deploy}.py` (timeout 120 s c/u); ningún chequeo tumba el scan |
| `todo_fixme` (0003) | TODO/FIXME en el C++ nativo (tope 50; excluye `tests/third_party`) | `git grep -n -E "TODO\|FIXME"` |

Estado medido 2026-11-14 (HEAD 8ac65b9d): `host_checks_red` 6/6 en rojo
(jni 12, kotlin 13, header 20, rt_safety 6, docs_claims 18,
datasets_deploy 46 líneas `[FAIL]`); `todo_fixme` 28; `saf_without_guards`
1 hit real (`saf_latent_bridge.cpp`); `dead_functions` 11; `untested` 19.

### 2.2 Desde el teléfono (autorepair: watcher + rules.py)

7 reglas sobre el snapshot del puente (`GET /v1/diagnostics/latest`):
`daemon-desconectado`, `termico`, `route-b-no-acopla` (dormida hasta el
esquema v2 con `routeB`), `asrc-deriva` (>10 underruns/min),
`dsp-sobrecarga` (>60 frameDrops/min o >4000 µs/bloque), `memoria-alta`,
`latencia-degradada`. Auto-arreglables: route-b, dsp, daemon, latencia.
Solo reporte: asrc-deriva (vive en `hal_integration/`, no se toca),
termico, memoria-alta. Ver `tools/autorepair/README.md` §3–4.

## 3. Cómo el LLM redacta la reescritura

- **Cliente:** `tools/evolution/llm_client.py` → `POST
  http://localhost:20128/v1/chat/completions` (o `IVANNA_OMNIROUTE_URL`),
  modelo `ivanna-free` (pool ddgw que sí funciona en v3.8.53), temperatura
  0.2, `max_tokens` 8192.
- **Key:** solo en `~/.omniroute/ivanna-app-key` (modo 600, solo host).
  Se lee en cada llamada; nunca se imprime, nunca se guarda en el repo,
  nunca sale en errores ni logs.
- **Timeouts:** 15 s connect / 60 s read en llamadas normales; el draft usa
  240 s de lectura (medido: una respuesta trivial tardó 37.6 s por el
  gateway; un diff completo necesita más). Reintentos 3 con backoff
  1/2/4 s, solo ante 429, 5xx o error de red.
- **Qué recibe:** el hallazgo (tipo, título, evidencia medida) + el
  contenido de los archivos relevantes (recortado a 12000 caracteres por
  archivo). NUNCA credenciales, secretos, PATs ni tokens.
- **Qué devuelve:** un bloque ` ```diff ` con el unified diff completo
  (o, en plan B, bloques `# FILE: <ruta>` con el contenido completo, que
  se convierten a diff con `difflib`). Si no devuelve nada utilizable, el
  draft falla: no se inventa un parche.
- **Gates del draft** (`is_draft_forbidden`, además de `FORBIDDEN`):
  archivos de versión (los mueve solo el publish), `README.md`,
  `app/src/main/java/com/ivanna/omega/audio/` (doctor runtime del agente
  principal, 2026-11-14). El parche del LLM se vuelve a chequear contra
  esta lista antes de aceptarlo.

## 4. Gates de verificación, en orden

### 4.1 Validación de una propuesta (`--proposal <id> --validate`)

1. **Gate prohibidos:** el parche no toca `FORBIDDEN` (build.gradle.kts,
   `cpp/CMakeLists.txt`, `security/` = IntegrityGate, `.github/workflows/`,
   `tools/security/`, keystores, `*SECRET*`, `*PAT*`).
2. **Gate secretos:** regex sobre el texto del parche (`ghp_…`,
   `github_pat_…`, `password=`, `token=`).
3. **Scratch:** `git worktree add --detach` junto al repo (no en `/tmp`:
   es un tmpfs de 512 MB y el repo pesa 1.5 GB) + `git diff HEAD --binary`
   encima (la base es el árbol actual, no HEAD pelado) + copia de los
   archivos sin seguir del motor (`git status -uall`, `tools/evolution/`,
   `tools/autorepair/`) + copia de los archivos tocados que no estén
   seguidos. El árbol real NUNCA se toca.
4. **Gate apply:** `git apply --check` dentro del scratch.
5. **Verificación:**
   - Parche C++: cmake configure + build de los targets nuevos + ejecución
     + build completo + CTest completo en el scratch. El gate es 100%.
   - Parche sin C++: CTest se registra como saltado (con el motivo) y se
     corren las auto-pruebas del motor en el scratch (`unittest discover`
     + `--scan` smoke).
6. **Limpieza:** `git worktree remove --force` + `prune` + borrado del
   tmpdir padre. Se verifica que `git status --porcelain` es idéntico
   antes y después (`tree_untouched`).
7. **Registro:** `validation.json` se escribe SIEMPRE (también en los
   retornos tempranos), con `applied`, `built`, `new_tests`, `full_suite`,
   `python_checks`, `scratch`, `tree_untouched`.

### 4.2 Publicación (`run_fix.py publish`, solo tras validate + gates)

- `validate` (CTest 100% en `build-tests`, con lock anti-concurrencia) →
  `gates` (sin prohibidos, ≤6 archivos, ≤300 líneas, sin secretos) →
  `publish --execute` solo con `IVANNA_ALLOW_PUBLISH=1` → bump de versión
  (5 archivos sincronizados) + commit + push con PAT por stdin + URL
  remota limpia después. Sin `--execute` o sin la variable, es dry-run.
  Nunca force-push. El publish lo ejecuta el operador, no el motor.

### 4.3 Ciclo completo (`--evolve <id> --finding '<json>'`)

`draft_patch` → escribe la propuesta → `validate_proposal` (scratch) →
`REPORT.md` con el veredicto (VERDE/ROJO). No aplica al árbol, no commitea,
no publica. Si el veredicto es ROJO, la propuesta queda como evidencia y
no se publica.

## 5. Uso

```bash
# Detección (no toca el árbol):
python3 tools/evolution/evolution_engine.py --scan

# Redactar un draft para un hallazgo (sin validar):
python3 tools/evolution/evolution_engine.py --draft 0004 --finding \
  '{"kind":"host_checks_red","title":"...","evidence":"...","files":["..."]}'

# Ciclo completo de una propuesta (draft + validar en scratch):
python3 tools/evolution/evolution_engine.py --evolve 0004 --finding '{...}'

# Validar una propuesta existente (parche ya en proposals/<id>/):
python3 tools/evolution/evolution_engine.py --proposal 0003-new-detectors --validate

# Unit tests del motor:
python3 -m unittest discover -s tools/evolution/tests -v

# Autorepair: redactar el parche de un incidente con el LLM (no aplica nada):
python3 tools/autorepair/run_fix.py draft --incident tools/autorepair/processing/<id>.json
python3 tools/autorepair/run_fix.py validate && python3 tools/autorepair/run_fix.py gates
```

## 6. Lecciones de esta construcción (2026-11-14)

- **El pool `ivanna-free` no siempre devuelve diffs:** dos veces devolvió
  ` ```diff\n*** End Patch ` (vacío) en ~1 s; el formato código-plano
  (` ```python ` con el código completo) sí funcionó (4.2 s). El motor
  pide ` ```diff ` primero y cae al plan B (bloques `# FILE:` +
  `difflib`) si el diff viene vacío. No reintentar el mismo formato que
  ya falló dos veces. Y hay un TERCER formato: el modelo emite a menudo
  `*** Begin Patch` / `*** Update File:` (apply_patch) aunque se le pida
  unified diff — desde la 0005, `extract_patch()` lo convierte a unified
  diff por coincidencia exacta de contexto (fail-closed si el contexto es
  ambiguo o no existe). Un `run_fix.py draft` fue rechazado por no aceptar
  ese formato antes de la 0005; ahora se acepta.
- **El `new file mode 100644` es obligatorio** en un diff generado para
  archivos nuevos: sin él, `git apply` falla con "dev/null: No such file
  or directory" (medido en repo recién creado).
- **Los hunks del LLM no son fiables:** la 0002 traía `@@ -1,0 +1,0 @@`
  y `index <commit>..00000000` inventados; el código era correcto, el
  framing no. Integrar a mano con revisión, no con `git apply` a ciegas.
- **El LLM se contradice en sus propios tests:** el draft 0002 ponía
  `header_checked` con la última ruta candidata y su test esperaba null.
  La integración debe correr el test y corregir, no confiar.
- **Scratch en /tmp no cabe:** /tmp es tmpfs de 512 MB, el repo 1.5 GB.
  El scratch vive junto al repo (`~/workspace/ivanna-evo-*`).
- **La base del scratch es el árbol actual, no HEAD:** el motor tiene
  trabajo sin commitear y los parches se redactan contra ese árbol. Sin el
  overlay `git diff HEAD`, el parche no aplica en el worktree.
- **Los archivos sin seguir no llegan solos al worktree:** ni el worktree
  ni `git diff HEAD` los incluyen. Hay que copiarlos (`git status -uall`).
  Sin esto, el `unittest` del motor se saltaba en el scratch.
- **El tmpdir padre queda huérfano:** `git worktree remove` borra solo el
  `wt`, no el padre (que guarda `dirty.patch`). Borrado explícito al final.
- **`validation.json` debe escribirse siempre:** los retornos tempranos
  (base desconocida, apply --check fallido) no dejaban evidencia. Ahora se
  escribe en el `finally`.
- **Los archivos nuevos del parche no se copian al scratch:** el scratch
  copiaba los archivos sin seguir del motor ANTES de aplicar el parche, y
  un archivo nuevo del parche ya existía al aplicar ("already exists in
  working directory", 0004). Hay que saltarlos en AMBOS bucles de copia
  (el general y el de archivos tocados). Y la detección de "archivo nuevo"
  no puede comparar el string exacto `--- /dev/null\n+++ b/...`: la línea
  `---` lleva TAB + timestamp (`--- /dev/null\t2026-...`); se detecta por
  líneas (`startswith("--- /dev/null")` + siguiente `+++ b/`). El mismo
  bug estaba en la parte de test del parche 0002 (regenerada con
  `--- /dev/null` correcto).
- **El detector TODO/FIXME debe excluir `third_party`:** el googletest
  vendoreado aportaba 13 de 41 TODOs; no son deuda de IVANNA.

## 7. Qué queda pendiente para que el motor corra solo

- [ ] **Puente desplegado en Render** (URL + token) + **cron del watcher**
      activado (`tools/autorepair/cron/watcher.cron`): sin esto no hay
      telemetría y el autorepair no se dispara solo.
- [ ] **App con el conector de diagnóstico** apuntando al puente + esquema
      v2 del snapshot (`routeB`) para despertar `route-b-no-acopla`.
- [ ] **Bucle de fix en cron del agente principal:** revisar
      `run_fix.py pending` cada 30 min; `draft` (LLM) o subagente con el
      prompt; `validate` + `gates` + `publish` (con `IVANNA_ALLOW_PUBLISH=1`).
      Hoy el `draft` existe pero el lanzamiento sigue siendo manual.
- [ ] **Propuestas 0002/0003 sin publicar:** están en el árbol sin
      commitear (por orden: este trabajo no commitea ni pushea). Publicar
      es un commit por cambio, directo a main, con CI verde.
- [ ] **Quitar `app/.../audio/` de DRAFT_FORBIDDEN** cuando el agente
      principal termine el doctor runtime: hoy es un hold temporal
      (2026-11-14) y mientras dura, los drafts de incidentes
      `route-b-no-acopla` se rechazan con GATE (su archivo principal vive
      en `audio/`); esos incidentes siguen la vía del prompt quirúrgico
      (`run_fix.py next` + subagente).
- [ ] **Los 6 chequeos de Fase 0 en rojo:** el motor los detecta pero sus
      fixes viven en `scripts/` y `README.md` (fuera del ámbito de este
      trabajo). Son las próximas propuestas del motor (draft + validación
      en scratch, revisión del agente principal antes de publicar).
- [ ] **Detector `dead_functions`:** lento (un `git grep` por función) y
      con falsos positivos/negativos (menciones en comentarios cuentan como
      llamadores; llamadas por JNI/`dlsym` no se ven). Candidato a 0004.
- [ ] **Validación C++ en scratch desde cero:** cada propuesta C++ hace
      configure + build completo en el scratch (lento). Medir el tiempo
      real en la primera propuesta C++ y decidir si se cachea `ccache`.
