# Motor de Auto-Evolución en Código de IVANNA

> IVANNA mejora su propio código: analiza → propone → escribe el parche →
> valida con tests host → deja listo para revisión → (con aprobación) aplica,
> pushea, la CI compila y publica → rollback si falla.

## 1. Dónde vive el motor (decisión de arquitectura)

**HÍBRIDO. La app nunca reescribe su propio fuente.**

| Capa | Dónde vive | Qué hace |
|---|---|---|
| Recolección | Teléfono (daemon + app) | Telemetría, logs de fallos, métricas DSP, eventos de auto-reparación. La sube por el canal existente (SHM/socket del daemon, o el canal Tailscale+SSH de Muse). |
| Propuesta | VM/servidor (este repo) | `tools/evolution/evolution_engine.py`: lee el estado, construye el paquete de conocimiento, detecta candidatos, genera la propuesta. |
| Generación (LLM) | VM/servidor | El LLM (Gemini vía `IvannaGeminiAgent`, o el analista en el MVP) redacta el parche a partir de `knowledge.json`. |
| Validación | VM/servidor | Compila host (`~/.local/bin/cmake` en `build-tests`), corre el/los tests nuevos y la suite completa (847/847 baseline). |
| Aplicación | VM/servidor + CI | Con aprobación de Esse: commit, push (PAT vía stdin, sin guardarlo), la CI compila Gradle/NDK y publica el release. |
| Rollback | VM/servidor + CI | Si la CI falla o el dispositivo reporta regresión, `git revert` del commit y re-release. |

**Por qué no en el teléfono:** el fuente vive en el repo (VM/GitHub), no en el
teléfono. La app no tiene el código, ni compilador host, ni acceso a GitHub.
Lo que sí puede hacer el teléfono es detectar el síntoma (telemetría) y
proponer el problema; la escritura del parche ocurre donde está el repo.

## 2. Base de conocimiento (qué debe saber el motor)

`tools/evolution/knowledge/knowledge.json` (generado por `--scan`):

- **Estado del repo:** HEAD (últimos 5 commits), árbol sucio, nº de targets ctest.
- **Patrones de referencia:** reglas extraídas del propio código que el motor
  debe respetar al proponer (hoy: `SAF_GUARD (2026-09-17)` en
  `app/src/main/cpp/include/saf_runtime.h :: SAFUpdate`).
- **Candidatos detectados (reglas deterministas):**
  - `saf_without_guards`: funciones SAF sin `std::isfinite`.
  - `dead_functions`: definidas y nunca llamadas en el repo.
  - `untested_top_level_modules`: `.cpp` sin target en `tests/CMakeLists.txt`.
- **Lo que falta añadir (próximas iteraciones):** índice semántico del
  codebase (qué módulo hace qué), historial de fallos del dispositivo
  (logcat + telemetría del daemon), metas/directivas del usuario
  (MEMORY.md), resultados de CI por commit, y el backlog de `docs/`.

## 3. Ciclo de una propuesta

```
scan → knowledge.json → detector → propuesta (parche + justificación)
  → --validate: git apply → cmake build → tests nuevos → ctest completo
  → validation.json → git checkout (revertir) → REPORT.md
  → revisión de Esse → (aprobado) commit + push + CI → release
  → (falla) revert + re-release
```

El entregable de cada propuesta es `tools/evolution/proposals/<id>/`:
`0001-*.patch` (diff aplicable), `REPORT.md` (justificación + evidencia),
`validation.json` (resultado de la validación).

## 4. Gates de seguridad (lo que NUNCA se auto-modifica)

- `app/build.gradle.kts`, `app/src/main/cpp/CMakeLists.txt`
- `app/src/main/java/com/ivanna/omega/security/*` (anti-piratería, IntegrityGate)
- `.github/workflows/*`, `tools/security/*`
- Keystores, claves, PATs, secretos.
- El motor rechaza (`GATE`) cualquier propuesta que toque estos archivos.
- **Aprobación:** por defecto toda propuesta requiere revisión humana de Esse.
  La auto-aplicación sin revisión solo para cambios de bajo riesgo y solo si
  Esse lo autoriza explícitamente (aún no autorizado).

## 5. MVP — propuesta 0001 (completada)

`proposals/0001-saf-phi-infinity-guard/`

- **Hallazgo:** `PhiSAFInfinity` (`app/src/main/cpp/saf_phi_infinity.cpp`,
  compilada en el APK, sin llamadores actuales) no tenía las guardas
  SAF_GUARD que su hermana `SAFUpdate` sí tiene. Repro medido: una sola
  entrada no-finita (NaN/Inf) o G≤0 clavaba el gain en 2.0 (máximo).
- **Fix:** guarda NaN/Inf en las 4 entradas, G degenerada (≤1e-6) y
  denominador degenerado → fail-closed (devuelve el gain actual sin tocar
  el estado); sanitización de G movida ANTES de su uso.
- **Test:** `test_saf_phi_infinity.cpp` (6 tests), ruta válida bit-idéntica
  (1.09091) antes/después.
- **Validación:** ver `validation.json` (tests nuevos 6/6 + suite completa).

## 6. Qué falta para la autonomía completa

1. **Generación por LLM:** conectar Gemini (API key en Secure Vault) para que
   redacte el parche desde `knowledge.json` sin analista. Hoy la redacción
   es del analista; la detección y validación ya son automáticas.
2. **Telemetría del teléfono → conocimiento:** ingestar logs del daemon y
   métricas DSP como candidatos (el teléfono propone el síntoma).
3. **Índice semántico del codebase:** qué hace cada módulo, para proponer
   mejoras de diseño y no solo guardas.
4. **Auto-aplicación con aprobación de Esse:** el flujo commit/push/CI ya
   existe (lo usa el release 370); falta el gate de aprobación explícito
   por propuesta.
5. **Rollback automático en dispositivo:** si la versión instalada reporta
   regresión (telemetría), revertir al release anterior.
6. **Más detectores:** tests que faltan por módulo, duplicación de código,
   funciones con complejidad alta, TODOs/FIXMEs reales.

## 7. Uso

```bash
# Solo analizar (no toca el árbol):
python3 tools/evolution/evolution_engine.py --scan

# Ciclo completo de una propuesta (aplica, valida, revierte):
python3 tools/evolution/evolution_engine.py --proposal 0001 --validate
```
