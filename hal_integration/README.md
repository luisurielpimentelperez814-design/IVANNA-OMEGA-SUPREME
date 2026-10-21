# IVANNA hardware-clock control: target HAL integration

This directory contains the reusable HAL-side hook. It is not a replacement for the device vendor's HAL and must be instantiated by the target output-stream implementation.

## Integration files

- `ivanna_hal_output_stream.hpp`: calls the target HAL's presentation-position query and sends the paired sample to the bounded servo.
- `ivanna_audio_clock_reporter.hpp`: optional diagnostic snapshot publisher.
- `sepolicy/` and `init.ivanna_audio_clock.rc`: product-specific templates only; adapt to the target policy and init layout.

Add `app/src/main/cpp/include` to the target HAL module's include paths. Compile the target HAL against the same C++ standard and NDK/platform toolchain used by that HAL.

## 1. Own the controller in the long-lived output stream

The stream implementation owns one `HalOutputStreamClockControl` per physical output stream/clock. Pass two callbacks:

1. `queryPresentationPosition`: call the stream's existing driver/HAL presentation-position method once; only return true when both the frame counter and its associated monotonic timestamp are valid.
2. `applyExistingAsrcRatio`: set the ratio on the ASRC already used by that stream. Return true only after the converter accepts the command. Do not report success if the implementation merely stores an unused variable.

Example callback contract (adapt names and types to the actual vendor implementation):

```cpp
static bool queryPresentationPosition(void* context,
                                     ivanna::audio::HardwareClockSample* out) noexcept {
    auto* stream = static_cast<MyOutputStream*>(context);
    uint64_t frames = 0;
    timespec timestamp{};
    // This method must query the underlying driver, not call this wrapper recursively.
    if (!stream->queryDriverPresentationPosition(&frames, &timestamp)) return false;
    if (timestamp.tv_sec < 0 || timestamp.tv_nsec < 0 ||
        timestamp.tv_nsec >= 1000000000L) return false;
    out->presentedFrames = frames;
    out->hardwareTimestampNs =
        static_cast<uint64_t>(timestamp.tv_sec) * 1000000000ull +
        static_cast<uint64_t>(timestamp.tv_nsec);
    out->nominalSampleRateHz = stream->activeOutputSampleRate();
    return true;
}

static bool applyExistingAsrcRatio(void* context, double ratio) noexcept {
    auto* stream = static_cast<MyOutputStream*>(context);
    return stream->existingAsrc().setInputFramesPerOutputFrame(ratio);
}
```

The example names are placeholders, not vendor API names. Implement them against the actual HAL source and its existing ASRC.

## 2. Call at a control cadence

Create the controller during stream construction, after the ASRC exists:

```cpp
ivanna::audio::HalOutputStreamClockControl clockControl(
    &queryPresentationPosition, this, &applyExistingAsrcRatio, this);
```

Call `clockControl.controlTick()` from a low-rate control task (approximately 10–20 Hz) that is serialized with stream lifecycle and ASRC configuration. Do not call it from the hard real-time render/write path. Call `clockControl.reset()` on standby, stream reopen, route/device change, or timestamp discontinuity. The ASRC callback must be thread-safe or dispatched to the HAL's existing control thread.

If the presentation-position query fails, the controller resets and attempts to command unity ratio. If the ASRC rejects a command, the controller marks the applied ratio unknown and disables correction. Verify the ASRC's actual applied ratio in target logs/telemetry.

## 3. Platform API mapping

- Legacy C HAL: use the implementation's successful `audio_stream_out::get_presentation_position` result, where the returned frame count and `CLOCK_MONOTONIC` timestamp belong to the same query.
- HIDL/AIDL HAL: use the stream implementation's corresponding presentation-position method and preserve the timestamp domain and frame-counter semantics.
- Do not substitute callback entry time, wall-clock time, or a separately sampled timestamp.
- If the HAL has no variable-ratio ASRC, the callback cannot be implemented honestly by changing a field. Integrate and validate a real ASRC in the output data path first; do not stack another resampler without analyzing the existing chain.

## 4. Validation gates

1. Build the actual target vendor HAL against the device's Android branch.
2. Run host tests for lock gating, correction bounds, slew, query failure, and ASRC rejection.
3. On device, log commanded and applied ratio, presentation-frame deltas, timestamps, underruns, and output drift.
4. Validate speaker, wired, USB, and Bluetooth independently; these may use different output clocks and routing paths.
5. Do not claim physical DAC-clock correction from host tests. Software ASRC compensates stream cadence; it does not directly discipline the oscillator.

The repository does not contain the proprietary target HAL source, so this component is the integration implementation/contract. A target-specific patch cannot be completed safely until the vendor stream and its ASRC API are identified.
