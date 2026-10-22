# SAFMUS Plan 10D — Bluetooth acceptance gates

Status: SAFMUS Plan 10D software release published in v2.5.123. This follow-up closes ambiguous codec lookup when the caller has no selected-device address; physical A2DP/USB acceptance remains open. Android build, 252 JVM tests, host CTest/ASan+UBSan and release-artifact verification passed on the published main SHA. Physical A2DP/USB acceptance remains open; this is not a hardware certification.

## Implemented in this branch
- The owned `IvannaBridgePlayer` route selector gives a connected USB DAC priority; otherwise it selects A2DP/BLE and targets the selected Android output device.
- For A2DP, codec lookup requires a nonblank selected-output address and exact address match (case-insensitive). The detector itself now fails closed when callers omit that identity; it never substitutes the first connected headset. Recognized rates (44.1/48/88.2/96 kHz) are preferred; unknown/ambiguous rates fall back to 48 kHz.
- Source PCM is converted to the selected output clock before DSP/HRTF, so rate-dependent kernels process at the rate for which they are configured.
- The playback spatial fallback has isolated delay/history state from system-wide capture and scales ITD/crossfeed delay lines to the selected sample rate.
- The existing 64-tap/1024-phase FIR resampler and bounded AudioTrack timestamp servo remain app-owned. Timestamp failure resets correction to unity.
- JVM regression tests cover A2DP rate selection and spatial delay scaling.

## Explicit non-claims
- This does not insert an ASRC into Qualcomm PAL/AGM/A2DP vendor HAL and does not control the remote Bluetooth receiver oscillator.
- The AudioTrack timestamp describes the local app/platform playback clock; it is not proof of remote-sink clock recovery.
- This does not automatically process Spotify/YouTube or all third-party apps. System-wide behavior remains subject to Android effects, OEM routing/offload and Magisk integration.
- CI and synthetic tests do not replace listening tests, negotiated-codec verification, underrun/click/channel checks, and long-duration drift measurements on physical A2DP devices.

## Release gates
1. Android build and JVM tests pass on the exact PR head, including null/blank/mismatched/matched-address codec-selection cases.
2. Verify the selected A2DP device, actual codec and sample rate in device logs.
3. Test SBC/AAC and a high-bitrate codec where hardware supports them; check for underruns, clicks, channel swaps and sample-rate churn.
4. Exercise Bluetooth ↔ USB-C ↔ speaker route changes and ensure stale codec lookups are discarded.
5. Validate system-wide A2DP/offload independently before claiming global Bluetooth ASRC.
6. Do not bump release metadata or claim hardware certification until the relevant gates are measured and documented.
