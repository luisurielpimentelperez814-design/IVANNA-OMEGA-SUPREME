# Moto G85 PAL active-stream binding

This folder adds a compile-time binding between the existing PAL handle owned by the Motorola output HAL and IVANNA's shared hardware-clock snapshot. It is telemetry-only.

## Include and lifecycle

Build this header only inside a matching Motorola/Qualcomm HAL source tree that already has the real `pal_stream_handle_t*` and the matching vendor `PalApi.h`:

```cpp
#include "ivanna_pal_active_stream_binding.hpp"

// In the long-lived output-stream owner, after PAL stream creation:
ivanna::audio::moto_g85::ActivePalStreamClockBinding ivannaClock(
    existingPalStreamHandle, activeSampleRateHz);
const bool clockTelemetryReady = ivannaClock.start();

// On the owner's serialized low-rate control task (~10–20 Hz):
if (clockTelemetryReady) (void)ivannaClock.poll();

// On standby, route change, PAL handle replacement, or stream restart:
ivannaClock.reset();
// If the PAL handle was replaced, while serialized:
ivannaClock.rebind(newPalStreamHandle);

// Before destroying the output stream:
ivannaClock.stop();
```

The callback invokes `pal_stream_get_mmap_position()` exactly once per sample and uses the frame position and timestamp returned by that same result. Unsupported stream modes/errors fail closed and publish no new sample. The code does not open a second PCM device, write mixer controls, infer time from wall clock, or issue ASRC commands.

## Non-negotiable deployment gates

1. Insert the binding into the actual long-lived output-stream owner in a source/build tree matching the installed Moto G85 HAL. This repository does not contain Motorola's proprietary `audio.primary.blair.so` source or build target, so the header is not auto-injected into that binary.
2. Confirm the real active stream supports MMAP position queries; PAL may return `-ENOSYS` for ordinary mixer streams.
3. During real USB playback, capture repeated successful samples and verify frames advance, timestamps increase monotonically, and IVHC reader permissions/visibility are correct.
4. Reset on standby/reopen/route changes and validate speaker, USB, and Bluetooth independently.
5. Keep hardware/system ASRC correction disabled until a real variable-ratio actuator, its payload/units, and runtime acceptance semantics are separately verified.

The existing IVANNA software ASRC in `IvannaBridgePlayer` only corrects the app-owned player path. Do not feed PAL samples from a different output stream into that player's `AudioTrack` PLL. A working telemetry binding is not proof of system-wide clock correction.
