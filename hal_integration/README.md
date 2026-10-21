# IVANNA hardware clock: Android HAL integration

## Findings

Android already defines the required output-clock API. The legacy HAL interface
`audio_stream_out::get_presentation_position(const audio_stream_out*, uint64_t*,
timespec*)` returns the presented-frame count and the associated
`CLOCK_MONOTONIC` timestamp. Android's framework HAL wrapper also exposes
`StreamOutHalInterface::getPresentationPosition()`. References:

- https://source.android.com/reference/hal/structaudio__stream__out
- https://android.googlesource.com/platform/frameworks/av/+/master/media/libaudiohal/include/media/audiohal/StreamHalInterface.h

This repository does not contain the device vendor's `audio.primary.*` HAL
implementation, nor an AIDL audio HAL service implementation. Therefore a
device-specific call site cannot be patched automatically here. The producer
helper and the effect-side reader/estimator are now present; the HAL call site
must be added to the actual device source tree.

## Producer call site

Add `app/src/main/cpp/include` and this directory to the vendor HAL target's
include paths. Include `ivanna_audio_clock_reporter.hpp` in the output-stream
implementation. Keep one reporter per physical output stream (or one per
underlying output clock), not one per client track.

At stream open/configuration, outside the real-time write loop:

```cpp
ivanna::audio::HalOutputClockReporter ivannaClock;
const bool ivannaClockReady = ivannaClock.start();
```

At a non-blocking telemetry point in the HAL's existing position-reporting path,
only after the underlying driver query succeeds:

```cpp
uint64_t frames = 0;
timespec timestamp{};
const int status = stream->get_presentation_position(stream, &frames, &timestamp);
if (status == 0 && ivannaClockReady) {
    ivannaClock.report(frames, timestamp, configuredSampleRateHz);
}
```

The snippet is a template: adapt names to the actual HAL implementation. Do not
call the function pointer recursively from inside its own implementation. If
you are implementing `get_presentation_position` itself, report only after the
underlying ALSA/driver position query returns its matching frame/timestamp pair.
For AIDL HALs, call the reporter with the frames and timestamp returned by that
stream's `getPresentationPosition()` implementation. Do not use a timestamp
sampled independently from the frame counter.

The effect callback maps the shared snapshot outside audio processing and reads
it with bounded atomic loads. The reader does not open files or call the kernel
inside the callback. Hardware telemetry remains invalid until a real producer
publishes at least two monotonic samples.

## Data path and access control

The shared snapshot defaults to:

`/data/vendor/ivanna_audio/clock_snapshot`

The bridge uses mode 0660, not world-writable permissions. The target product
must create the directory at boot, assign ownership that both the HAL producer
and AudioFlinger effect can use, label the file with the new SELinux type, and
merge the policy templates in `sepolicy/`. Android product policy differs by
device; verify the actual HAL service domain and effect-loading domain before
shipping. Do not disable SELinux or use permissive mode as a workaround.

Templates in this directory:
- `init.ivanna_audio_clock.rc`
- `sepolicy/ivanna_audio_clock.te`
- `sepolicy/file_contexts`

These are integration templates, not a claim that the current phone's vendor
policy already permits access.

## Safety gates

- Never use the effect callback's `CLOCK_MONOTONIC` observation as the hardware
  frame timestamp.
- Never enable PLL phase correction from the clock estimator until two or more
  valid HAL samples are observed and hardware tests confirm the timestamp domain.
- This monitor estimates output clock rate; it does not modify sample rate,
  PCM, or GFE phase by itself.
- Verify lock-free 64-bit atomics for the target ABI and validate the shared
  mapping on the target device.
- Run native tests, build the actual vendor HAL, verify SELinux denials/logs,
  then measure drift while routing speaker, wired headset, USB and Bluetooth
  separately. Bluetooth/external routes may report a different observable clock.
