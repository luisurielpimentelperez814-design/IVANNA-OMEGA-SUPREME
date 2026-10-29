# IVANNA software ASRC — wiring and deployment boundary

## Decision

The Moto G85 does not need a discoverable hardware ASRC control for IVANNA to implement rate correction. The actuator can be a software resampler. The production-safe scope is the audio stream IVANNA itself owns; a software resampler in that path cannot retime arbitrary third-party app streams unless it is inserted into their shared audio path by a separately built/validated system effect or HAL.

## Existing app-owned playback path

The local-file player (`IvannaBridgePlayer`) already wires a software ASRC loop:

1. Decode PCM with `MediaCodec`.
2. Process the interleaved stereo chunk through `StereoAudioResampler.process()`. The resampler uses a 64-tap, 1024-phase windowed-sinc FIR, preserves fractional position/history between chunks, and exposes `setClockCorrectionPpm()`.
3. Write the resampled chunk to IVANNA's own `AudioTrack`.
4. Poll `AudioTrack.getTimestamp()` off the hot path. Use the timestamp's frame position together with cumulative frames accepted by this same `AudioTrack` to estimate queue occupancy.
5. `AudioTrackBufferPll.observe()` now performs centered rolling least-squares regression over paired presented-frame/timestamp samples and combines the measured clock-rate feed-forward with the bounded buffer-occupancy PI term. The single output is limited to ±500 ppm and 25 ppm per update, then passed to `StereoAudioResampler.setClockCorrectionPpm()`.
6. If timestamp retrieval fails, reset both controllers and force resampler correction to zero.

This is a software ASRC/queue-control loop for IVANNA's own player. The timestamp and frame counter are paired to that app-owned track; it is not a PAL/DAC clock read and does not control the Qualcomm HAL, other applications, or a Bluetooth sink's physical oscillator.

## PAL/HAL clock telemetry

`ivanna_pal_active_stream_clock_publisher.hpp` is a separate report-only bridge. A matching Motorola HAL source tree must bind its callback to the actual active `pal_stream_handle_t`; then IVANNA can receive paired PAL frame/timestamp samples. Those samples must not be fed into the app-owned AudioTrack PLL as if they described the same stream. To use PAL telemetry for another actuator, first establish that the PAL stream is the same stream being controlled and define a stable cross-layer ownership/identity contract.

## Native `VariableAsrc`

`app/src/main/cpp/dsp/variable_asrc.cpp` remains a separately tested native variable-ratio component and is not the actuator in `IvannaBridgePlayer`. The app-owned live path uses `StereoAudioResampler` with one `AudioTrackBufferPll` controller; the daemon's native estimator is not fed into that app-owned loop, avoiding two controllers acting on the same resampler.

## Release gates

Before claiming system-wide correction:
- integrate the software ASRC at a real, shared audio-effect/HAL processing point with variable-frame buffering appropriate to that interface;
- ensure one coherent stream identity connects clock samples, resampler, and output owner;
- verify bypass bit-transparency and stable frame accounting across pause, flush, route changes, and EOS;
- pass native CTest and Android build, then test long playback on the Moto G85 for underruns, drift, discontinuities, and audible artifacts;
- keep any path that lacks a valid clock/route in unity-ratio bypass.

No hardware mixer control or undocumented ASRC parameter is required or assumed by this software design.

## Native clock estimator — mathematical contract

`AsrcClockController` now estimates the clock-rate slope from a fixed-size rolling window of paired cumulative frame positions and monotonic timestamps. It centers timestamps and frame positions before ordinary least squares to reduce numerical conditioning errors; it rejects regressing counters, impossible intervals, non-finite/out-of-range drift, and interval outliers. No allocation or I/O occurs in the update path.

Ratio convention is explicit: `appliedRatio` = rho = **input frames consumed per output frame** by `VariableAsrc` (rho > 1 emits fewer output frames). `driftPpm = 1e6*(f_sink/f_nominal - 1)` is measured on the presented-frame (sink) clock. A faster sink needs more output frames per input frame, so `appliedRatio = 1/(1 + drift*1e-6)`, i.e. **below unity for positive drift**. An earlier revision published `1 + drift*1e-6`; closed-loop measurement (sink +100 ppm: residual mismatch 208 ppm with that sign vs. ~100 ppm uncorrected) showed it amplified the error. `SinkDriftIsCancelledClosedLoop` and `InvertedSignWouldBeDetected` in `test_variable_asrc.cpp` pin the corrected sign. A configurable deadband suppresses corrections below measurement resolution, and the ratio correction is bounded and slew-limited. Any disabled, invalid, uncorrelated, or faulted route reports unity ratio.

The native daemon controller remains a separate telemetry/control-plane component. The app-owned path has its own rolling regression and buffer controller, driven exclusively by paired timestamps and frame positions from that same AudioTrack. Do not cross-feed PAL/daemon measurements into the app track. Device validation and explicit stream-identity wiring are still required before claiming system-wide or physical-clock correction.


## Reference-model validation

The repository root `sync_asrc.py` is the deterministic host reference for the
SAFMUS FIFO-occupancy plant, bounded PI/anti-windup control, adaptive two-state
clock estimator, Q0.32 phase stepping, Kaiser-windowed 64-tap/1024-phase ASRC,
clock-loop bandwidth selection, and GCC-PHAT sign convention. CI runs both
`python3 sync_asrc.py --self-test` and `tests/test_sync_asrc.py`.

This reference model is not itself an Android runtime actuator. The app-owned
`IvannaBridgePlayer` path remains controlled by `AudioTrackBufferPll` and
`StereoAudioResampler`, whose measured queue is the output AudioTrack queue,
not the ASRC input FIFO in the reference plant. Do not substitute one error
signal for the other: their control signs and plant dynamics differ. The
native `VariableAsrc` is separately tested and is not implicitly activated.
A production wiring change requires a single stream-identity contract joining
the measured input FIFO, its timestamp source, and the ASRC instance.

## USB output clock link — what was measured on the Moto G85 (capture 2026-10-28)

Evidence: `ivanna_capture_20261028_171142` (USB DAC "JieLi Technology USB Audio", full speed, ALSA card 0 / `pcm0p`,
Android 15, kernel 6.1.128). All figures below come from that dump; nothing here is simulated except where marked.

| Fact | Value | Consequence |
|---|---|---|
| Playback path | AudioFlinger `AudioOut_D` MIXER + FastMixer -> primary HAL -> `AUDIO_DEVICE_OUT_USB_HEADSET`, 48 kHz, PCM16, HAL frame count 192 | The stream owner is the audio HAL process, not PAL/AGM DSP |
| `/proc/asound/card*/pcm*p/sub*/status` | **absent on this kernel** (glob matched nothing on any card) | The kernel `owner_pid`/`hw_ptr`+`tstamp` route is not available; the daemon must stay fail-closed. Do not open `/dev/snd` PCM nodes |
| FastMixer timestamps | n=2,622,524, writeErrors=0, underruns=17, overruns=95 (~5.2 M cycles); jitter sigma ~0.137 ms, max 109 ms | The HAL timestamp is usable but has rare large spikes |
| AudioFlinger sink-rate statistics | `localSR` 48000.3 / 48000.5 Hz (+6..+10 ppm) vs `rate=0.999982` (-18 ppm) | The two AF estimators disagree in sign: USB sink drift is at most a few tens of ppm; no stronger claim is supported |
| `omega_shm` | 231 SELinux `write` denials in the 600-line tail | Fixed by the `adb_data_file mlstrustedobject` rule (v3.4) |

**Authoritative link for the USB clock = the app-owned AudioTrack timestamp** (`AudioTrack.getTimestamp`, which the HAL
fills from the USB stream position) feeding `AudioTrackBufferPll` -> `StereoAudioResampler.setClockCorrectionPpm`. That is
the only paired frames+timestamp source on this device that is bound to the stream the app owns. It is an app-side
measurement of the HAL presentation clock; it is **not** a reading of the DAC crystal. For an asynchronous USB DAC the host
sends frames at the rate fed back by the device, so the position tracks the device clock; for an adaptive DAC (probable for
this full-speed dongle, **not verified**: `stream0` was not captured) the device follows the host and there is nothing to
correct. `capture_usb_clock.sh` v2 now records `stream0` to settle this.

Estimator identification (reproducible: `tools/safmus/usb_drift_estimator_sim.py`, threshold fixed before running:
std < 5 ppm, max error < 12 ppm in steady state; observation model = the measured jitter above, **synthetic**):
the previous 8-point OLS (~2 s window) had std 34-54 ppm / max error 105-368 ppm, i.e. larger than the drift itself. The
current Theil-Sen median-of-pair-slopes over a ~30 s window (<= 40 sub-sampled points, <= 780 pair slopes, pre-allocated
buffers) gives std 1.3-1.9 ppm / max error <= 5 ppm. The feed-forward weight ramps from 0 to 1 over the first 20 s of window.
`AudioTrackBufferPllTest` pins this (including a negative control that the old estimator fails with 208 ppm error).
Not validated: behaviour on a real USB DAC over time (needs a new capture with the v2 script).

