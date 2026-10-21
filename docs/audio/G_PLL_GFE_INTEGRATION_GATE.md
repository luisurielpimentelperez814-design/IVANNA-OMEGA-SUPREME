# G-PLL + GFE integration gate

Status: **not enabled in the production AudioFlinger callback**.

## Verified repository boundary

`omega_effect.cpp` implements an AudioFlinger effect callback and receives PCM buffers. The repository search performed for this change found no `snd_pcm_t` owner, `snd_pcm_status_get_audio_htstamp`, `audio_htstamp`, or `AAudioStream_getTimestamp` integration. An AudioFlinger effect buffer is not itself an ALSA PCM handle. Calling ALSA status APIs from this callback without the actual PCM owner would fabricate a clock source and is prohibited.

## Safe implementation sequence

1. The PCM/HAL/AAudio owner must expose measured hardware-frame position and a monotonic audio timestamp from the API it actually owns. If only AudioFlinger timestamps are available, document their clock domain and semantics; do not label them DAC timestamps.
2. Publish a bounded, lock-free snapshot (sequence counter or equivalent) to the effect callback. The callback must never open devices, make blocking syscalls, allocate, wait, or touch IRQs.
3. Validate snapshot freshness, monotonicity, sample rate, frame counter, and discontinuities. Invalidate/reset the PLL on stale or inconsistent data; never synthesize a phase reference from wall-clock time.
4. Feed the PLL only measured phase error. Keep frequency pull bounded in ppm and separate from PCM sample-rate changes.
5. Add the GFE analytic-signal path (documented Hilbert/FIR latency and coefficients) before any complex phase operation. Never apply a complex rotation directly to real PCM.
6. Preserve a true bit-identical bypass: when bypass is active, copy input to output without GFE/PLL arithmetic or format conversion.
7. Gate activation on native tests, device loopback measurements, and a CSV trace containing measured timestamp/frame-position plus PLL state. Report THD+N and latency only from recorded measurements.

## Required release gates

- Native test: bypass output is bit-identical for every supported PCM format and buffer size.
- Native test: injected 10 ppm drift converges within 5 s and frequency-pull overshoot is at most 10%.
- Native test: enabling the analytic/GFE path does not worsen measured THD+N by more than 0.1 dB under the defined fixture.
- Device test: timestamp/frame-position samples are monotonic and valid for the selected output route.
- CI: configure, compile, and run the native suite successfully.

Until the PCM owner and measurements exist, keep the PLL disconnected from the live audio path. A software phase estimator cannot physically discipline a DAC clock by itself.

## HAL-fed hardware clock estimator scaffold added

`include/audio_hardware_clock.hpp` now implements `HardwareClockTracker`, which estimates output sample rate and drift from paired driver-reported presented-frame positions and hardware-correlated timestamps. Its native tests are registered as `test_audio_hardware_clock`.

**Important boundary:** this estimator is created but is not yet connected to a live hardware source. Repository search found no implementation of `get_presentation_position`, `audio_hw_device::out_write`, `AAudioStream_getTimestamp`, or `snd_pcm_status_get_audio_htstamp`. Therefore no caller can honestly supply these samples from this repository today. The HAL/PCM owner must call the tracker (or publish the same validated snapshot to the effect) using its actual API before the PLL can be enabled. Callback monotonic timestamps must not be passed as hardware timestamps.

## Hardware clock IPC and effect consumer added

The repository now contains:
- `include/audio_hardware_clock_bridge.hpp`: shared-memory publisher/reader between a HAL/PCM output owner and the effect callback.
- `include/audio_hardware_clock_hal_adapter.hpp`: adapter for a successful `get_presentation_position()`-style call, passing the driver-returned frame counter and its associated timestamp.
- `include/audio_hardware_clock.hpp`: bounded rate/drift estimator.
- `omega_effect.cpp`: maps the snapshot outside the callback, reads it in the callback without syscalls, and publishes validated hardware-rate/drift telemetry to the effect beacon.
- `tests/test_audio_hardware_clock_bridge.cpp`: IPC and adapter tests registered in native CMake.

**Remaining integration boundary:** this repository still does not include the device's `audio.primary.<device>.so` source or any implementation of `audio_stream_out::get_presentation_position()`, `AAudioStream_getTimestamp()`, or ALSA hardware status. Consequently, no in-repository component currently calls `HardwareClockPublisher::publish()` with real driver data. The effect-side consumer is wired, but the physical producer must be added to the target device's actual HAL/PCM owner and must be permitted to access `/data/local/tmp/ivanna_hw_clock_snapshot` under SELinux. Until that call site is integrated and measured on-device, hardware telemetry remains invalid and the G-PLL must remain disconnected from PCM/phase control. Do not synthesize producer samples from the callback clock.

## Software callback clock added

The AudioFlinger callback now feeds a per-effect `CallbackClockMonitor` using the callback's frame count and `CLOCK_MONOTONIC` observations. The monitor publishes observed callback cadence and drift through the existing 64-byte effect beacon, and `readEffectCallbackClockStatus()` exposes that diagnostic to the app.

This closes a **software callback-clock telemetry** path only. It does not measure the DAC oscillator, HAL hardware pointer, or the DAC's audio timestamp, and it is intentionally not fed into the geometric PLL as a phase reference. Multiple effect sessions can share the beacon, so this global diagnostic is not a per-device hardware clock. The PLL remains gated until the actual PCM/HAL owner supplies hardware-correlated frame/timestamp snapshots.
