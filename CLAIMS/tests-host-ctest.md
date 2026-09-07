# 🔒 FLANCO RECLAMADO: Tests nativos host (CTest) + integración en CI

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
