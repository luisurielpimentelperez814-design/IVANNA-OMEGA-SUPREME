# Moto G85 / Blair Qualcomm PAL clock integration

## What the extracted binaries establish

The device dump identifies these paths:

- `audio.primary.blair.so` imports `pal_get_timestamp`.
- `libpalclient.so` exposes a `pal_get_timestamp` thunk.
- `vendor.qti.hardware.pal@1.0-impl.so` contains `ipc_pal_get_timestamp` at the reported offset `0x1b5fc`.
- `libar-pal.so` contains `SessionAlsaPcm::getTimestamp` at the reported offset `0x24adc4`.
- `PayloadBuilder::payloadTimestamp` at the reported offset `0x2340bc` writes a 48-byte payload using `mixer_ctl_set_array`.
- The supplied `mixer_paths_blair_qrd.xml` has no explicit PLL/ASRC rate-adjustment control.

These facts establish a timestamp/driver IPC path, not a variable-ratio ASRC actuator. The `pal_get_timestamp` thunk and `ipc_pal_get_timestamp` are not, by themselves, a proof of a paired frame-position/timestamp sample or a frequency correction.

## Integration seam added

`ivanna_qualcomm_pal_clock_integration.hpp` connects the existing IVANNA components:

1. A device-specific query callback supplies a validated `HardwareClockSample`.
2. `HalOutputStreamClockControl` feeds the sample to the existing tracker and bounded rate servo.
3. A device-specific ASRC callback must apply the requested input-frames/output-frame ratio to the converter already in the output path and report whether it accepted the command.
4. An optional `HardwareClockPublisher` publishes the same paired sample to the existing shared-memory bridge for the effect-side reader.
5. The owner calls `controlTick()` from a serialized 10–20 Hz control task and `reset()` on standby, reopen, route changes, or discontinuities.

## Required target bindings (do not fake these)

- Implement `QueryPairedSample` from a source that gives the *matching* presented-frame counter and hardware-correlated monotonic timestamp. Do not decode the private `pal_session_time` structure by guessing its field offsets. The supplied evidence says it is 32 bytes but does not provide a verified field map. If PAL returns only a timestamp, pair it with the corresponding frame count from the same driver snapshot/query path; do not combine unrelated reads.
- Implement `ApplyExistingAsrcRatio` against the actual variable-ratio converter. Do not map this to `BT SLIMBUS CLK SRC`, `mixer_ctl_set_array`, or an unverified `pal_set_param` ID. A clock-source selector is not an ASRC ratio control, and a timestamp payload is not proof of a rate-adjustment command.
- Instantiate the integration object in the real, long-lived target output stream; open the optional reporter during stream setup; schedule ticks on the stream's serialized control thread; reset/stop it before teardown.
- The current dump does not establish the ASRC control API, its ratio units, or whether a paired PAL frame/timestamp query exists. Until those are verified from the actual call sites/parameter definitions, do not enable the correction callback on the phone.

## Build and validation gate

This header is a target-HAL integration seam, not a binary patch to the proprietary Motorola/Qualcomm HAL. Compile it in the target HAL build with the same Android toolchain, then test with a fake query/ASRC callback before wiring real hardware. On-device validation must record queried frames/timestamps, commanded ratio, converter acceptance, underruns, and route transitions. Do not claim hardware PLL/DAC oscillator discipline: this servo can compensate stream cadence through an existing ASRC, not directly retune the physical oscillator.
