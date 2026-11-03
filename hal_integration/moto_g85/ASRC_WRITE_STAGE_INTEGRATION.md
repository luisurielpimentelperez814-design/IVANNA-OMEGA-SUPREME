# Moto G85 PAL ASRC write-stage adapter

This change adds `ivanna_pal_asrc_write_stage.hpp`, a source-level adapter for
placing IVANNA's existing `PalAsrcOutputPath` immediately before the write
performed by the long-lived owner of a real PAL output stream.

## What the adapter does

- Owns one `PalAsrcOutputPath` per physical PAL output stream.
- Allocates its output scratch buffer during stream setup, not on the audio thread.
- Accepts interleaved Float32, S16, or S32 blocks.
- Runs the existing ASRC and forwards only produced PCM bytes to a target-supplied
  write callback.
- Returns exact `framesConsumed` and `framesProduced`; the HAL caller must retain
  and resubmit any unconsumed input frames.
- Exposes the existing controller's `controlTick`, reset, ratio and diagnostics.
- Does not open a second PCM stream or write mixer controls.

## Required insertion point

In the matching Motorola primary-HAL source tree, add one
`PalAsrcWriteStage` member to the long-lived output-stream owner, after the PAL
stream handle and stream configuration are known. The HAL's existing
`pal_stream_write` call must be moved behind the adapter's `WriteBytes`
callback. That callback must construct the PAL buffer using the exact
`PalApi.h` shipped with the target build and call `pal_stream_write` on the
same existing stream handle.

Do not copy a guessed `pal_buffer` layout or use a hard-coded vtable slot/GOT
offset. The callback is intentionally typed in bytes so this repository does
not assume the private target ABI.

The adapter must be called for the actual PCM blocks feeding the USB PAL stream,
not for an AudioFlinger insert effect or a separate OMEGA AudioTrack. Use one
instance per output stream. For TIDAL's reported 96 kHz direct stream, configure
the stage with the actual PAL stream's format/rate (nominally 96 kHz in the
reported snapshot); do not force it to 48 kHz. For the 48 kHz mixer stream, use
a separate 48 kHz instance.

## Control clock requirements

Supply `QueryPairedSample` from the same PAL stream instance. A candidate is
one successful `pal_stream_get_mmap_position` result containing both frame
position and monotonic timestamp. The active stream must be tested: ordinary
mixer/direct streams may reject MMAP queries. If no valid paired source exists,
the adapter can process at unity ratio but the clock servo must remain
fail-closed; do not synthesize timestamps or infer DAC drift from callback
arrival time.

Call `controlTick()` from the stream's serialized non-real-time control task at
10–20 Hz. Reset on standby, reopen, route change, or stream discontinuity. Stop
before the PAL stream is destroyed.

## Important deployment boundary

This repository contains the adapter and the ASRC implementation, but does not
contain Motorola's proprietary `audio.primary.blair.so` source/build target.
Therefore this commit does **not** modify the installed vendor binary and does
not by itself activate the adapter on the phone. The target HAL source callsite,
matching PAL headers, exact write-buffer semantics, and on-device build/test are
still required before claiming the HAL is connected.

The known ELF findings (the PAL write dispatch, the `libpalclient.so` thunk,
and candidate stream methods) are useful reverse-engineering leads, but the
previously reported vtable slot and `pal_buffer` size/layout remain
unauthenticated. Do not patch the binary using those values.
