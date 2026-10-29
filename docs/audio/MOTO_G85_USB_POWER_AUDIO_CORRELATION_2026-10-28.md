# Moto G85 USB-C power ↔ audio timing correlation — v2.5.191

## Purpose

Connect observed USB-C power-management events to AudioFlinger timing telemetry through a repeatable, read-only capture. This is a diagnostic bridge, not a claim that IVANNA can command the physical DAC clock.

## Grounded device evidence (2026-10-28)

- AudioFlinger reports `AudioOut_D` at 48 kHz and `AUDIO_DEVICE_OUT_USB_HEADSET`.
- FastMixer reported `writeErrors=0`, a 4 ms period and 21 ms latency in the supplied snapshots.
- FastMixer corrected timestamp jitter was around 0.0086–0.0088 ms (8.6–8.8 µs) in the supplied samples.
- Kernel excerpts include `usbin-collapse`, `aicl-done` and `icl_settled=1500000`. These establish charger input-management activity, not that the events caused audio timing changes.
- A later AudioOut_D summary reported `rate=0.994115` and corrected jitter standard deviation `0.46727 ms`; an earlier pasted value was `0.0046727 ms`. The conflict is unresolved. Neither value is a calibrated DAC clock measurement.
- Android `dumpsys battery` showed `USB powered=false` and `vbus=false` while kernel `usb` power_supply showed `USB_PD online=1`, current around 1.26 A and voltage around 4.41 V. This vendor-layer discrepancy alone does not verify TurboPower pass-through.

## Correlation method

Use wall-clock time and uptime to align power telemetry with AudioFlinger counters. Compare **counter deltas** across equal-duration windows, not only cumulative totals. Preserve raw sysfs values unless the driver's units are verified; common Linux power-supply fields often use micro-units, but drivers can differ.

Synthetic data is acceptable for unit tests of the correlation math (e.g. a simulated collapse event beside a simulated underrun increase). Every generated row must be marked `SYNTHETIC`, excluded from device evidence, and never trigger a real hardware correction. Correlation is not causation.

## Fail-closed implementation contract

- Capture is read-only: do not write PAL/AGM parameters, change USB interfaces, or alter gain/clock correction.
- Keep the kernel `snd-usb-audio` route intact.
- Charger events are not a clock sensor. Never map them directly to an ASRC ratio.
- Keep app-owned AudioTrack SAFMUS correction scoped to its own valid paired frame/timestamp observations.
- A physical PAL/HAL actuator remains blocked until the active stream, paired hardware-correlated frame/timestamp source, parameter ID/payload/units, limits, and successful control response are proven.

## Release gate

This evidence bridge does not certify physical DAC clock lock or an audible quality improvement. The release is valid only after GitHub Actions builds and verifies the exact APK and Magisk ZIP for the release commit.
