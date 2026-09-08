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

## Entregado (2026-09-08)

- benchmark_suite.cpp compila y corre por primera vez (fix setAmount→setParams wet=0.45)
- Target `ivanna_benchmark` (EXCLUDE_FROM_ALL) en tests/CMakeLists.txt — fuera de la puerta
- Corridas verificadas: 48k/256/15s → 0.99% CPU, e2e 5.39 ms; 48k/128 → e2e 2.69 ms; 96k/512 → 1.96% CPU, e2e 5.44 ms
- benchmark_device.sh: shellcheck limpio (SC2034, 3×SC2086 corregidos)
- docs/BENCHMARKS.md: corrida de referencia reproducible con comandos exactos
- Puerta de tests intacta (74/74) y CI verde (corrida 34291175045)
- Pendiente externo: protocolo on-device Moto G85 (requiere hardware físico)
