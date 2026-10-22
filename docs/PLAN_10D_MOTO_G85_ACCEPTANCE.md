# Plan 10D — acceptance gate for Moto G85

This file separates source-level wiring from evidence that requires the physical phone. It is not a claim that the phone test has passed.

## Verified from source

- The FotoHolo UI opens `RoomArCaptureActivity`; the Activity is declared in `AndroidManifest.xml`.
- The AR scanner owns an ARCore session, renders the camera texture, exposes capture start/finish/export, and labels room geometry as an estimate rather than certified acoustics.
- `AudioTrackBufferPll` adjusts only the app-owned `StereoAudioResampler` in `IvannaBridgePlayer`. Its correction is bounded and reset to unity when timestamps or queue estimates are invalid.
- The SAFMUS Camera2 adapter requires an already-owned Camera2 session and explicit readback. ARCore does not expose its internal camera session as a public Camera2 owner, so this adapter must not be called with a fabricated session.
- Qualcomm PAL clock integration remains a target binding seam. No verified Moto G85 ASRC ratio actuator or paired hardware frame/timestamp callback is established by source inspection.

## Required on-device evidence (Moto G85, Android 15)

Run from Termux with the phone unlocked and the route under test active. Capture each route separately: speaker, wired USB-C DAC/adapter, and Bluetooth A2DP if available.

1. Install the APK and Magisk module built from the same green CI commit. Record the commit SHA and APK/module SHA-256.
2. Open FotoHolo → **ABRIR AR & ESCÁNER RP-88 · CÁMARA 6-DoF**. Grant camera permission only when asked. Confirm live camera frames, tracking state, capture start/finish, ZIP export, and no crash on pause/resume. Record any ARCore unsupported-device message.
3. Play a local test track through IVANNA's own player for at least 10 minutes per route. Confirm timestamps advance, PLL telemetry locks after valid samples, correction remains within ±500 ppm, and underrun count does not increase abnormally. Repeat with a different sample rate and after switching routes.
4. Compare with correction disabled/unity to check for clicks, pitch change, channel imbalance, instability, and latency regression. A green build alone is not an acoustic pass.
5. For HAL/PAL, run `hal_integration/tools/probe_moto_g85_pal.sh` as documented in `MOTO_G85_PAL_CLOCK_INTEGRATION.md`. Keep the report read-only. Do not write mixer controls or guess a PAL parameter ID. Enable a hardware callback only after its paired sample source, ASRC actuator, payload and ratio units are identified and read back.
6. Attach logs, route, Android build fingerprint, commit SHA, test duration, underrun deltas and exported scanner ZIP to the release acceptance record.

## Release gate

Do not mark Plan 10D physically complete unless all relevant route tests above pass on the actual phone and a reviewer can inspect the evidence. If a vendor HAL callback is unavailable, report that limitation explicitly; app-level software ASRC is not a substitute for a hardware-clock claim.
