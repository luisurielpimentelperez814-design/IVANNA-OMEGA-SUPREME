# Moto G85 USB playback / PAL clock evidence (2026-10-26)

## Scope and provenance

This note records the structured diagnostic summary supplied for a rooted Moto G85 5G (device `malmo`, Qualcomm platform `blair`), Android 15 / SDK 35. It is evidence inventory, not a claim that the HAL callsite or a physical ASRC actuator has been integrated. The underlying raw report sections for mixer controls and ALSA state were summarized rather than reproduced in full, so details not included below remain unverified.

## Device and library inventory

Reported device identity:

- Model: `moto g85 5G`
- Device: `malmo`
- Board platform: `blair`
- Hardware: `qcom`
- Android: 15, SDK 35
- Diagnostic shell: root / Magisk

Reported audio components include:

- `/vendor/lib64/libar-pal.so`
- `/vendor/lib64/libpalclient.so`
- `/vendor/lib64/libagm.so`
- `/vendor/lib64/libagmclient.so`
- `/vendor/lib64/vendor.qti.hardware.pal@1.0-impl.so`
- `/vendor/lib64/hw/audio.primary.blair.so`

The report identifies symbols including `pal_get_timestamp`, `pal_stream_get_mmap_position`, `StreamOutPrimary::GetMmapPosition`, `SessionAlsaPcm::getTimestamp`, and AGM timestamp functions. These establish available API/symbol candidates, not that IVANNA currently calls them for the active stream.

## Active route reported by AudioFlinger

The summary reports:

- Active AudioFlinger output thread: `AudioOut_D`, type `Mixer`.
- Thread mix rate: 48,000 Hz.
- HAL buffer: 192 samples.
- Active route: `AUDIO_DEVICE_OUT_USB_HEADSET`.
- Repeated `CFG_EVENT_CREATE_AUDIO_PATCH` events directed to the USB device.
- ALSA card 0 is reported as USB Audio (JieLi Technology); card 1 is reported as `blair-qrd-snd-card`, with multiple virtual/proxy DAIs.

These details identify an active USB output path at the AudioFlinger/policy level. They do not, on their own, map `AudioOut_D` to a particular `pal_stream_handle_t`, ALSA substream, or PAL session. The 48 kHz mix rate is not proof of the USB endpoint's measured physical clock rate. A 192-sample buffer at 48 kHz corresponds arithmetically to 4 ms of audio, but it is not necessarily the full end-to-end latency.

## What is and is not established

### Established by the supplied summary

1. PAL/HAL/AGM timestamp and MMAP-position APIs are present in the listed binaries.
2. AudioFlinger reports an active 48 kHz mixer route to a USB headset.
3. The platform exposes USB and Qualcomm ALSA cards.
4. Timestamp API availability is a viable investigation path for correlating frames and time.

### Not established yet

1. The exact PAL stream/session backing the active USB playback route.
2. A paired frame-position + timestamp sample obtained from that same active stream, including documented units and monotonic clock domain.
3. A verified ASRC/PLL parameter ID, payload layout, ratio units, and successful write path.
4. A physical sample-rate correction mechanism accessible to IVANNA.
5. End-to-end clock synchronization or drift correction on the real device.

## Required integration path

1. Identify the active USB playback stream by correlating AudioFlinger output/patch information with the HAL's actual stream handle and PAL session. Do not select a PCM by hard-coded card/device number.
2. At the owning HAL/PAL callsite, obtain frame position and timestamp from the same stream and preserve their units/clock-domain semantics.
3. Publish only fresh, monotonic, internally consistent samples through the existing IVANNA clock snapshot interface.
4. Reject missing, stale, duplicate, non-monotonic, implausible, or cross-stream samples. Keep ASRC neutral/fail-closed while the evidence gate is not satisfied.
5. Separately identify a documented or experimentally verified resampler actuator. Do not treat `BT SLIMBUS CLK SRC`, `devicePPMFC`, a timestamp payload, or a generic mixer control as an ASRC ratio without proof of semantics and successful control behavior.
6. Validate on-device with controlled playback, recorded frame/timestamp deltas, route changes, stop/start transitions, and CI tests. Record results without claiming physical correction until measured.

## Safety boundary

Do not open a second PCM from the daemon to imitate the active stream. Do not patch proprietary HAL instructions or write guessed mixer controls. The correct integration point is the actual owner of the active stream; if that callsite cannot be safely linked from the available source, retain fail-closed behavior and document the limitation.

## Relation to existing integration notes

This evidence supplements [MOTO_G85_PAL_CLOCK_INTEGRATION.md](../../hal_integration/MOTO_G85_PAL_CLOCK_INTEGRATION.md). It narrows the active route to USB headset at AudioFlinger level but does not close the missing HAL/PAL stream-owner callback or prove an ASRC actuator.
