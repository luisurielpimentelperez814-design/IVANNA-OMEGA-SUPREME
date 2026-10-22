# SAFMUS Plan 10D — Bluetooth acceptance gates

Status: implementation in review; not a release certification.

## Implemented in the app-owned playback path
- `IvannaBridgePlayer` checks the connected output before decoding and prefers USB when a USB DAC is connected.
- When Bluetooth is selected, it prefers the sample rate reported by the active A2DP codec (44.1/48/88.2/96 kHz), with a 48 kHz fallback for unavailable/unsupported rates.
- The selected Bluetooth output device is passed as the preferred `AudioTrack` device. Codec lookup is matched to that device address where Android exposes it.
- The existing 64-tap/1024-phase `StereoAudioResampler` and bounded `AudioTrackClockDriftServo` are used in this app-owned path. Timestamp failure resets correction to unity.
- Route-specific SAFMUS DSP profile lookup is being made asynchronous with generation checks in the companion PR, so an A2DP proxy lookup cannot block the route callback or apply stale results after a route change.

## What this does not claim
- It does not insert a sample-rate converter into Qualcomm PAL/AGM/A2DP vendor HAL.
- `AudioTrack` timestamps describe the app/platform playback clock; they do not expose the remote Bluetooth receiver's physical oscillator. The servo must not be described as remote-sink clock recovery.
- It does not automatically process all third-party apps. System-wide effect processing remains subject to Android's effect insertion, OEM routing/offload and Magisk integration.
- Synthetic FIR tests are not a substitute for listening tests, packet/xrun telemetry, codec-rate verification and long-duration drift measurements on real Bluetooth headsets.

## Release gates
1. Android build and JVM tests pass on the exact PR head.
2. A2DP codec and sample rate are logged from the selected device; unknown metadata falls back safely.
3. Exercise SBC/AAC and a high-bitrate codec when hardware is available; verify no repeated underruns, clicks, channel swaps or unexpected sample-rate churn.
4. Verify route transitions Bluetooth ↔ USB-C ↔ speaker and reject stale codec lookup results.
5. Separately validate system-wide A2DP/offload behavior on the target rooted device before claiming global Bluetooth ASRC.
6. Keep release metadata/version unchanged until all required CI jobs pass; hardware-only gates remain explicitly pending until measured.
