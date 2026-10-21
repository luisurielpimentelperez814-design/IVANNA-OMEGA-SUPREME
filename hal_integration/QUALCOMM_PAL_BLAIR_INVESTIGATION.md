# Qualcomm PAL / Blair timestamp investigation

## Scope

This note records observations transcribed from the user's Moto G85 / Android 15 Termux dump on 2026-10-20. The binary files and their hashes/build IDs were not attached to this commit, so symbol offsets and disassembly must be reproduced against the exact device build before any binary instrumentation.

## Observations from the supplied dump

- `audio.primary.blair.so` has an undefined dynamic symbol `pal_get_timestamp`.
- `libar-pal.so` reports `pal_get_timestamp` at file-relative offset `0x2ec120`, symbol size 4 bytes. The shown AArch64 instruction branches to `0x19080c`; this is a thunk, not the body of the function.
- `libpalclient.so` reports `pal_get_timestamp` at `0x1c930`, size 4 bytes, also shown as a branch thunk.
- `vendor.qti.hardware.pal@1.0-impl.so` reports `PAL::ipc_pal_get_timestamp` at `0x1b5fc`, size 380 bytes. The excerpt allocates and zeroes `0x240` (576) bytes and calls `pal_get_timestamp@plt`. This allocation does not establish the size or layout of `PalSessionTime` or `pal_session_time`.
- `SessionAlsaPcm::getTimestamp(pal_session_time*)` appears at `0x24adc4` and delegates to `SessionAlsaUtils::getTimestamp(mixer*, vector<int> const&, unsigned int, pal_session_time*)`.
- `PayloadBuilder::payloadTimestamp(...)` appears at `0x2340bc` and requests a 48-byte vector. That is not proof that the timestamp structure itself is 48 bytes.
- The supplied mixer XML excerpt contains route mux settings such as `RX_MACRO RX0 MUX = ZERO`; the excerpt does not demonstrate a writable PLL or variable-ratio ASRC control.

## Engineering conclusion

The dump supports a plausible timestamp-query path. It does **not** prove that the timestamp path controls sample-clock frequency, that a PLL control is exposed through ALSA mixer, or that the timestamp structure has a particular ABI/layout.

Do not hook or modify the timestamp function as a substitute for rate control. Do not write guessed arrays to mixer controls. First identify and validate an existing variable-ratio ASRC/rate-control interface and its units, bounds, synchronization, and lifecycle.

## Required verification before device integration

1. Record SHA-256 and ELF Build ID for every inspected shared object; preserve the exact device build fingerprint.
2. Re-run symbol and disassembly extraction on those exact files. Resolve each thunk and PLT entry, accounting for file offsets, load bias, and relocations.
3. Inspect the complete `ipc_pal_get_timestamp` body, including its callback payload construction, and obtain the matching PAL headers/source or reconstruct the ABI only with corroborating call-site evidence.
4. Establish the actual `pal_session_time` definition and units from matching headers and callers. Do not infer it from the 48-byte vector or 576-byte temporary allocation.
5. Trace `SessionAlsaUtils::getTimestamp` and identify the mixer control IDs and payload schema it reads. A timestamp-query control is not automatically a rate-control endpoint.
6. Locate the real ASRC implementation and documented/verified API for ratio changes. If none exists, keep the existing servo disconnected rather than applying a fictitious ratio.
7. Validate on a device build with instrumentation: measured ratio, drift, underruns/xruns, route changes, standby/reopen, and rollback. Host tests alone do not validate the physical DAC clock.

## Relation to IVANNA

The repository's HAL output-rate servo can estimate drift and bound correction, but it must only be connected to a callback that demonstrably changes a real ASRC ratio. The generic legacy HAL adapter is not a binary patch for the proprietary Qualcomm HAL. G-PLL/GFE are not considered connected to the device clock by this evidence alone.
