# cpp/tests/ — suite host-side del DSP (flanco Tests host/CTest)

Suite host-side que valida estabilidad numérica y regresión auditiva del motor
nativo sin depender del APK. **60 tests** en 10 ejecutables (GoogleTest
vendoreado en `third_party/googletest` — cero red, determinista en CI).

## Vía canónica: `scripts/run_ctest.sh`

```bash
bash scripts/run_ctest.sh              # Release, 60/60 en ~21 s
IVANNA_SAN=asan bash scripts/run_ctest.sh   # ASan+UBSan
IVANNA_SAN=tsan bash scripts/run_ctest.sh   # ThreadSanitizer (lento: 5-15x)
```

Corre en CI via [`.github/workflows/tests-host.yml`](../../../.github/workflows/tests-host.yml):
CTest + ASan+UBSan en cada push/PR que toque `app/src/main/cpp/` o `tests/`
(pasan en ~1 min cada pata); TSan en carril semanal+manual (en runners de 2
núcleos necesita >45 min — medido, no estimado).

> Historial: antes este script invocaba `cmake --preset host-release` con
> `CMakePresets.json` inexistente — la puerta estuvo muerta hasta que el
> flanco Tests host la reparó (commits `f11a60f2`, `f15cb090`).

## Qué cubre

| Ejecutable | Qué valida |
|---|---|
| `gammatone_numerical_stability` | ruido a ~-80 dBFS + impulso sostenido, sin NaN/Inf ni runaway |
| `no_denormals_low_level` | salida finita y sin subnormales visibles en señales diminutas |
| `dsp_core_stability` | recorre los .cpp reales de `dsp/` (EQ, compresor, excitador, widener, gain stage) |
| `test_regression_tuning` | bugs de los Parches de Tuning 6-9 (clipCount, gainReduction en dB, makeup) |
| `test_audio_quality_metrics` | SNR > 90 dB en bypass, THD < 10% en clipping, latencia < 1 ms/bloque |
| `test_harmonic_exciter_overshoot` | softClip Padé [3/2] satura hacia ±1 (bug de clipping a 1.94) |
| `test_limiter_hires_timing` | soft-knee y ataque/release reales a 48k/384k (bug 2026-08-27) |
| `test_audio_regression` | **regresión auditiva de campo**: cada TEST_F documenta un bug real (tronidos, clipping, NaN) con commit, síntoma y condición de reproducción |
| `test_ihr1_format` | barrera de regresión del lector IHR1 (datasets densos, ficheros truncados) |
| `test_shm_lifecycle` | ciclo completo del plano de control SHM contra backing file en `/tmp` |
| `test_oem_stability_suite` | regresión OEM: transparencia de mix, IIR estable, peak guard, stress de hilos |
| `test_adaptive_engine` / `test_rir_dataset` / `test_close_loop` / `test_stability` / `test_control_frame_bus_stress` / `test_audio_bus` | motor adaptativo, RIR, cierre de lazo, estabilidad y buses de control |

Huérfanos rescatados por el flanco Tests host: `test_audio_regression` (677
líneas) y `test_oem_stability_suite` no tenían target — la regresión más
importante del repo no se ejecutaba en ninguna parte. `test_adaptive_eq_stress`
sigue sin target deliberadamente: incluye `dsp/AdaptiveEQ.h` que no existe en
el árbol (ver nota en `CMakeLists.txt`).

## Build manual (equivalente al script)

```bash
cmake -S app/src/main/cpp/tests -B build/tests-host -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests-host -j
ctest --test-dir build/tests-host --output-on-failure
```

La suite es estable en paralelo (`ctest -j8`: 60/60) y en 5 corridas
consecutivas (cero flakiness verificada por el flanco Tests host).
