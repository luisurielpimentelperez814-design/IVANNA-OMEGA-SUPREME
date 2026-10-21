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

## Follow-up disassembly evidence (2026-10-20)

A further `llvm-objdump` excerpt shows this instruction sequence:

- call `PayloadBuilder::payloadTimestamp(...)`
- load the resulting buffer and byte count
- call `mixer_ctl_set_array(...)`

This strengthens the conclusion that a timestamp payload is serialized and sent to a mixer control. The excerpt's enclosing demangled label is `std::__1::vector<...>::__vallocate`, which is suspicious for this call sequence; verify the function boundary/call-site attribution before treating that label as the owning PAL method. Either way, this is not evidence of a variable-ratio ASRC command: no ratio argument or verified ASRC parameter ID is shown.

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

## How to obtain the missing device evidence (read-only probe)

`hal_integration/tools/probe_moto_g85_pal.sh` collects, from the real phone, exactly the facts this seam still needs. It only reads: it lists PAL/HAL libraries, dynamic symbols and strings that mention timestamps or ASRC/slip/rate-adapter modules, matching graph/mixer config files, mixer control names, and the AudioFlinger output-thread state. It does not write to the system, change mixers, or open streams.

```sh
# Termux, with root (optional: pkg install binutils for symbols/strings)
su -c "sh /path/to/probe_moto_g85_pal.sh"
# -> /sdcard/Download/ivanna_pal_probe_<date>/report.txt  (+ .tar.gz)
```

The report ends with an automatic reading. Host-tested in the sandbox with synthetic libraries (candidates found / nothing found / timestamp but no ASRC); its output on the real Moto G85 has **not** been collected yet. Evidence from the probe is a lead, not proof: the ASRC module must still be confirmed in the playback graph, with its parameter ID, payload and ratio units, before the correction callback is enabled.

## Build and validation gate

### Host test status (what is and is not verified)

`app/src/main/cpp/tests/test_qualcomm_pal_clock_integration.cpp` (10 tests, clean under ASan+UBSan) exercises the seam with a **fake** PAL query and a **fake** ASRC. Verified in host only:

- a missing query callback, a missing hardware timestamp, or a sample rate outside 8–768 kHz is rejected, and such samples can only ever drive the ASRC back to the unity ratio (1.0), never to a correcting ratio;
- three valid paired samples lock the servo and command the ASRC; a later query failure, ASRC rejection, or `reset()` drops correction (fail-closed, ratio marked unknown on rejection);
- the optional shared-memory reporter publishes exactly the sample the controller used, never publishes an invalid one, and its absence or failure does not disable local control.

Mutation-checked: removing the timestamp check, widening the rate bound, or publishing before validation each makes a test fail.

**Not verified:** the real Motorola/Qualcomm HAL, any real ASRC, the `pal_session_time` layout, or any on-device behavior. The two target callbacks remain unbound.

This header is a target-HAL integration seam, not a binary patch to the proprietary Motorola/Qualcomm HAL. Compile it in the target HAL build with the same Android toolchain, then test with a fake query/ASRC callback before wiring real hardware. On-device validation must record queried frames/timestamps, commanded ratio, converter acceptance, underruns, and route transitions. Do not claim hardware PLL/DAC oscillator discipline: this servo can compensate stream cadence through an existing ASRC, not directly retune the physical oscillator.


## Public PAL API cross-check (2026-10-20)

A public AudioReach PAL code search found a use of `PAL_PARAM_ID_ASR_CONFIG` passed to `pal_stream_set_param()`, with a payload typed `pal_asr_config`. This is **ASR** (automatic speech recognition) configuration, not evidence of an **ASRC** (asynchronous sample-rate converter) ratio actuator; do not reuse that parameter ID for clock correction. The public search did not identify a verified variable-ratio control API applicable to the Moto G85/Blair build.

The next decisive evidence must come from the target audio graph/configuration or its matching headers: identify the converter module actually present in the playback graph, its documented parameter ID/payload and ratio units, and the call site that applies it. Then verify that the graph supports runtime ratio updates while streaming. Until those items are grounded, the PAL callback remains intentionally unbound and hardware correction remains disabled.
