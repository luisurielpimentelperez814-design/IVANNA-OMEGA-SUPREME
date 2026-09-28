# 🧪 FLANCO TESTS HOST NATIVOS (CTest)

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


**Propietario del flanco:** agente Genspark — sesión iniciada 2026-09-08.
**Protocolo para cualquier otro agente (LEER ANTES DE TOCAR):**

> ⛔ **ESTE FLANCO ESTÁ TOMADO. NO MODIFICAR.** Cada agente trabaja UN
> SOLO flanco. Si necesitas un cambio aquí, deja una nota en AGENT_CLAIMS.md
> o aquí y lo integro yo. Escoge cualquier otro flanco (DSP, daemon/Magisk,
> UI/UX, conversación/Gemini, SAF-HRTF, IAEL, Web Dashboard, Benchmarks).
> No reviertas, no reformatees, no "limpies" archivos de este flanco.

## Archivos bajo este flanco
- `app/src/main/cpp/tests/` (gammatone_numerical_stability.cpp,
  regression/test_oem_stability_suite.cpp, CMakeLists.txt)
- `tests/hrtf/test_ihr1_format.cpp`, `scripts/run_ctest.sh`

## Hallazgo de raíz (auditoría, C2)
| Test | Error verificado |
|---|---|
| test_ihr1_format.cpp | `fatal error: spatial/ihr1_format.hpp: No such file or directory` |
| adaptive_engine/tests/* | `undefined reference to AdaptiveDecisionEngine::evaluate/start/stop` (falta el .cpp en el link) |
| gammatone / regression | `fatal error: gtest/gtest.h: No such file or directory` (GTest vendored sin -I) |

## Plan (de raíz)
1. Conectar includes: `-I app/src/main/cpp` para `spatial/ihr1_format.hpp`; `-I` al GTest vendored (`tests/third_party/googletest/googletest/include` + googlemock) o usar `add_subdirectory` en CMake del target de tests.
2. Añadir los .cpp reales de adaptive_engine al link de sus tests.
3. `run_ctest.sh` como puerta única; verificar con el mismo camino que usa `test-native-dsp`.

## Criterio de "terminado, world-class"
1. `bash scripts/run_ctest.sh` compila y ejecuta TODA la suite sin errores (0 failures).
2. Reproducible en CI job `test-native-dsp` con el mismo include/link.
3. Resultados de CTest en log de CI con exit code real.
