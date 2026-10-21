# IVANNA hardware clock: Android HAL integration

## Control path
This package now has two distinct stages:
1. HardwareClockTracker estimates output-clock drift from paired presentation-frame and hardware-correlated timestamp samples.
2. HardwareClockRateServo turns valid estimates into an input-frames-per-output-frame command for the HAL's existing asynchronous sample-rate converter (ASRC). It requires three valid observations, clamps correction to ±500 ppm, limits slew to 25 ppm per update, smooths the estimate, and fails closed on invalid or implausible observations.

The servo is intentionally not inserted into omega_process(): AudioFlinger insert effects do not own the physical output queue and cannot reliably control the DAC's output cadence. Instantiate beside the HAL output stream and apply its ratio to that stream's existing ASRC. Do not add a second resampler if the HAL already has a variable-ratio converter; adapt its control interface.

## Android clock source
Android defines the required output-clock API. Legacy audio_stream_out::get_presentation_position returns the presented-frame count and associated CLOCK_MONOTONIC timestamp. Android's framework HAL wrapper also exposes StreamOutHalInterface::getPresentationPosition().
- https://source.android.com/reference/hal/structaudio_stream_out
- https://android.googlesource.com/platform/frameworks/av/+/master/media/libaudiohal/include/media/audiohal/StreamHalInterface.h

## Producer call site
The repository does not contain the device vendor's audio.primary HAL implementation or AIDL service source. In the target HAL source tree, keep one HalOutputClockReporter and one HardwareClockRateServo as members of the long-lived output stream (one publisher per underlying output clock). Open the reporter during stream setup, not on the write path.

In the HAL's existing presentation-position implementation, after the underlying driver query has returned a matching frame count and timestamp, publish that pair. Do not call the HAL's own function pointer recursively. At the HAL control cadence, feed HardwareClockTracker::observe() with fresh samples and pass valid estimates to HardwareClockRateServo::observe(). Apply output.inputFramesPerOutputFrame to the HAL's existing ASRC only when output.enabled is true; otherwise use unity ratio. A rate command is not proof that samples were resampled—instrument the ASRC's applied ratio and test resulting output.

## Data path and access control
Shared snapshot path: /data/vendor/ivanna_audio/clock_snapshot. The target product must create the directory, assign ownership usable by both the HAL and AudioFlinger, label it with matching SELinux type, and merge/adapt templates in sepolicy/. Never disable SELinux. Use one writer per physical output clock; opening a publisher truncates the snapshot.

## Acceptance gates
- Host tests cover lock gating, bounded correction, slew, and fail-closed behavior.
- Build and test the actual target vendor HAL and confirm its ASRC consumes the ratio.
- Verify applied ratio, underruns and output drift on speaker, wired, USB, Bluetooth separately.
- Until the device HAL call-site and ASRC are integrated and measured, this is a ready-to-integrate control primitive, not a claim the phone DAC is already being controlled.
