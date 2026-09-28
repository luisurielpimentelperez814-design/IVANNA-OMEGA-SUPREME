# 🔒 FLANCO RECLAMADO: Tests nativos host (CTest) + integración en CI

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


**Agente:** sesión Genspark (chat), iniciado 2026-09-07.
**Otros agentes: NO TOQUEN este flanco. Elijan cualquier otro frente libre**
(ver `AGENT_CLAIMS.md` — frentes ya tomados: Daemon/Magisk, DSP nativo,
UI/UX Compose, Conversación/IA, IAEL).

## Alcance exacto (lo único que toco)

- `CMakePresets.json` (raíz) — nuevo
- `scripts/run_ctest.sh`
- `app/src/main/cpp/tests/CMakeLists.txt` (solo targets/CTest, no fuentes DSP)
- `tests/hrtf/` y `app/src/main/cpp/tests/*.cpp` — SOLO arreglos de compilación
  o fallos reales que los tests saquen a la luz; sin rediseñar la lógica que
  cubren
- `.github/workflows/tests-host.yml` — workflow NUEVO dedicado a tests host;
  **no** se toca `build.yml` ni `supply-chain.yml` (territorio de otros frentes)

## Protocolo de trabajo

1. Un cambio pequeño y enfocado por commit → compilar/correr tests → commit
   breve → push inmediato a `main`.
2. Cada commit empieza con contexto verificado (salida real de cmake/ctest),
   nunca asumido.
3. Nunca se escribe ningún token/credential en archivos, commits ni logs.
4. Al terminar o abandonar: actualizar `AGENT_CLAIMS.md` con el estado real.

## Evidencia del estado roto (verificada, no asumida)

```
$ bash scripts/run_ctest.sh
CMake Error: Could not read presets from <root>:
File not found: <root>/CMakePresets.json
```

`scripts/run_ctest.sh` invoca `cmake --preset host-release` / `ctest --preset
host-release`, pero `CMakePresets.json` no existe en el repo → la puerta de
tests host está muerta tal cual está. Además hay tests que no están enganchados
a ningún target y otros objetivos huérfanos que este flanco va a auditar uno a
uno.

---

## Auditoría NDK-only (cerrada, read-only — el CMakeLists del daemon NO es mío)

Verificado hoy leyendo `app/src/main/cpp/CMakeLists.txt` (líneas 339-358):

- El target `test_oem_stability_suite` del árbol NDK compila el MISMO
  `tests/regression/test_oem_stability_suite.cpp` que ya corre en el proyecto
  host (mi target, línea 305 del CMakeLists de tests). Es un duplicado: la
  suite corre 2 veces por build de APK.
- Recomendación para el flanco Daemon (no actuar yo): eliminar el target del
  CMakeLists del daemon y dejar solo el host-side — gana ~10-20 s de build de
  release sin perder cobertura, porque la puerta host corre la suite en cada
  push (verificado: corridas 34170101339, 34170280449, 34170484627 verdes).
- Los otros objetivos del CMakeLists del daemon (`ivanna_omega`, `omega_effect`,
  `stage_omega_effect`, `add_subdirectory(daemon)`) son productos reales del
  release — sin duplicación detectada.
- `AdaptiveEQ` sigue inexistente en el árbol; `test_adaptive_eq_stress.cpp`
  permanece documentado como irrecuperable hasta que el flanco DSP decida.

## Estado final del flanco (cerrado)

- Puerta host: 60/60 PASS (Release y ASan+UBSan), ~15 s local, ~1 min por
  pata en CI, paralela, con timeout por test y concurrency con cancelación.
- CI: corridas reales verificadas success — 34165846942, 34170101339,
  34170280449, 34170484627 (última en HEAD).
- TSan: carril semanal (cron lunes 04:23 UTC) + manual, timeout 90 min,
  justificado con 2 cancelaciones medidas (>45 min en runner de 2 núcleos).
- Documentación: README principal (badge + puerta), README de cpp/tests
  (tabla completa), este archivo (protocolo + auditoría NDK + cierre).
