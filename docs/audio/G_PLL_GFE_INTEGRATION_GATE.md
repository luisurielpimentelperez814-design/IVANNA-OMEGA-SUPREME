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
