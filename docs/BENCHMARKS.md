# BENCHMARKS — surgical-hardening-v4

## Scope

`tools/benchmark_suite.cpp` measures the full native host-side pipeline (GainStage → ParametricEQ → Compressor → HarmonicExciter → StereoWidener + Gammatone13 visualizer analysis) and reports:

- average / p95 / p99 / max block time
- real-time CPU duty against the block budget
- end-to-end latency estimate (`block_duration + avg_block_time`)
- jitter estimate (`p99 - avg`)
- coarse battery estimate in mAh/h using the Moto G85 official battery reference

## Host smoke result (reference only, not an on-device Moto G85 run)

```text
IVANNA benchmark suite
sample_rate_hz=48000
block_frames=256
duration_s=15
avg_block_ms=0.027889
p95_block_ms=0.032732
p99_block_ms=0.043148
max_block_ms=0.066399
realtime_cpu_percent=0.5229
end_to_end_latency_ms=5.361223
jitter_ms=0.015258
estimated_battery_mah_per_hour=0.769009
```

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
