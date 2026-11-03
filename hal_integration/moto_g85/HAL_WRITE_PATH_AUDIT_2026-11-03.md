# Moto G85 HAL write-path audit — 2026-11-03

## Purpose

Record the newest supplied Moto G85 / blair ELF and AudioService findings
without conflating separate captures. This is an evidence ledger, not a claim
that IVANNA is already connected to the proprietary HAL.

## Current TIDAL stream evidence

The latest supplied AudioService snapshot reports:

- Package: `com.aspiro.tidal`
- Usage/content: MEDIA / MUSIC
- Stereo channel mask: `0x3`
- Sample rate: `44100`
- Stream state: started
- Mode: normal AudioTrack in this capture; no evidence in this snapshot that
  TIDAL is DIRECT/offload.

An earlier capture reportedly showed a 96 kHz DIRECT stream. Treat those as
separate captures. The 96 kHz observation does not describe the latest stream
and must not be used to configure a current 44.1 kHz PAL stream.

## Primary HAL findings supplied

| Symbol / point | RVA | Supplied finding | What it proves |
|---|---:|---|---|
| `StreamOutPrimary::write` | `0x54dd4` | Implemented, 788 bytes | HAL write entrypoint exists |
| `configurePalOutputStream` | `0x550e8` | Implemented, 1876 bytes | PAL stream setup/configuration path |
| `splitAndWriteAudioHapticsStream` | `0x5583c` | 612 bytes | Candidate stream-management/helper path; not yet proven PCM writer |
| `pal_stream_write` PLT | `0x7cfc0` | GOT `0x81268` | Imported PAL write trampoline exists |
| `pal_stream_open` PLT | `0x7cf80` | Called by `Open()` | HAL opens a PAL stream |

The supplied disassembly of `StreamOutPrimary::write` shows a copy into an
internal queue and no direct branch to the `pal_stream_write` PLT stub.
Consequently, `StreamOutPrimary::write` is not yet established as the
correct ASRC insertion point. The queue consumer / render worker must be
identified, including its owning stream object and exact write call.

The reported 247 references to the `pal_stream_write` PLT are candidate
callers only. They are not proof that any specific callsite carries TIDAL's
PCM. Do not patch all references or use the PLT address as a universal
cross-process hook.

## PAL HIDL / AGM findings supplied

- `vendor.qti.hardware.pal@1.0-impl.so::ipc_pal_stream_write` at RVA
  `0x1a34c` is a candidate Binder bridge.
- `ipc_pal_stream_open` at RVA `0x16bfc` is a candidate stream-open bridge.
- `libagm.so::agm_session_write` at RVA `0x264f0`, size 8, is likely a
  veneer/thunk and must be followed to its branch target before interpreting
  it as the write implementation.
- The process list reports vendor audio HAL service PID 1427 and audioserver
  PID 1486. The supplied mappings do not yet establish where
  `audio.primary.blair.so`, `libpalclient.so`, and `libar-pal.so` are
  mapped at the time of TIDAL playback.

These observations do not yet prove whether the HAL process calls PAL
locally, crosses Binder to the PAL service, or uses more than one transport
path for the relevant stream. Correlate PID, loaded-object maps, stream
handle, and callsite during the same playback session.

## What is deliberately not assumed

- `aaudio.mmap_policy` and `aaudio.mmap_exclusive_policy` are policy values,
  not proof that this AudioTrack uses MMAP.
- `audio.offload.disable=false`, minimum offload duration, and USB-offload
  feature properties are configuration, not proof of active offload.
- Dolby enabled/current properties do not locate Dolby in the PCM path.
- `pal_stream_get_mmap_position` being imported does not prove the active
  TIDAL stream supports MMAP position queries.
- A public Qualcomm `PalDefs.h` from another branch/device is useful
  reference material, but it does not authenticate the exact private
  `pal_buffer` ABI shipped in this Moto G85 firmware.
- The ASRC ratio remains unity unless a paired clock source and an accepted
  actuator are both verified for this exact stream.

## Implemented in this branch

`ivanna_pal_asrc_write_stage.hpp` now treats the PAL write callback as a
byte-count API, preserving PAL's positive-byte / zero-progress / negative-error
semantics. If PAL accepts only part of an output block, the adapter retains the
unwritten bytes and drains them before accepting new input. It detects
impossible over-reporting and non-frame-aligned partial writes and fails closed.
The caller still must honor `framesConsumed` and `framesProduced`.

This fixes output-loss behavior in the adapter; it does not install a hook in
`audio.primary.blair.so`.

## Remaining integration gate

Before any live hook can be built, the following must be tied together from
the same capture / firmware build:

1. Identify the internal queue consumer and its call to the PAL write path.
2. Identify the exact active stream handle for TIDAL and its live format.
3. Authenticate the target `pal_buffer` definition from the matching firmware
   headers or prove its layout from complete callsite/data-flow analysis.
4. Establish whether the write executes in the primary HAL process or crosses
   into the PAL HIDL service, then select the matching injection boundary.
5. Build and validate the ARM64 hook against the actual target ABI, test frame
   accounting and short writes, and package a reversible Magisk deployment
   with a tested disable/rollback path.

Until those gates pass, no binary patch, guessed vtable slot, guessed
`pal_buffer` layout, or claim of active TIDAL processing is acceptable.
