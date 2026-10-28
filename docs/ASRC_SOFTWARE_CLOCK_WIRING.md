# IVANNA software ASRC — wiring and deployment boundary

## Decision

The Moto G85 does not need a discoverable hardware ASRC control for IVANNA to implement rate correction. The actuator can be a software resampler. The production-safe scope is the audio stream IVANNA itself owns; a software resampler in that path cannot retime arbitrary third-party app streams unless it is inserted into their shared audio path by a separately built/validated system effect or HAL.

## Existing app-owned playback path

The local-file player (`IvannaBridgePlayer`) already wires a software ASRC loop:

1. Decode PCM with `MediaCodec`.
2. Process the interleaved stereo chunk through `StereoAudioResampler.process()`. The resampler uses a 64-tap, 1024-phase windowed-sinc FIR, preserves fractional position/history between chunks, and exposes `setClockCorrectionPpm()`.
3. Write the resampled chunk to IVANNA's own `AudioTrack`.
4. Poll `AudioTrack.getTimestamp()` off the hot path. Use the timestamp's frame position together with cumulative frames accepted by this same `AudioTrack` to estimate queue occupancy.
5. `AudioTrackBufferPll.observe()` computes a bounded, slew-limited correction (±500 ppm; max 25 ppm per update) and `IvannaBridgePlayer` passes it to `StereoAudioResampler.setClockCorrectionPpm()`.
6. If timestamp retrieval fails, reset both controllers and force resampler correction to zero.

This is a software ASRC/queue-control loop for IVANNA's own player. The timestamp and frame counter are paired to that app-owned track; it is not a PAL/DAC clock read and does not control the Qualcomm HAL, other applications, or a Bluetooth sink's physical oscillator.

## PAL/HAL clock telemetry

`ivanna_pal_active_stream_clock_publisher.hpp` is a separate report-only bridge. A matching Motorola HAL source tree must bind its callback to the actual active `pal_stream_handle_t`; then IVANNA can receive paired PAL frame/timestamp samples. Those samples must not be fed into the app-owned AudioTrack PLL as if they described the same stream. To use PAL telemetry for another actuator, first establish that the PAL stream is the same stream being controlled and define a stable cross-layer ownership/identity contract.

## Native `VariableAsrc`

`app/src/main/cpp/dsp/variable_asrc.cpp` is a native variable-ratio component with bounded target ratio, slew control, and tests. It is not currently the active resampler in `IvannaBridgePlayer`; the live player path uses `StereoAudioResampler` and `AudioTrackBufferPll`. Do not report the native component as production-active merely because CMake compiles it.

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

Ratio convention is explicit: `appliedRatio` means **input frames consumed per output frame** by `VariableAsrc`. Therefore positive measured source-clock drift requests a ratio above unity; the opposite sign would increase output production and worsen a producer-ahead buffer. A configurable deadband suppresses corrections below measurement resolution, and the ratio correction is bounded and slew-limited. Any disabled, invalid, uncorrelated, or faulted route reports unity ratio.

This native controller is covered by host regression tests. It is not automatically the active player controller: `IvannaBridgePlayer` currently uses `AudioTrackBufferPll` and `StereoAudioResampler`. Do not connect both controllers to one actuator simultaneously. Device validation and explicit stream-identity wiring are still required before promoting the native controller into a live route.
