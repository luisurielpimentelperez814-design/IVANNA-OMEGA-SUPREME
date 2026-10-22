# SAFMUS Plan 10D — Bluetooth acceptance gates

Status: v2.5.124 is the published software release. v2.5.125 is prepared in release PR #138 and is not published until exact-head CI and artifact verification pass. The selected-device route guard is merged; this follow-up binds Bluetooth latency profiling to the selected A2DP address, refreshes same-category endpoint changes, and rejects stale asynchronous results. Physical A2DP/USB acceptance remains open; this is not hardware certification.

## Implemented in this branch
- The owned `IvannaBridgePlayer` route selector gives a connected USB DAC priority; otherwise it selects A2DP/BLE and targets the selected Android output device.
- For A2DP, codec lookup requires a nonblank output address and exact address match (case-insensitive). Because `AudioManager.getDevices()` enumerates connected endpoints rather than identifying the active system route, the global route manager queries codec metadata only when exactly one A2DP endpoint is present; multiple endpoints or missing identity use the conservative profile. The detector itself fails closed when callers omit identity; it never substitutes the first connected headset. Recognized rates (44.1/48/88.2/96 kHz) are preferred; unknown/ambiguous rates fall back to 48 kHz.
- Source PCM is converted to the selected output clock before DSP/HRTF, so rate-dependent kernels process at the rate for which they are configured.
- The playback spatial fallback has isolated delay/history state from system-wide capture and scales ITD/crossfeed delay lines to the selected sample rate.
- The existing 64-tap/1024-phase FIR resampler and bounded AudioTrack timestamp servo remain app-owned. Timestamp failure resets correction to unity.
- `BluetoothAudioProfiler` now requires the uniquely selected A2DP address and exact proxy-device match. It does not select the first connected headset or infer the negotiated codec from the product name; ambiguous identity uses a conservative profile, and stale profile refreshes are invalidated on route-generation changes.
- JVM regression tests cover A2DP rate selection and spatial delay scaling.

## Explicit non-claims
- This does not insert an ASRC into Qualcomm PAL/AGM/A2DP vendor HAL and does not control the remote Bluetooth receiver oscillator.
- The AudioTrack timestamp describes the local app/platform playback clock; it is not proof of remote-sink clock recovery.
- This does not automatically process Spotify/YouTube or all third-party apps. System-wide behavior remains subject to Android effects, OEM routing/offload and Magisk integration.
- CI and synthetic tests do not replace listening tests, negotiated-codec verification, underrun/click/channel checks, and long-duration drift measurements on physical A2DP devices.

## Release gates
1. Android build and JVM tests pass on the exact PR head, including null/blank/mismatched/matched-address and multiple-connected-A2DP cases.
2. Verify the selected A2DP device, actual codec and sample rate in device logs.
3. Test SBC/AAC and a high-bitrate codec where hardware supports them; check for underruns, clicks, channel swaps and sample-rate churn.
4. Exercise Bluetooth ↔ USB-C ↔ speaker route changes and ensure stale codec lookups are discarded.
5. Validate system-wide A2DP/offload independently before claiming global Bluetooth ASRC.
6. Do not bump release metadata or claim hardware certification until the relevant gates are measured and documented.
