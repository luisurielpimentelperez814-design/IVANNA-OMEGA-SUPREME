# Qualcomm PAL / Blair timestamp investigation

## Scope and exact target build

Observations supplied from Termux on a Moto G85, Android 15, dated 2026-10-20. Target fingerprint:

`motorola/malmo_gn/malmo:15/V1UOS35H.74-16-4-16/11943-1eca7:user/release-keys`

These hashes and Build IDs identify the copied files. Offsets are ELF virtual/file offsets as shown by the tools, not process runtime addresses; instrumentation must resolve load bias and relocations.

| File | SHA-256 | ELF Build ID |
|---|---|---|
| `audio.primary.blair.so` | `b84eb4887ba5ab010f544286c0f1a4fdb14c103cd076a195438816392765cafe` | `ff49eb05aa17808870f6226949761faf` |
| `libar-pal.so` | `0a2eef24718b89a6379938f93b5bc8f693078c7225db0a75cf7753560d23fdeb` | `c1be29112c3026e9ec544d7e183a1720` |
| `libpalclient.so` | `1bcd5eac479fd6ded710ce535d2eb6bd443af1b7544d341475594e06b2fa8524` | `69d067aeed442a54632dcc0f03b9edf0` |
| `vendor.qti.hardware.pal@1.0-impl.so` | `3c29f3907cc412b023e8961e4e04321dd0e2ca3ac36c8082aeded7797c44a712` | `837cd6bf22b7cd10fa572d4bcc370873` |

## Confirmed observations from the supplied dump

- `audio.primary.blair.so` has an undefined dynamic symbol `pal_get_timestamp`.
- `libar-pal.so` reports `pal_get_timestamp` at `0x2ec120`, symbol size 4 bytes. Its shown AArch64 instruction branches to `0x19080c`; this is a thunk, not the body of the function.
- `libpalclient.so` reports `pal_get_timestamp` at `0x1c930`, size 4 bytes, also shown as a branch thunk.
- `vendor.qti.hardware.pal@1.0-impl.so` reports `PAL::ipc_pal_get_timestamp` at `0x1b5fc`, size 380 bytes. The shown code validates a stream handle, allocates and zeroes `0x240` (576) bytes, initializes stack values including `0x18` (24), calls `pal_get_timestamp@plt`, copies 24 bytes from a stack location and an additional 8-byte value into the allocated buffer, then invokes a callback.
- The 576-byte allocation is a temporary buffer in this function; it does **not** establish the size/layout of `PalSessionTime` or `pal_session_time`. The shown 24-byte copy and 8-byte store likewise do not, by themselves, define the full public ABI.
- `SessionAlsaPcm::getTimestamp(pal_session_time*)` appears at `0x24adc4`. It calls `SessionAlsaUtils::getTimestamp(mixer*, vector<int> const&, unsigned int, pal_session_time*)` through a PLT entry. `llvm-nm -D -C` reports the latter symbol at `0x2ec890`.
- `PayloadBuilder::payloadTimestamp(...)` appears at `0x2340bc` and requests a 48-byte vector. The code writes metadata into a buffer, but the 48-byte vector is not proof that the timestamp structure itself is 48 bytes or that this payload changes a clock.
- The supplied mixer XML excerpt contains route mux settings such as `RX_MACRO RX0 MUX = ZERO`; it does not demonstrate a writable PLL or variable-ratio ASRC control.

## Engineering conclusion

The evidence supports a timestamp-query path and identifies the next function to inspect: `SessionAlsaUtils::getTimestamp` at `0x2ec890`. The output provided so far does not include the body at that address, so it does not yet identify the exact mixer control ID, array schema, or units used by the utility.

This evidence does **not** prove that the timestamp path controls sample-clock frequency, that a PLL control is exposed through ALSA mixer, or that the timestamp structure has a particular ABI/layout.

Do not hook or modify a timestamp function as a substitute for rate control. Do not write guessed arrays to mixer controls. First identify and validate an existing variable-ratio ASRC/rate-control interface and its units, bounds, synchronization, and lifecycle.

## Next verification steps

1. Disassemble the actual function body at `0x2ec890`, with surrounding callers and callees. Follow its PLT calls and string/control-name references to determine which mixer controls and payload schema it reads.
2. Inspect the complete `ipc_pal_get_timestamp` callback construction and corroborate the ABI against matching PAL headers or additional call sites. The current excerpts do not prove the structure layout.
3. Trace `PayloadBuilder::payloadTimestamp` callers and its downstream consumers to determine what the 48-byte payload represents.
4. Locate the actual ASRC implementation and a documented/verified ratio-change API. A timestamp-query control is not automatically a rate-control endpoint.
5. Validate on a device build with instrumentation: measured ratio, drift, underruns/xruns, route changes, standby/reopen, and rollback. Host tests alone do not validate the physical DAC clock.

## Relation to IVANNA

The repository's HAL output-rate servo can estimate drift and bound correction, but it must only be connected to a callback that demonstrably changes a real ASRC ratio. The generic legacy HAL adapter is not a binary patch for the proprietary Qualcomm HAL. G-PLL/GFE are not considered connected to the device clock by this evidence alone.
