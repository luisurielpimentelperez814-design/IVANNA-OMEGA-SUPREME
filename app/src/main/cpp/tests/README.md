# cpp/tests/ — Fase 4 surgical-hardening-v4

Suite host-side para validar estabilidad numérica del motor nativo sin depender del APK.

## Qué cubre

- `gammatone_numerical_stability`: ruido a ~-80 dBFS + impulso sostenido, sin NaN/Inf ni runaway.
- `no_denormals_low_level`: confirma salida finita y sin subnormales visibles en señales diminutas.
- `dsp_core_stability`: recorre los .cpp reales de `dsp/` (EQ, compresor, excitador, widener, gain stage).

## Build rápido

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Sanitizers

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DIVANNA_TEST_ENABLE_ASAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

cmake -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DIVANNA_TEST_ENABLE_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

## Resultado de esta pasada

- Release host suite: PASS
- ASan host suite: PASS
- TSan host suite: PASS
