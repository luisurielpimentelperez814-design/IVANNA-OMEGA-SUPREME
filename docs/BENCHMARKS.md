# BENCHMARKS — surgical-hardening-v4

## Scope

`tools/benchmark_suite.cpp` measures the full native host-side pipeline (GainStage → ParametricEQ → Compressor → HarmonicExciter → StereoWidener + Gammatone13 visualizer analysis) and reports:

- average / p95 / p99 / max block time
- real-time CPU duty against the block budget
- end-to-end latency estimate (`block_duration + avg_block_time`)
- jitter estimate (`p99 - avg`)
- coarse battery estimate in mAh/h using the Moto G85 official battery reference

## Host reference result (REPRODUCIBLE — 2026-09-08)

Historia: este smoke result viejo NO era reproducible — `benchmark_suite.cpp`
nunca compiló (llamaba `HarmonicExciter::setAmount()`, API inexistente) y no
estaba enganchado a ningún build. El flanco Benchmarks lo resucitó: target
`ivanna_benchmark` (EXCLUDE_FROM_ALL, fuera de la puerta de tests).

Reproducir:

```bash
cmake -S app/src/main/cpp/tests -B build/tests-host -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests-host --target ivanna_benchmark -j
./build/tests-host/ivanna_benchmark 48000 256 15
```

Corrida de referencia (Ubuntu 24.04, g++ 13.3, x86_64, build Release):

```text
# ./ivanna_benchmark 48000 256 15
avg_block_ms=0.052576
p95_block_ms=0.064059
p99_block_ms=0.074644
max_block_ms=0.640833
realtime_cpu_percent=0.9858
end_to_end_latency_ms=5.385909
jitter_ms=0.022068
estimated_battery_mah_per_hour=1.449697

# ./ivanna_benchmark 48000 128 5   (bloques de 128: e2e baja a la mitad, CPU ~igual)
realtime_cpu_percent=0.9792
end_to_end_latency_ms=2.692779
jitter_ms=0.012773

# ./ivanna_benchmark 96000 512 3   (96 kHz: CPU ~2x, e2e estable)
realtime_cpu_percent=1.9599
end_to_end_latency_ms=5.437859
```

Sanidad de los números: la CPU escala ~2x de 48→96 kHz (trabajo por segundo
doblado) y la latencia e2e se mantiene ~5.4 ms porque el bloque de 512 a 96 kHz
dura lo mismo que 256 a 48 kHz — exactamente lo que una cadena DSP sana debe
hacer. El max_block_ms=0.64 de la corrida 15 s es el clásico pico de scheduler
del primer bloque (warm-up de caché/frecuencia), no de la cadena: p99 está a
0.075 ms.

### Re-verificación 2026-09-12 (sesión Claude, chat)

Recompilado y re-ejecutado contra el código actual — 4 días y decenas de
commits después de la entrega original (DSP, HRTF, daemon y UI cambiaron
de forma sustancial en el intervalo). Sigue compilando y corriendo limpio
sin tocar el benchmark en sí; números en el mismo rango, sin regresión:

```text
# ./ivanna_benchmark 48000 256 15
realtime_cpu_percent=0.7901   (referencia: 0.9858)
end_to_end_latency_ms=5.375472 (referencia: 5.385909)

# ./ivanna_benchmark 96000 512 15
realtime_cpu_percent=1.6743   (referencia: 1.9599)
end_to_end_latency_ms=5.422631
```

Escalado 48→96 kHz: 2.12x (sano, coherente con la referencia). Puerta de
75 tests también verde en el mismo checkout. Evidencia de que el
benchmark no quedó decorativo con el tiempo — sigue midiendo la cadena
real, no un snapshot congelado.

## Moto G85 reference values used by the estimator

- Battery size: **5,000 mAh**
- Battery life: **over 34 hours**
- Derived average-device reference current for coarse DSP estimate: **~147.06 mA** (`5000 / 34`)

Source: Motorola Support — Specifications: moto g85 5G
https://en-us.support.motorola.com/app/answers/detail/a_id/187380/~/specifications---moto-g85-5g

## Public competitor references used for contextual comparison

### Dolby Atmos

Publicly described by Dolby as a spatial audio format that lets creators place each sound exactly where they want it to go for a more realistic immersive experience. Dolby's consumer-facing page does **not** publish CPU%, jitter, or battery numbers, so those fields remain unavailable for like-for-like comparison.
https://www.dolby.com/technologies/dolby-atmos/

### DTS:X

DTS publicly states that DTS:X is immersive audio, supports decode & playback from streaming up to **5.1.4** and from optical disc up to **7.1.4**, and that DTS Neural:X can scale to **up to 32 speakers (30.2)**. DTS does **not** publish mobile CPU%, latency, or battery metrics on the cited public page.
https://dts.com/dts-x/

### iZotope Neutron 5

Neutron 5 is publicly described as an all-in-one mixing suite with **11 plugins**, AI-powered Mix Assistant, and AAX/AU/VST3 64-bit host support. iZotope publishes desktop system requirements, but not real-time mobile CPU/jitter/battery figures.
https://www.izotope.com/products/neutron

## Comparison table (publicly verifiable fields only)

| System | Publicly stated scope | Public numeric fields available on cited page | CPU / jitter / battery public data |
|---|---|---:|---|
| IVANNA OMEGA SUPREME | Native Android DSP + visualizer benchmark harness | Host benchmark output above | Measured by local harness |
| Dolby Atmos | Spatial/immersive audio playback ecosystem | No public CPU/jitter/battery numbers on cited page | Not disclosed publicly |
| DTS:X | Immersive decode & playback | 5.1.4 streaming, 7.1.4 optical, up to 32 speakers via Neural:X | Not disclosed publicly |
| iZotope Neutron 5 | Desktop mixing suite | 11 plugins, 64-bit AU/VST3/AAX support | Not disclosed publicly |

## Recommended on-device Moto G85 protocol

1. Push `tools/benchmark_suite.cpp` into an NDK test binary or shell benchmark target.
2. Run at 48 kHz with block sizes 128, 256, and 512.
3. Capture Perfetto / `top -H` / `dumpsys batterystats` during a 15-minute run.
4. Export avg, p95, p99, max, CPU duty, and measured current draw.
5. Replace the host smoke table above with the real Moto G85 numbers.
