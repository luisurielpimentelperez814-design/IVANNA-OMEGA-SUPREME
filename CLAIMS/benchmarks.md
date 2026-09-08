# 🔒 FLANCO RECLAMADO: Benchmarks (tools/benchmark_suite.cpp + scripts/benchmark_device.sh + docs/BENCHMARKS.md)

**Agente:** sesión Genspark (chat), iniciado 2026-09-08.
**Otros agentes: NO TOQUEN este flanco. Elijan cualquier otro frente libre.**

## Alcance exacto (lo único que toco)

- `tools/benchmark_suite.cpp` (completo)
- `scripts/benchmark_device.sh` (completo)
- `docs/BENCHMARKS.md` (completo)
- Target de CMake/CTest SOLO para el benchmark host (nuevo, en `app/src/main/cpp/tests/CMakeLists.txt`
  como target OPCIONAL fuera de la puerta — la puerta de tests es de otro flanco cerrado mío:
  Tests host/CTest; la coordinación es conmigo mismo, un commit lo documenta)

## NO toca

- La puerta de tests (targets `ivanna_add_test(...)` existentes) — solo añado
  un target `ivanna_benchmark` aparte que no corre en la puerta
- DSP, UI, daemon, IAEL, dashboard, HEXAGON, controles, conversación

## Evidencia del estado roto (verificada hoy, no asumida)

1. `tools/benchmark_suite.cpp` (132 líneas) tiene CERO referencias: no está en
   ningún CMakeLists, workflow ni script (grep verificado) — no compila ni
   corre en ninguna parte. Orfandad total del benchmark de CPU/latencia host.
2. `docs/BENCHMARKS.md` documenta el flujo — pendiente verificar si coincide
   con la realidad.
3. `scripts/benchmark_device.sh` corre en dispositivo (root) — verificable
   aquí solo con shellcheck + revisión lógica, no en ejecución.

## Protocolo

1. Commit breve por cambio → verificación real (compilación host, shellcheck,
   corrida real del benchmark) → push inmediato.
2. Nunca escribir tokens en archivos/commits/logs.
3. Al terminar o abandonar: actualizar AGENT_CLAIMS.md.
