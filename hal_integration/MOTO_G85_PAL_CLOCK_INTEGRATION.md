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

## Bluetooth Slimbus clock-source path confirmed (2026-10-21)

The additional Blair disassembly now connects the clock-selector symbols into one concrete path:

- `Bluetooth::configureSlimbusClockSrc()` at `0x1fa150` reads the Bluetooth Slimbus clock-source mapping. If no mapping is available, the observed fallback is integer `1`.
- It calls `Device::configureDeviceClockSrc("BT SLIMBUS CLK SRC", value)` (call site reported near `0x1fa1dc`).
- `Device::configureDeviceClockSrc(const char*, unsigned)` at `0x1f0c9c` passes the supplied name to `mixer_get_ctl_by_name()` and writes the supplied integer with `mixer_ctl_set_value(control, 0, value)`.
- `ResourceManager::getBtSlimClockSrc(unsigned)` is reported at `0x2ed074`; `ResourceManager::updateBtSlimClockSrcMap(unsigned,unsigned)` at `0x2ecc40` delegates to `std::map::operator[]`. This is key/value storage for the selector; it does not itself program a frequency or ratio.
- The supplied `mixer_paths_blair_qrd.xml` has no entry named `BT SLIMBUS CLK SRC`. Therefore PAL attempts to look up a named mixer control, but the provided XML does not establish that the control exists or that the write succeeds on this build.

This closes the **clock-source selection** path at the PAL code level. It does **not** close the IVANNA drift-correction path: the selector's integer is not shown to be an ASRC ratio, and there is no evidence that changing this selector continuously adjusts the output sample rate. Keep this path separate from `ApplyExistingAsrcRatio`; do not feed servo ratios into `BT SLIMBUS CLK SRC`.

The body of `Device::configureDeviceClockSrc` confirms the mixer lookup/write mechanics, including an index-zero write. Its error handling can return `-EINVAL` for a null name/invalid mixer context and `-ENOENT` when the named control is not found (one logging-dependent missing-control branch returns success, so a zero status alone must not be treated as proof that the control was applied). A successful call also does not prove the selected value's hardware meaning without the matching control definition/driver behavior.

## AGM and MFC boundary

`SessionAgm::setParameters(Stream*, int, unsigned int, void*)` at `0x251e04` has a branch for `param_size == 0x24` that reads a 32-bit word from the payload, advances the pointer by four bytes, and calls `agm_session_set_params(session_id, remaining_payload, first_word)`. This confirms an AGM parameter transport path, but the observed branch alone does not identify the parameter ID as a clock or ASRC command.

Likewise, `Session::updateCustomPayload` and `Device::updateCustomPayload` store staged payload bytes in object-owned buffers; staging is not itself proof that the DSP accepted a command. The MFC output-media-format payload discussed below configures media format; do not repurpose it as a live fractional-ratio actuator without a distinct, verified parameter contract.

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


## MFC evidence supplied from Blair dump and public ABI cross-check (2026-10-21)

The device investigation reports `MODULE_ID_MFC = 0x00010202`, `PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT = 0x00010204`, and a five-field `payloadMFCConfig` containing sample rate, bit width, q factor, data format, and channel count. Treat these as **device-dump hypotheses pending exact call-site validation**, not as a runtime ASRC ratio API.

A public AudioReach PAL implementation defines `PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT` as `0x08001024` and its documented payload as sampling rate, bit width, channel count, and channel map. That ABI differs from the five-int payload and ID reported for Blair. This may reflect a different PAL/AGM generation or an incorrect symbol/ID mapping; do not send either payload until the target binary's `payloadMFCConfig` call path, parameter header construction, and payload size establish the exact contract. Reference: https://github.com/AudioReach/audioreach-pal/blob/e5f86df5e1d1be4ce3e49f22735306ee5e146f8b/session/SessionAR/src/PayloadBuilder.cpp

MFC is a media-format converter and may contain resampling functionality, but `PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT` configures output media format; public definitions do not establish a continuously adjustable fractional ratio, update cadence, or safe live-update semantics. The controller must not feed servo corrections by repeatedly changing the nominal media format. First verify the exact Blair payload and whether MFC exposes a separate runtime ratio/drift control or another documented actuator. If no such control exists, use a purpose-built software ASRC in IVANNA's audio path rather than pretending the media-format parameter is a PLL knob.

The paired-time hypothesis is also recorded: `pal_session_time` reportedly contains `timestamp` and `frames` in one call, while AGM internal session time additionally exposes QTIMER/absolute time. Confirm field order and units against the target's actual ABI before compiling the callback; do not infer structure layout solely from log strings.


## Timestamp call-chain findings (2026-10-21, latest Termux excerpts)

The latest disassembly adds the following call-chain evidence:

- `Stream::getTimestamp(pal_session_time*)` is at `0x19d6c0`. It rejects a null output pointer, checks a state/availability field, locks a mutex, then dispatches through a function pointer loaded from the session object (`ldr x8, [x20, #0xc0]`; `blr x8` at `0x19d740`). This confirms the stream layer delegates timestamp retrieval to its active session implementation; the virtual target has not yet been resolved.
- A call site at `0x190a4c` invokes `Stream::getTimestamp` with an output pointer. The excerpt does not identify the caller's higher-level consumer or establish timestamp units.
- `SessionAlsaPcm::getTimestamp` at `0x24adc4` was previously observed forwarding to `SessionAlsaUtils::getTimestamp(mixer*, vector<int> const&, unsigned int, pal_session_time*)` via a PLT entry.
- The symbol entry at `0x2ec890` is a branch thunk to `0x256894`, not a readable function body at the symbol address. Disassembly at `0x256894` begins with a large stack frame and object/container initialization; the visible range includes `ResourceManager::getInstance()` and a loop over a stored collection, but does not yet show a verified timestamp source, output-field writes, or a `clock_gettime` call. The surrounding demangled label `std::__1::vector<...>::__vallocate` is not a reliable owning-function attribution for this code. Function boundaries and the branch-thunk target must be resolved before assigning semantics.
- The broad symbol grep found `clock_gettime` call sites around `0x21057c`, `0x210704` and `0x2107e4` in `SpeakerProtection` methods. These are unrelated call sites in the shown disassembly and must not be claimed as the PAL playback timestamp source.

### Current timestamp conclusion

The call chain is now clearer at the stream/session-dispatch level, but the actual implementation and `pal_session_time` ABI remain unresolved. Do not assume the output fields, ordering, units, monotonic-clock domain, or that frames and timestamp represent the same hardware snapshot. Next, resolve the indirect call at `0x19d740` against the stream/session vtable and inspect only the matching concrete `getTimestamp` implementation and its output writes. Keep the target callback unbound until a paired frame/timestamp sample is verified.

## Mixer-parameter lookup disassembly (2026-10-21)

The latest device disassembly adds the following bounded observations:

- The exported/thunk symbol `SessionAlsaUtils::setMixerParameter` at `0x2ec8e0` branches to `0x2579e8`.
- At `0x2579e8`, the routine receives four register arguments, calls `ResourceManager::getInstance()`, loads a global table/vector, and compares a 32-bit value from each entry with the second argument. The loop advances by `0x48` bytes per entry. This is evidence of an ID-to-entry lookup, not evidence of the ID's semantic meaning.
- A nearby disassembly block at `0x258264` calls `mixer_get_ctl_by_name`, followed by `mixer_ctl_set_enum_by_string` at `0x258270`. The enclosing demangled label is a large/inaccurate symbol range; do not assume this nearby enum-write block is the ASRC actuator or that it belongs to the same semantic operation without verifying the control-flow and call sites.
- Strings such as `setMixerParameter failed`, `Invalid mixer control`, `RAT_RENDER`, and `set devicePPMFC to match codec configuration for SCO` identify error/reporting and configuration paths, but do not reveal a verified variable-ratio control name, ID, payload ABI, or ratio units.

### Integration decision

No device-specific `ApplyExistingAsrcRatio` callback is added from this evidence. The generic IVANNA controller must remain unbound to an unverified mixer/DSP parameter. The missing requirements are still (a) a verified paired presented-frame/hardware-time query for this firmware and (b) a verified, existing variable-ratio ASRC actuator with known units and acceptance/error semantics. The public Android legacy HAL contract documents paired frame count and `CLOCK_MONOTONIC` timestamp semantics for `get_presentation_position`, but that does not establish that Motorola's proprietary PAL stream exposes or uses that legacy callback internally.
## Public AudioReach Engine lead: Dynamic Resampler / RAT (2026-10-21)

A deeper public-source search found a more relevant lead in Qualcomm's open AudioReach Engine, distinct from the unrelated PAL `ASR` (speech-recognition) parameter:

- `MODULE_ID_DYNAMIC_RESAMPLER = 0x07001016`.
- `PARAM_ID_DYNAMIC_RESAMPLER_CONFIG = 0x08001025`; payload fields are `use_hw_rs`, `dynamic_mode`, and `delay_type`. The public header describes dynamic resampling as a software-resampler mode; hardware resampler selection is a separate flag.
- `PARAM_ID_DYNAMIC_RESAMPLER_OUT_CFG = 0x08001034` carries a nominal output sampling rate. The public header marks both parameters as Calibration/RTC-capable.
- The AudioReach Engine also contains hardware-resampler driver extensions with a “force fractional” mode and drift-mode concepts. That is evidence the public engine has resampling machinery that can account for input/output clock drift in some supported configurations.

References:
- Dynamic Resampler API: https://github.com/Audioreach/audioreach-engine/blob/master/modules/processing/resamplers/dynamic_resampler/api/dynamic_resampler_api.h
- Hardware resampler extension: https://github.com/Audioreach/audioreach-engine/blob/master/modules/processing/resamplers/dynamic_resampler/inc/rs_driver_ext.h
- Rate Adapted Timer module API: https://github.com/Audioreach/audioreach-engine/blob/master/fwk/spf/modules/rat/api/rate_adapted_timer_api.h

**What this does and does not establish:** this is a credible module/API lead, not yet a valid Moto G85 actuator. The public Dynamic Resampler API exposes mode and output-rate configuration, not a documented per-tick fractional ratio payload. The force-fractional definitions belong to a lower-level driver extension and do not establish that an app/HAL can call it, that the relevant module is loaded in the `malmo` playback graph, or that this firmware supports runtime updates while a stream is active. The public engine's ARM resampler library path is not proof that this DSP module/ABI is present in the phone's proprietary `blair` firmware.

**Next verification on target:** inspect the real playback graph/configuration and available module/parameter metadata for `MODULE_ID_DYNAMIC_RESAMPLER` (and the matching firmware module implementation), then trace the target's runtime set-param path and confirm the active graph instance ID, payload ABI, supported update behavior, and returned status. Do not send guessed APM/GPR commands or change `PARAM_ID_DYNAMIC_RESAMPLER_OUT_CFG` every servo tick: that would change nominal format, not prove drift correction. Until target presence and runtime semantics are verified, keep `ApplyExistingAsrcRatio` unbound and correction disabled.

This narrows the next search to a concrete Qualcomm candidate, but it does not yet justify claiming a real hardware connection.
## Concrete PAL timestamp observer added from public mirror (2026-10-21)

Added `hal_integration/ivanna_pal_timestamp_observer.hpp`. It uses the public `pal_get_timestamp(pal_stream_handle_t *, pal_session_time *)` signature and decodes the documented `pal_time_us` high/low words into 64-bit microsecond fields: `session_time`, `absolute_time`, and `timestamp`. It is diagnostic-only and must be included only in a vendor HAL target that already owns a valid PAL stream handle and has PAL headers/linkage.

Public mirror evidence:
- `inc/PalApi.h` declares `pal_get_timestamp`.
- `inc/PalDefs.h` documents each `pal_time_us` as a 64-bit microsecond value and `pal_session_time` as three such values.
- In the public `SessionAgm` implementation, `getTimestamp()` is a no-op returning 0; the public `SessionAlsaPcm` path implements timestamp retrieval. Therefore even a valid API call is path-dependent, and success must be checked on the target.
- Public `SessionAlsaUtils::setMixerParameter` accepts a generic payload, but that generic transport does not define a variable-ratio ASRC command.

Why the new observer does NOT drive the servo: the public PAL timestamp structure contains no presented frame counter, and its fields are not documented as a paired `CLOCK_MONOTONIC` presentation timestamp. Converting PAL microseconds into a fake frame/time pair would invent a clock relationship and risk unstable correction. The observer therefore cannot enable correction and is deliberately separate from `QualcommPalClockIntegration`.

Remaining device-specific blockers are unchanged: (1) a paired frame-position and clock-domain timestamp source for the actual Motorola output path, and (2) a verified live variable-ratio ASRC actuator with units, accepted range, stream scope and error semantics. Do not mark the hardware integration complete until both are confirmed on the Moto G85 and exercised with playback tests.

## New device dump evidence — PAL timestamp mixer payload (2026-10-21, follow-up archive)

The uploaded `ivanna_hal_followup_20261021_061252.tar.gz` contains a disassembly of the installed `libar-pal.so` (SHA-256 `0a2eef24718b89a6379938f93b5bc8f693078c7225db0a75cf7753560d23fdeb`). This adds a concrete result beyond the earlier probe:

- The code at `0x256b04` calls `mixer_get_ctl_by_name`; at `0x256b38` it calls `PayloadBuilder::payloadTimestamp`; at `0x256b58` it writes the payload with `mixer_ctl_set_array`; and at `0x256d50` it reads it back with `mixer_ctl_get_array`.
- After a successful read, the code copies six consecutive 32-bit words from payload offsets `0x10, 0x14, 0x18, 0x1c, 0x20, 0x24` into the caller-provided output at offsets `0x00, 0x04, 0x08, 0x0c, 0x10, 0x14`. This establishes the six-word output layout for this concrete helper path; it is consistent with a 24-byte `pal_session_time` object, but does not independently establish field semantics, units or clock domain.
- The control name is constructed dynamically using a mapped table entry plus literal fragments before `mixer_get_ctl_by_name`. The follow-up archive's attempted `llvm-objdump -s` ranges did not overlap the relevant sections, so the final control name is still **not recovered**. Do not infer it from the demangled STL label.
- The payload path proves that PAL obtains a timestamp-like six-word structure through a mixer-control array. It does **not** show a presented frame counter, a paired frame-position/`CLOCK_MONOTONIC` sample, or an ASRC ratio write. The call to `mixer_ctl_set_array` here is for the timestamp payload; it must not be misidentified as an ASRC actuator.

### Updated decision

The output field offsets are now better supported than in the prior checkpoint, but the frame/time pairing and live variable-ratio ASRC actuator remain unverified. Keep the device-specific `QueryPairedSample` and `ApplyExistingAsrcRatio` callbacks unbound and the clock-correction servo disabled until the mixer-name lookup/callers and active playback graph are traced, and an actual variable-ratio control with payload ABI, units, update semantics and return status is confirmed. The uploaded probe also did not identify a confirmed actuator. No guessed mixer writes or DSP parameter IDs are permitted.


## ASRC integration status checkpoint (2026-10-21)

The host test suite now also exercises the existing rate-control contract at nominal rates of 44.1, 48, 88.2, 96, 176.4, 192, 352.8, 384 and 768 kHz. These tests validate software bounds and callback sequencing with a fake converter; they do **not** prove that the Motorola HAL or a connected USB DAC accepts a ratio change.

The Android application path in `AudioPipeline.kt` currently creates `AudioRecord` and `AudioTrack` at the same `SAMPLE_RATE`. Its direct USB path is an optional separate URB bypass. Neither path, by itself, supplies the hardware presentation-frame/timestamp pair and verified variable-ratio converter callback required by `HalOutputStreamClockControl`. Inserting a nominal-rate converter at ratio 1.0 would not fix physical clock drift, and changing only a software variable would not change the DAC clock.

Acceptance criteria for declaring the physical ASRC feature complete remain:
1. identify the active output-stream implementation for the installed build;
2. obtain a paired, validated presentation-frame counter and hardware-correlated timestamp from that stream;
3. identify the actual variable-ratio ASRC already in the output path, including units, limits and error handling;
4. bind both callbacks in the long-lived output-stream owner and schedule control outside the hard real-time write path;
5. run the native CI tests and build the APK and Magisk module from the same commit;
6. on-device playback-test internal output and USB-C DAC separately, checking applied-ratio telemetry, underruns and audio stability.

Until those steps are completed, this branch is an integration/test improvement, not a claim of completed device-level clock correction.

## Moto G85 probe results — 2026-10-22

The uploaded `IVANNA_PAL_PROBE.tar.gz` was inspected. It contains `report.txt` and `config_files_with_hits.txt`; it does not contain the expanded `report_deep.txt`.

Observed on the actual device:
- `ro.product.model=moto g85 5G`, `ro.product.device=malmo`, `ro.board.platform=blair`, Android 15 / SDK 35, probe executed as root.
- Vendor libraries present include `audio.primary.blair.so`, `libar-pal.so`, `libpalclient.so`, `libagm.so`, `libagmclient.so`, and `vendor.qti.hardware.pal@1.0-impl.so`.
- PAL/AGM binaries expose timestamp-related symbols including `pal_get_timestamp`, `SessionAlsaPcm::getTimestamp`, `agm_get_session_time`, and `agm_get_buffer_timestamp`.
- AudioFlinger reports active mixer output threads at 48 kHz. The `AudioOut_D` sample shows 192 HAL frames, 171 ms FastMixer latency, 27 underruns and 102 overruns in the captured counters; `AudioOut_15` shows 1,920 HAL frames and about 282 ms average threadloop write latency. These counters are point-in-time cumulative diagnostics, not evidence of ASRC control or a direct DAC-clock measurement.
- The probe's filtered mixer-control section returned no named clock/ASRC/ppm controls. Its configuration search found resource-manager and policy XML files, but the emitted excerpts do not identify a variable-ratio actuator or prove which DSP module instance is active in the playback graph.

### Decision after the real-device probe

The device confirms that the expected PAL/AGM stack and timestamp interfaces exist. It does **not** confirm (a) a paired presentation-frame/hardware-time sample in the active output path or (b) a runtime variable-ratio ASRC actuator with documented payload, units, limits and acceptance status. The PAL callbacks therefore remain unbound and hardware correction remains disabled. Do not route the servo into `BT SLIMBUS CLK SRC`, timestamp mixer payloads, or nominal media-format parameters.

The read-only probe was extended on `main` to capture additional AudioPolicy/AudioFlinger route context, audio-related properties, ALSA device listings, relevant resource-manager module/parameter lines, and a broader sample of mixer control names. Re-run the updated script and upload the new archive; it improves evidence collection but does not itself connect the HAL.
