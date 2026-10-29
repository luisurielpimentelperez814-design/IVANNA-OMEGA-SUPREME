# USB-C DAC clock experiment — phase 1 (observe-only)

## Goal

Use the Moto G85 + an attached USB-C DAC as an experiment before binding any proprietary Qualcomm PAL/HAL callbacks. This phase must establish evidence first; it does **not** claim to control the DAC's hardware clock.

## Current implementation facts

- `UsbAudioProManager.activateUnifiedDacPath()` inspects Android's exposed AudioStreaming endpoint descriptors and keeps the kernel `snd-usb-audio` route intact.
- `negotiateSampleRate()` estimates a rate ceiling from endpoint packet budget. That is **not** proof of the DAC's currently selected UAC clock or the active AudioFlinger output rate.
- `AudioTrackBufferPll` is explicitly scoped to IVANNA's own app-owned AudioTrack and resampler. It is not a physical-DAC, Bluetooth-sink, or vendor-HAL servo.
- The raw usbfs/URB route must remain disabled unless UAC alternate-setting, clock SET_CUR, endpoint format, packet scheduling, permissions, and teardown have been validated for the specific device. Do not claim the interface just to obtain timing data.
- The PAL/HAL report documents timestamp symbols and a Bluetooth Slimbus clock-source selector, but does not establish a paired hardware frame/timestamp query plus a verified variable-ratio ASRC actuator. Do not map a correction ratio to `BT SLIMBUS CLK SRC` or an unverified PAL/AGM parameter.

## Phase 1 acceptance criteria

1. Capture the connected DAC's VID/PID, interface/endpoint descriptors, endpoint packet budget and feedback endpoint type using Android's public USB descriptor API.
2. Independently capture the active output sample rate, presented frame position and monotonic timestamp from the same Android audio clock domain where available. Label any unavailable or estimated field explicitly.
3. Keep estimate, observed rate, and verified negotiated rate as separate values. A packet-budget ceiling or fallback 48 kHz must never be called a negotiated hardware rate.
4. Do not feed an estimate into a physical ASRC/HAL actuator. App-owned resampler correction may only use the app's own valid paired AudioTrack observations.
5. On detach, route change, standby, stale timestamp, or non-monotonic counters, clear observations and return app-owned correction to unity.
6. Validate on host/CI and then on the actual Moto G85 with a DAC attached. A green host build is not proof of on-device timing or sound quality.

## Phase 2 — PAL/HAL binding gate

Only proceed after the read-only probe and disassembly identify both (a) a trustworthy paired presented-frame counter and hardware-correlated monotonic timestamp, and (b) the real active variable-ratio converter's control contract, including parameter ID, payload layout, units, limits and acceptance/error semantics. Keep the hardware correction fail-closed until then.

## Evidence added in this branch

`hal_integration/tools/probe_moto_g85_pal.sh` now enumerates readable `/proc/asound/card*/pcm*p/sub*/status` files with shell-expanded globs and records available PCM playback/capture directories. This avoids writing a literal unmatched wildcard into the report. The probe remains read-only.

## Status

**Experimental / observe-only.** No verified hardware ASRC command, no claim of DAC clock discipline, and no claim of on-device validation.
