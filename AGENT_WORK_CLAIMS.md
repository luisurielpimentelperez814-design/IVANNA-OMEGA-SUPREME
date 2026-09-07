# AGENT_WORK_CLAIMS.md — Protocolo de coordinación entre sesiones concurrentes

Este repositorio recibe trabajo de múltiples sesiones de IA en paralelo,
sin memoria compartida entre ellas ni revisión humana entre commits. Sin
coordinación, eso produce colisiones reales y documentadas en este mismo
historial de git:

- `126ce746` — una sesión sobrescribió el README de otra, calificándolo de
  "genérico", con cifras distintas del mismo árbol (39 vs 218 SOFA).
- `d8ce8a72` — `IvannaCognitiveCore` simulado/stub quedó commiteado y
  otra sesión tuvo que purgarlo.
- `d556df81` — `GeminiOrchestrator` legacy y `GeminiKeyStore` quedaron
  huérfanos (0 callers) tras un rediseño que otra sesión no vio.
- `c0fbb7f5`, `fbbdfaf4`, `98eb439e` — archivos/carpetas duplicados de
  módulos Magisk, CMake roto y snapshots muertos, cada uno de una sesión
  distinta pisando o ignorando el trabajo de la anterior.
- Un release (`v2.3.2`) quedó borrado sin recrear porque dos sesiones
  empujaron casi al mismo tiempo y el concurrency group del workflow
  cortó el `gh release delete` a mitad de un `create` (ya corregido en
  `bec48f5e`).

**Regla:** antes de emprender trabajo sustancial (no un fix de una línea),
revisa la tabla de abajo. Si el área ya tiene un claim activo de otra
sesión, trabaja en un flanco distinto. Si tu propio trabajo va a tomar
más de una sesión, reclama tu flanco aquí ANTES de empezar, y actualiza
el estado al cerrar cada sesión — aunque sea a medias.

## Claims activos

| Flanco | Sesión | Estado | Última actualización | Alcance (sí toca) | Fuera de alcance (no tocar) |
|---|---|---|---|---|---|
| Build, empaquetado y entrega | Claude (esta sesión) | 🟡 EN PROGRESO | 2026-09-07 | `.github/workflows/`, `magisk_module/` (estructura, `module.prop`, `update.json`, `service.sh`, `sepolicy.rule`), `app/src/main/cpp/daemon/` (lifecycle, `CMakeLists.txt`), IPC/SHM (`omega_control_bus.*`, `shm_manager.*`, `shm_hyperplane.cpp`), `version.properties`, empaquetado del zip/APK, publicación de release | Algoritmos DSP (`pd_engine.hpp`, `hrtf_convolver.cpp` internals, `cue_based_spatial.hpp`), UI/Compose, capa conversacional (Gemini/Firebase), memoria del asistente (`IvannaMemoryArchitecture`) |

## Cómo añadir tu propio claim

Añade una fila con tu flanco, fecha, y alcance explícito (qué SÍ y qué NO
tocas). Mantén el alcance angosto — un claim que dice "todo el repo" no
sirve para nada. Marca 🟢 CERRADO cuando termines, o borra la fila si el
trabajo se abandona.
