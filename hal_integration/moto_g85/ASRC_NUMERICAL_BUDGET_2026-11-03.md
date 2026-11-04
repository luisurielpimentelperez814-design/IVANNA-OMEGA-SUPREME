# Moto G85 PAL ASRC — numerical sizing and release gate

Date: 2026-11-03
Scope: sizing calculations for the source-level `PalAsrcWriteStage`; not a claim
that the proprietary Moto G85 HAL has been patched or that a DAC clock actuator
has been verified.

## Inputs already established

- TIDAL direct output thread: 96,000 frames/s, stereo, reported by AudioFlinger.
- The 192-frame period belongs to the separate 48 kHz MIXER/FAST output thread;
  it is **not** established as the TIDAL direct stream's PAL block size.
- The earlier USB inspection found a full-speed synchronous endpoint with no
  asynchronous feedback endpoint. That device is host-SOF-clocked; no independent
  DAC oscillator/feedback loop has been demonstrated.
- The target PAL buffer ABI, real direct-stream block size, and exact
  `pal_stream_write` callsite remain unauthenticated.

## Frame and payload arithmetic

For a stereo stream, bytes per frame are 4 for S16, 6 for packed S24_3, and 8
for S32 or F32.

| Rate / format | Bytes per second | 192-frame payload | 512-frame payload |
|---|---:|---:|---:|
| 96 kHz S16 stereo | 384,000 | 768 B | 2,048 B |
| 96 kHz packed S24_3 stereo | 576,000 | 1,152 B | 3,072 B |
| 96 kHz S32/F32 stereo | 768,000 | 1,536 B | 4,096 B |

At 96 kHz, 192 frames represent 2.000 ms; at 48 kHz, they represent 4.000 ms.
A 512-frame block at 96 kHz represents 5.333 ms. These are arithmetic
conversions, not claims about the direct PAL period.

## Ratio envelope and worst-case output capacity

The current PAL write-stage adapter accepts a ratio envelope of 0.9994–1.0006
(±600 ppm). The maximum positive output-frame growth is therefore:

- 96 kHz: 57.6 frames/s.
- 48 kHz: 28.8 frames/s.
- 44.1 kHz: 26.46 frames/s.
- A 50 ms control interval at 96 kHz corresponds to 2.88 frames of worst-case
  accumulated difference; 100 ms corresponds to 5.76 frames.

For a 600 ppm upper ratio, a conservative per-call capacity is
`ceil(inputFrames / 0.9994) + 4`:

| Input frames | 600 ppm bound | Existing adapter's conservative bound* |
|---:|---:|---:|
| 192 | 197 frames | 198 frames |
| 512 | 517 frames | 522 frames |

`* `The current `PalAsrcBridge::maxOutputFrames()` uses a conservative
fallback minimum step of 0.99 and adds four guard frames. This is deliberately
larger than the ±600 ppm envelope when the minimum step has not been published.

For stereo 32-bit scratch storage, those adapter bounds require 1,584 bytes
for a 192-frame input block and 4,176 bytes for a 512-frame input block.
S16/S24_3 output uses fewer bytes, but frame capacity must remain the same.
The adapter must still honor both `framesConsumed` and `framesProduced`;
a partial consume requires retaining and resubmitting the unconsumed input.

## What the clock evidence means

The synchronous USB endpoint is paced by the host USB frame clock. The evidence
does not show a separate DAC clock that needs to be steered. Therefore the
correct initial setting for this specific USB DAC is **unity ratio / correction
disabled**. A non-unity servo would be unjustified; even an enabled unity-ratio
ASRC can add filter latency and alter samples without solving a demonstrated
clock mismatch.

A different asynchronous USB DAC must be measured independently. Do not reuse
this conclusion for another endpoint.

## Release and activation gates

A Magisk ZIP can be packaged and rolled back, but packaging is not proof of
HAL integration. Do not publish or label a build as “PAL ASRC connected” until
all of the following are true:

1. The exact active TIDAL stream's PAL write owner and callsite are identified.
2. The target `pal_buffer` ABI and byte/frame semantics are verified against
   matching firmware headers or runtime evidence.
3. The ASRC stage is inserted before the write on the same existing PAL handle,
   with correct buffering for both consumed and produced frame counts.
4. A build for arm64 passes native/host tests and the Magisk ZIP integrity gate.
5. On-device tests prove the hook is loaded in the correct process, count input
   and output frames, show no underruns, and can disable/rollback cleanly.

Until then, the existing PR #26 adapter is an integration seam, not an active
HAL hook. No proprietary vendor binary has been modified by this repository.
