# Auto-reparación y auto-evolución de IVANNA — bucle cerrado automático

> La app reporta un problema → la VM lo detecta → un agente lo diagnostica
> y parchea → se prueba (CTest + CI) → si pasa, se publica solo. Sin que
> Esse tenga que correr comandos ni pegar salidas.

## 1. El bucle (diagrama)

```
Teléfono (Moto G85)                    VM (este repo)                        GitHub
┌─────────────────┐   POST /v1/diagnostics  ┌──────────────────────┐
│ app + daemon    │ ──────────────────────▶ │ puente (Render)      │
│ telemetría del  │   snapshot JSON ≤16 KiB │ GET /latest          │       ┌──────────┐
│ DSP (cada N min)│                         └──────────┬───────────┘       │ CI       │
└─────────────────┘                                    │ GET /latest     │ paso 17  │
                                                       ▼                 │ (Kotlin) │
                                        ┌──────────────────────┐         └────┬─────┘
                                        │ watcher.py (cron 5')│              │
                                        │  - detecta snapshot  │              │ publica
                                        │    nuevo (sequence)  │              ▼
                                        │  - clasifica (rules) │       release v2.5.N
                                        │  - encola incidente  │              │
                                        └──────────┬───────────┘              │
                                                   │ queue/<ts>-<tipo>.json  │
                                                   ▼                          │
                                        ┌──────────────────────┐              │
                                        │ run_fix.py next      │              │
                                        │  - mueve a processing│              │
                                        │  - genera el prompt  │              │
                                        │    quirúrgico        │              │
                                        └──────────┬───────────┘              │
                                                   │ prompts/<id>.md          │
                                                   ▼                          │
                                        ┌──────────────────────┐              │
                                        │ subagente de Muse    │              │
                                        │  - parchea (mínimo,  │              │
                                        │    fail-closed)      │              │
                                        │  - corre CTest       │              │
                                        │  - rellena fix.*     │              │
                                        └──────────┬───────────┘              │
                                                   │                          │
                                        ┌──────────┴───────────┐              │
                                        ▼                      ▼              │
                                  run_fix.py validate   run_fix.py report     │
                                  + gates (100% CTest,  (no publicable:      │
                                  cambio mínimo, sin     reporte para Esse)   │
                                  prohibidos/secretos)                        │
                                        │                                     │
                                        ▼                                     │
                                  run_fix.py publish ───────────────────────▶┘
                                  (bump + commit + push [publish-release],
                                   PAT por stdin, URL limpia después)
```

**Una aclaración honesta sobre "automático":** el parche lo escribe un
subagente de Muse (un script no puede lanzar subagentes). Lo que este
sistema automatiza es todo lo demás: detección, clasificación, encolado,
generación del prompt quirúrgico, validación, gates y publicación. El
agente principal solo tiene que lanzar el subagente con el prompt generado
y, si todo pasa, el publish. El paso 2 (el subagente) es el único que
necesita un agente; el resto son comandos.

## 2. Piezas

| Archivo / dir | Qué hace |
|---|---|
| `rules.py` | Las reglas de clasificación. Cada regla es una función `rule_<nombre>(snapshot, estado)` que devuelve la razón o `None`. Umbrales documentados junto a cada regla. |
| `watcher.py` | Consulta `GET /v1/diagnostics/latest` (token por `IVANNA_DIAGNOSTIC_TOKEN`, nunca en el código). Detecta snapshots nuevos por `sequence`, clasifica y encola. Sin puente o sin snapshot nuevo: silencio, no spam. |
| `run_fix.py` | Orquestador: `next` / `validate` / `gates` / `publish` / `report` / `pending` / `ack`. |
| `queue/` | Un JSON por incidente pendiente: `<timestamp>-<tipo>.json` (snapshot + clasificación + contexto). Deduplicado por `sequence` + tipo. |
| `processing/` | Incidentes en curso (el `next` los mueve aquí con un rename atómico). |
| `archive/` | Incidentes ya procesados (publicados o reportados). |
| `reports/` | Reportes de no-publicación para Esse (`<id>.md` + `<id>.ack` al entregarse). |
| `prompts/` | Prompts quirúrgicos generados para el subagente de fix. |
| `state/` | Estado del watcher (`watcher_state.json`: última sequence y contadores, para las reglas de tasa) y `last_validation.json` (última validación CTest). |
| `examples/` | Snapshots de ejemplo para probar sin puente. |
| `cron/watcher.cron` | Línea de cron documentada (INACTIVA hasta que el puente tenga URL + token). |

## 3. Reglas de clasificación (hoy)

| Tipo | Severidad | Dispara cuando | Umbral |
|---|---|---|---|
| `daemon-desconectado` | alta | agente corriendo pero `daemonConnected == false` | — |
| `termico` | alta | `thermalThrottling == true` | — |
| `route-b-no-acopla` | alta | Ruta B activa y beacon en 0 frames, `enabledCount == 0` | beacon ≤ 0 |
| `asrc-deriva` | media | underruns creciendo rápido | > 10/min |
| `dsp-sobrecarga` | media | frameDrops creciendo rápido, o bloque DSP demasiado lento | > 60/min o > 4000 µs |
| `memoria-alta` | media | `memoryPressure == "high"` | — |
| `latencia-degradada` | media | `latencyDegraded == true` | — |

Notas:
- Las reglas de **tasa** (underruns, frameDrops) necesitan dos muestras: en
  la primera vuelta no disparan; el watcher guarda la muestra en `state/`.
- **`route-b-no-acopla` está dormida en el esquema v1 del puente**: el
  `sanitize()` del puente solo reenvía `metrics` + `agent`, no el campo
  `routeB` (beacon). La regla existe y dispara sola cuando la app envíe
  esquema v2 con `routeB: {active, beaconFrames, enabledCount}`. Mientras
  tanto no da falsos positivos: exige que el campo esté presente.
- Un snapshot puede disparar varias reglas: cada una genera su propio
  incidente (un archivo por incidente).

## 4. Qué intenta arreglar solo y qué solo reporta

En `run_fix.py` (`CONOCIMIENTO`, por tipo):

| Tipo | Auto-fix | Por qué |
|---|---|---|
| `route-b-no-acopla` | ✅ sí | El fix vive en el repo (GlobalFX / Bridge / omega_effect / daemon). |
| `dsp-sobrecarga` | ✅ sí | Shed / amounts / anillo, en el repo. |
| `daemon-desconectado` | ✅ sí | Supervisor / service.sh / socket, en el repo. |
| `latencia-degradada` | ✅ sí | Buffers / anillo, en el repo. |
| `asrc-deriva` | ❌ reporte | El fix vive en `hal_integration/` (untracked, de otro agente). No se toca. |
| `termico` | ❌ reporte | Es físico (temperatura); no hay parche de código seguro. |
| `memoria-alta` | ❌ reporte | Requiere investigación; no hay fix mínimo obvio. |

Si el tipo no es auto-arreglable, o el CTest falla, o el cambio es mayor
que el mínimo, o toca algo prohibido → NO se publica: se escribe el reporte
en `reports/` y queda listo para que el agente principal se lo entregue a
Esse.

## 5. Límites de seguridad (DecisionAuthority, NO negociables)

La automatización NUNCA puede:
- Publicar sin CTest al **100%** (`validate` es el gate; `publish` lo vuelve
  a correr si la validación no está fresca).
- Tocar archivos prohibidos (lista importada del motor de evolución
  `tools/evolution/evolution_engine.py`: `app/build.gradle.kts`,
  `app/src/main/cpp/CMakeLists.txt`, `security/` (IntegrityGate),
  `.github/workflows/`, `tools/security/`, keystores, `*SECRET*`, `*PAT*`).
- Desactivar IntegrityGate, tocar credenciales, comprar nada.
- Publicar un cambio mayor que el mínimo: **≤ 6 archivos, ≤ 300 líneas**
  (el gate se evalúa solo sobre los archivos del fix, no sobre el trabajo
  ajeno sin commitear que haya en el árbol).
- Pushear sin `IVANNA_ALLOW_PUBLISH=1` explícito (sin él, `publish` es
  dry-run aunque se pida `--execute`).
- Hacer force-push. Si el push es rechazado (p. ej. el bot publicó
  `update.json` después), se detiene y se imprimen los pasos de rebase.

El PAT de GitHub (autorizado por Esse "hasta que diga que no") solo
transita por stdin/memoria (`getpass` o `IVANNA_GH_TOKEN`): nunca se escribe
en disco, nunca se imprime, y la URL remota se restaura limpia tras el push
(verificado con `git remote -v`).

## 6. Uso (comandos)

```bash
cd ~/workspace/ivanna/tools/autorepair

# --- Vigilancia ---
python3 watcher.py --once                 # una vuelta (lo que corre el cron)
python3 watcher.py --loop --interval 5    # bucle cada 5 min (prueba manual)
python3 watcher.py --self-test            # clasifica los ejemplos (sin puente)

# --- Cola y fix ---
python3 run_fix.py pending                # qué hay pendiente / en proceso / reportes
python3 run_fix.py next                   # toma el incidente más antiguo, genera el prompt
# ... el agente principal lanza un subagente con prompts/<id>.md ...
# ... o redacta el parche con el LLM (sin subagente, no aplica nada):
python3 run_fix.py draft --incident processing/<id>.json
# ... (revisa processing/<id>.patch y aplícalo) ...
python3 run_fix.py validate               # compila + CTest completo (gate 100%)
python3 run_fix.py gates --incident processing/<id>.json
python3 run_fix.py publish --dry-run --incident processing/<id>.json
IVANNA_ALLOW_PUBLISH=1 python3 run_fix.py publish --incident processing/<id>.json --execute
# ... o, si no se puede publicar:
python3 run_fix.py report --incident processing/<id>.json --reason "..."
python3 run_fix.py ack <reporte>.md       # marca un reporte como entregado a Esse
```

Variables de entorno:
- `IVANNA_BRIDGE_URL` — URL base del puente (def. `http://127.0.0.1:8787`,
  útil en local; en producción es la URL de Render).
- `IVANNA_DIAGNOSTIC_TOKEN` — bearer token del puente (≥ 32 caracteres).
  Sin él, el watcher sale en silencio.
- `IVANNA_CMAKE` — cmake a usar (def. `~/.local/bin/cmake`).
- `IVANNA_ALLOW_PUBLISH=1` — habilita el `publish --execute`.
- `IVANNA_GH_TOKEN` — PAT (alternativa al prompt por stdin).

## 7. Cómo añadir una regla nueva

1. Añade la función en `rules.py`:
   ```python
   def rule_mi_problema(snapshot, estado):
       """Qué detecta y por qué (umbral documentado)."""
       if <condición>:
           return "<razón en español>"
       return None
   ```
2. Regístrala en `REGLAS`, añade su severidad en `SEVERIDAD` y su posición
   en `ORDEN_REGLAS`.
3. Añade su entrada en `CONOCIMIENTO` en `run_fix.py` (auto_fixable, archivos,
   pistas, prohibido). Si no sabes el fix, pon `auto_fixable: False` con el
   motivo: irá a reporte, no a parche.
4. Añade un ejemplo en `examples/snapshot-<tipo>.json` con `_expected_tipo`
   y, si la regla es de tasa, `_state` con la muestra previa.
5. Corre `python3 watcher.py --self-test` (debe dar 6/6: hay 6 ejemplos).

## 8. Qué falta para que funcione de punta a punta (checklist)

- [ ] **Puente desplegado en Render** con URL pública + token (falta la
      cuenta/API key de Render de Esse; `render.yaml` ya está en la raíz
      del repo y el `Dockerfile` en `diagnostic-bridge/`). Sin esto, el
      watcher no tiene nada que leer.
- [ ] **App con el conector de diagnóstico apuntando al puente** (URL +
      token en ajustes; subida explícita activada). Sin esto, el puente
      nunca recibe snapshots.
- [ ] **Cron activado** (`cron/watcher.cron`): cada 5 min, con
      `IVANNA_BRIDGE_URL` e `IVANNA_DIAGNOSTIC_TOKEN` por el flujo seguro.
      Hoy está documentado-but-inactivo.
- [ ] **Esquema v2 del snapshot** (opcional, para la regla de Ruta B): que
      la app envíe `routeB: {active, beaconFrames, enabledCount}` y que el
      puente lo reenvíe en `sanitize()`. Sin esto, `route-b-no-acopla`
      sigue dormida (las demás reglas sí funcionan con el esquema v1).
- [ ] **Bucle de fix en cron del agente** (opcional): que el agente
      principal revise `run_fix.py pending` cada 30 min y lance el
      subagente con el prompt generado. Hoy ese paso es manual.
- [ ] **Prueba de punta a punta**: con el puente desplegado, forzar un
      snapshot con throttling y verificar que el incidente llega a `queue/`.

## 9. Honestidad: qué SÍ automatiza y qué NO

**SÍ automatiza:**
- Detección de problemas conocidos en la telemetría (cada 5 min, sin spam).
- Clasificación con reglas explícitas y umbrales documentados.
- Generación del prompt quirúrgico con el contexto del incidente y del repo.
- Validación (CTest 100%) y gates de seguridad antes de publicar.
- Bump de versión + CHANGELOG + commit + push con `[publish-release]`
  (con los gates pasados y `IVANNA_ALLOW_PUBLISH=1`).
- Reporte para Esse cuando algo no es publicable.

**NO automatiza (y no pretende):**
- Inventar física nueva ni reescribir arquitectura: solo fixes mínimos de
  problemas conocidos, fail-closed.
- Escribir el parche sin un agente: el subagente de Muse es el que parchea;
  este sistema lo orquesta y lo valida.
- Decidir por Esse en cambios riesgosos: esos van a reporte, no a push.
- Verificar en el teléfono: el CTest es host; el Kotlin lo compila la CI
  (paso 17) y la verificación en dispositivo (logcat + beacon + escucha)
  sigue siendo de Esse.
- La regla de Ruta B hasta que exista el esquema v2 (ver §3).

## 10. Estructura

```
tools/autorepair/
├── README.md            # este archivo
├── rules.py             # reglas de clasificación (umbrales documentados)
├── watcher.py           # polling del puente + encolado
├── run_fix.py           # orquestador (next/validate/gates/publish/report/pending/ack)
├── cron/watcher.cron    # cron documentado (INACTIVO)
├── queue/               # incidentes pendientes (un JSON por incidente)
├── processing/          # incidentes en curso
├── archive/             # incidentes procesados
├── reports/             # reportes de no-publicación para Esse
├── prompts/             # prompts quirúrgicos generados
├── state/               # estado del watcher + última validación
└── examples/            # snapshots de ejemplo (probar sin puente)
```

Relación con el motor de evolución (`tools/evolution/`): ese motor propone
mejoras de código desde el repo (detectores + validación con revert); este
sistema reacciona a la telemetría del teléfono. Comparten la lista de
archivos prohibidos (importada de `evolution_engine.py`) y el gate CTest.
El `validate` de aquí NO revierte el árbol (el fix debe quedar para el
commit); el `validate_proposal` de allí sí revierte (su entregable es el
diff, no el cambio aplicado).
