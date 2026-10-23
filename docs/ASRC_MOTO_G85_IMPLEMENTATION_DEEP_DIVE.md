# IVANNA Adaptive SRC — Moto G85 integration and verification plan

**Scope:** software sample-rate conversion and clock-drift correction for IVANNA OMEGA SUPREME on Moto G85 5G / Android 15 / Qualcomm blair platform.

**Status:** source-level implementation exists; device-wide HAL/PAL actuation is not verified. This document separates app-owned conversion from vendor hardware control.

## Executive decision

Do not create a third unrelated resampler. Reuse and harden the existing routes:

- StereoAudioResampler.kt is the app-owned interleaved stereo FIR SRC used by IvannaBridgePlayer.
- AudioTrackBufferPll.kt estimates queue phase from submitted frames and AudioTrack timestamp data, then changes the app-owned resampler ratio.
- usb_audio_pro_manager.cpp contains a distinct native USB path with a polyphase FIR converter and USB endpoint/feedback logic. It is not automatically the same route as Android ordinary AudioTrack output.
- hal_integration/ivanna_qualcomm_pal_clock_integration.hpp is a target integration seam, not a live Moto G85 binding.

The device's tinymix list had 196 controls and no names matching the searched ASRC/SRC/rate/clock terms. This means no obvious public mixer actuator was found; it does not prove the silicon or internal DSP lacks conversion. Vendor PAL can own a converter without publishing a tinymix control.

## Signal and clock model

Let F_in be input sample rate in input frames/second; F_out the desired output sample rate in output frames/second; r_nom = F_in / F_out input frames consumed per output frame; epsilon_ppm the bounded software correction in parts per million.

The adjusted input-frame step per output frame is r = r_nom / (1 + epsilon_ppm × 10^-6). The output-frame count over a sufficiently long, settled interval is approximately N_out = N_in × F_out / F_in × (1 + epsilon_ppm × 10^-6). Startup/filter history and finite-block boundaries mean an individual chunk is not required to match this formula exactly. Frame accounting must be checked cumulatively over multiple blocks.

Units are explicit: rates are Hz (frames/s), frame positions are frames, timestamps are monotonic nanoseconds, and ppm is dimensionless scaled by 10^-6. A frame is one time instant containing all channels; do not confuse stereo samples with stereo frames.

A 64-tap FIR has nominal linear-phase group delay of about 31.5 input samples (documented approximately as 32). At 48 kHz this is about 0.667 ms; at 44.1 kHz about 0.714 ms. This is only the filter algorithmic group delay, not total Android output latency.

The resampler correction bound of ±500 ppm corresponds to ±0.05% rate change. This is a conservative servo envelope, not a promise that the hardware clock itself has been changed. The current PLL output is a software ratio request only.

## Route boundaries

### A. IVANNA-owned file playback

MediaExtractor/MediaCodec → PCM → DSPBridge / owned DSP path → StereoAudioResampler → AudioTrack → Android AudioFlinger/HAL → output

This is the route where IVANNA can guarantee that its software resampler processes the PCM it owns, subject to verifying the exact call order and active code path for the build. The AudioTrack timestamp is not a physical acoustic loopback measurement and does not identify the remote Bluetooth DAC oscillator.

### B. USB-specific native engine

Producer → SPSC ring → USB endpoint / feedback scheduling → native USB ASRC → USB device

This is a separate and sensitive route. USB Audio Class endpoint feedback and allowed packet frame counts must be interpreted according to the actual device descriptor and endpoint mode. A generic ±1 frame policy is not universally valid for every endpoint interval, speed, sample rate, and format. Keep the engine disabled unless interface claiming, endpoint selection, packet sizing, feedback decoding, and teardown are verified against a real DAC. Never assume AudioTrack and direct usbfs streams can own the same USB interface simultaneously.

### C. System-wide / vendor HAL

Third-party app → AudioFlinger policy/offload → vendor HAL/PAL/AGM → DSP/codec → physical output

A library inside IVANNA cannot intercept this chain merely by being present in the APK. System-wide processing requires a supported Android audio effect route, or a controlled root/system integration with the correct ownership, SELinux policy, ABI, lifecycle and rollback. PAL clock correction additionally needs both a real paired hardware frame/timestamp source and a verified variable-ratio actuator in the active output graph.

Do not route a ratio value to BT SLIMBUS CLK SRC, generic mixer_ctl_set_array, a timestamp payload, or an unverified pal_set_param ID. Clock-source selection, timestamp transport, media-format configuration, and variable-ratio ASRC are different contracts.

## Stability and safety requirements

1. **Single owner:** one active resampler/clock servo per PCM path. Do not run Kotlin SRC, native USB SRC, global effect SRC, and a HAL SRC serially on the same samples unless the rate-conversion chain is explicitly designed and measured.
2. **Fail closed:** missing, stale, unpaired, non-monotonic or impossible timestamps reset software correction to unity/zero correction. Never infer a hardware sample from unrelated counter and timestamp reads.
3. **Bounded control:** preserve ±500 ppm absolute correction and 25 ppm maximum slew per update until device measurements justify a reviewed change. Clamp integral state and reject invalid buffer occupancy.
4. **No audio-thread blocking:** coefficient tables are prepared off the render thread; no file I/O, binder calls, mutex waits, dynamic table construction or logging storms in a hard real-time callback.
5. **Finite data:** NaN/Inf must not contaminate filter history or downstream limiter state. Keep bit-transparent unity bypass only for finite input and exact unity ratio.
6. **Gain and clipping:** resampling itself should not introduce uncontrolled gain. Verify per-phase DC normalization, peak bounds, denormals and non-finite behavior. Do not use an aggressive limiter to mask instability.
7. **Route changes:** reset filter history, PLL state and telemetry on sample-rate changes, pause/standby, track recreation, device removal and discontinuities.
8. **Rollback:** hardware correction stays disabled until the target binding is proven. A feature flag must be able to return to the known Android route without changing mixer controls.

## Regression gates added with this branch

StereoAudioResamplerAsrcControlTest.kt adds JVM coverage for extreme ppm values being clamped equivalently to ±500 ppm; NaN correction failing back to nominal/unity behavior; correction sign affecting output frame count in the expected direction; cumulative frame accounting across irregular chunk sizes; non-finite PCM not poisoning current or following blocks; and left/right channel isolation.

These are software contract tests only. They do not certify frequency response of the physical DAC, Bluetooth receiver clock, Android global path, or Qualcomm PAL integration. Existing fidelity tests remain responsible for passband and alias rejection.

## Moto G85 acceptance protocol

1. Record model, Android build fingerprint, app commit SHA, route/device ID, AudioTrack sample rate, channel mask, encoding, buffer capacity and timestamp availability.
2. Play deterministic 997 Hz and 18 kHz test tones plus silence-to-tone transitions through IVANNA's own player. Keep safe volume and compare correction disabled vs enabled.
3. Run at least 10 minutes per route. Record timestamp deltas, PLL lock state, correction min/max, queue occupancy, underrun/overrun counters and track recreation events.
4. Verify no clicks, pitch modulation, channel leakage, clipping, unexpected gain, or growing drift. Do not infer analog THD, DAC frequency accuracy, or total acoustic latency from a digital timestamp alone.
5. For Bluetooth, log negotiated codec and sample-rate evidence when the platform exposes it; the local AudioTrack timestamp is not a remote sink clock timestamp.
6. For USB, capture descriptors, endpoint direction/type, bInterval, wMaxPacketSize, sample format, feedback endpoint format, feedback measurements, ring occupancy and actual packet frame counts. Verify the active interface owner before enabling direct usbfs.
7. Run hal_integration/tools/probe_moto_g85_pal.sh read-only as root and preserve its report. The output is discovery evidence only, not proof of a live actuator.
8. Only enable a PAL callback after code-level evidence identifies (a) a paired frame/timestamp source from one coherent snapshot and (b) the actual active variable-ratio converter, parameter contract, ratio units and accepted-value readback. Then build the target HAL, run host tests and confirm telemetry on device.
9. Test route changes, unplug/replug, pause/resume, screen off, app backgrounding, process death and reboot rollback. A failed callback must leave the stock route intact.

## Current evidence table

| Claim | Status |
|---|---|
| Software FIR SRC exists in IVANNA player | Source-confirmed |
| App-owned buffer PLL adjusts that software SRC | Source-confirmed; on-device acceptance still required |
| A separate native USB ASRC implementation exists | Source-confirmed; real USB path/descriptor acceptance is route-specific |
| Mixer exposes an obvious ASRC control | Not found in the reported 196-control enumeration |
| Qualcomm PAL timestamp symbols/transport exist | Evidence described in repository investigation; does not prove paired frame/timestamp semantics |
| Variable-ratio PAL/HAL ASRC actuator identified | Not established |
| IVANNA changes physical DAC clock | Not established; do not claim |
| All third-party app audio is processed by this software SRC | False without a separate system-wide effect/HAL integration |
| Physical latency, THD, alias rejection and subjective improvement on Moto G85 | Pending controlled device measurement |

## Next implementation gate

The next code change should bind target PAL callbacks only after the device report and relevant binary/source evidence establish their contracts. Until then, strengthening tests, diagnostics, and route ownership is productive; guessing a control ID is not.