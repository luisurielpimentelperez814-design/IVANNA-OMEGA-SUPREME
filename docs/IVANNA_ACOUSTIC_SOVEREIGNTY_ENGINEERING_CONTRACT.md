# IVANNA OMEGA SUPREME — Acoustic Sovereignty Engineering Contract

Status: architecture contract / implementation gates
Target: Motorola Moto G85 5G (malmo, arm64-v8a, Android 15)
Repository baseline: v2.5.210 (versionCode 501)

## Purpose

Turn the acoustic-sovereignty vision into measurable software and hardware capabilities. IVANNA may aggressively optimize resources it is authorized and technically able to control, but it must never report an estimate as a measurement, a request as an applied control, or a perceptual hypothesis as a physical result.

Sovereignty means deterministic ownership of IVANNA's own DSP, control plane, model lifecycle, privacy and evidence—not ownership of Android, vendor firmware, the DAC oscillator, the room, or other people's devices.

## Non-negotiable invariants

1. Audio-thread boundedness: no network, disk I/O, heap allocation, model loading, blocking mutex, camera work, or unbounded inference on the real-time render callback.
2. Fail-safe actuation: every control has a validated range, finite-value check, slew limit, peak/energy budget and neutral fallback. NaN, infinity, stale telemetry, missing calibration or unknown route must not increase gain.
3. Evidence states: distinguish DETECTED, AVAILABLE, REQUESTED, APPLIED, READ_BACK, MEASURED and USER_VERIFIED. Successful JNI/IPC send is not proof that HAL or DSP applied the value.
4. Thermal honesty: thermal governors protect sustained operation; they do not repeal heat dissipation, battery limits, silicon throttling or safe operating limits. Never disable OEM thermal protections or write undocumented voltage/clock sysfs nodes.
5. Clock honesty: estimate source/device drift from timestamped frame counters, use bounded ASRC, and report residual error. Do not claim oscillator takeover or zero latency.
6. Route truth: classify headphones, USB line-out, wired AUX, Bluetooth and built-in speaker separately. A USB descriptor alone cannot identify headphones versus amplifier line input.
7. Privacy by default: no emotion inference, biometric capture, Wi-Fi CSI collection or sensor fusion without explicit opt-in, documented purpose, visible status, retention limits and disable control.
8. Reproducibility: every model, dataset, calibration and generated artifact carries version, provenance, integrity hash and compatibility metadata.

## Capability ladder and acceptance gates

| Domain | Implementable capability | Required evidence | Explicit non-claim |
|---|---|---|---|
| SoC / scheduling | Observe public thermal, battery, memory and process signals; request bounded thread priority where permitted | device matrix, scheduler traces, underrun counts, thermal soak | no arbitrary voltage/frequency control; no guaranteed sub-nanosecond latency |
| DAC / clock | Read exposed timestamps; estimate drift; drive ASRC with bounded ratio and anti-windup | loopback or trusted timestamp source, ppm error, residual drift, glitch count | no direct ownership of DAC crystal/PLL |
| DSP / neural | Run quantized bounded models off callback; publish immutable parameter snapshots | worst-case execution time, memory ceiling, callback budget, A/B and null tests | no infinite-resolution reconstruction; no recovery of unknown lost samples |
| Lossy restoration | Estimate plausible harmonics/transients with confidence and conservative mix | codec corpus, blind listening, spectral/transient error, hallucination tests | output is not the verified studio master |
| HRTF / binaural | Personalize headphone rendering with valid HRTF/SAF assets and supported head tracking | HDF5 validation, provenance, fit/confidence, latency and orientation tests | headphone HRTF is not speaker transaural cancellation |
| External speakers | Route-aware stereo processing, center preservation, bounded coloration | measured 2x2 speaker-to-ear transfer functions for transaural mode; otherwise stereo-only mode | AUX L/R is not WFS or discrete multichannel |
| WFS / distributed emitters | Experimental synchronized multi-endpoint renderer only with stable clocks, known positions and calibration | multi-device clock sync, geometry, transfer functions, phase-error and coverage maps | nearby IoT devices are not automatically synchronized loudspeakers |
| Active noise control | Local narrow-zone ANC only with suitable reference/error microphones and low-latency actuation | measured acoustic path, stability margins, multi-position error maps | no room-wide silence from phone speakers or unknown microphones |
| Room reconstruction | FotoHolo/AR geometry and acoustic material priors; measured calibration when available | geometry confidence, dimensions, material uncertainty, impulse-response validation | point cloud alone does not reveal absorption or exact room impulse response |
| SHM control plane | Versioned, bounded, authenticated local IPC with health counters and explicit ACK/read-back | ABI tests, malformed-frame fuzzing, SELinux tests, fd ownership, stale-state tests | shared memory is not telepathy and does not bypass permissions |
| Intent adaptation | User-authored modes and explicit context signals; opt-in sensor features | consent state, provenance, false-positive rate, explainability and disable tests | no thought reading or reliable emotion detection from grip/radio signals |

## Runtime control architecture

### Observation plane

Collect supported signals only: Android public thermal/battery/memory/route/audio inventory; audio frame counters and monotonic timestamps exposed by the selected path; DSP callback load, underruns, clipping, limiter activity and IPC health; optional camera/IMU measurements only during an explicitly enabled AR session.

Each observation includes source, monotonic timestamp, validity, uncertainty and expiry. Stale observations are discarded.

### Decision plane

A bounded policy engine computes a target profile from output route and declared endpoint class, resource budget, content features with confidence, active user intent (focus/immersion/reference/safe), and calibration availability/confidence. It produces a complete immutable parameter frame; it never changes native DSP state field-by-field from a background thread.

### Actuation plane

The render thread consumes the latest validated snapshot through lock-free publication or an existing bounded control queue. Every parameter has finite/range validation, per-block slew limit, peak/loudness budget, neutral fallback, and requested-versus-applied telemetry. Heavy inference, room solving, FotoHolo, model loading and file I/O stay off the render thread.

### Evidence plane

The dashboard shows separate states for observed capability, route/device support, requested target, native dispatch result, daemon acknowledgement, DSP read-back (if available), and physically measured result (if available). Never render a green active state from a boolean request alone.

## Acoustic field requirements

### Headphones

Personal HRTF/SAF binaural rendering is eligible only for a confirmed headphone route. Head tracking includes timestamp alignment, prediction horizon, stale-pose rejection and neutral fallback. Validate end-to-end latency and orientation discontinuities.

### Genezi / AUX / USB line-out

A stereo input is two electrical channels. Keep center and mono compatibility, preserve polarity, limit differential widening and avoid applying personal headphone HRTF as though it were calibrated transaural processing. The current 0.15 external-line-out HRTF blend is conservative coloration, not cancellation or room correction.

### WFS and active cancellation

Do not enable WFS or multi-point ANC merely because multiple devices are visible on Wi-Fi/Bluetooth. Eligibility requires explicit enrollment of each endpoint, stable sample clocks or continuous synchronization measurement, endpoint position/orientation, calibrated electro-acoustic transfer functions, phase-error/stability monitoring, and safe output limits with a tested stop/fallback path. Without all requirements, keep ordinary stereo processing and label the feature unavailable/experimental.

## Generative restoration requirements

A restoration model may estimate missing harmonic structure, transient envelopes and plausible ambience. Its output is confidence-weighted and user-adjustable, with untouched bypass. It must not be described as recovery of the original waveform. Tests include adversarial speech/music mixtures, clipped signals, codec artifacts, silence, impulses, out-of-distribution material and hallucination detection.

## Security and privacy

- Keep daemon IPC local; validate peer identity, message length, protocol version and every command.
- Do not expose SHM writable mappings to untrusted processes. Use descriptor transfer only after validating peer credentials and ownership.
- Do not accept arbitrary sysfs paths, shell fragments, firmware commands or vendor parameter strings from the app/dashboard.
- Sensor/radio-derived features remain off until explicit consent. Store minimum data and expose disable/clear controls.
- Preserve Android/Magisk SELinux and thermal boundaries. Root access is not proof that an operation is safe or supported.

## Super-test release gates

1. Source/assets: SOFA files validated by a real HDF5 reader; provenance and hashes match; no silent UTF-8 repair.
2. Host/native: CTest, DSP numerical references, malformed-input fuzzing, race/ABI/layout tests.
3. Android/JVM: all unit tests, JNI symbol audit, route-policy matrix, state-restoration tests.
4. Build integrity: clean arm64-v8a NDK build; ELF PIE/RELRO/BIND_NOW; no unexpected libc++_shared.so dependency in Magisk-launched binaries; APK/module ZIP CRC; staged hashes equal compiled hashes.
5. IPC: SCM_RIGHTS truncation, unauthorized peer, stale fd, malformed frame, SHM version mismatch, daemon restart and SELinux denial tests.
6. Device soak: Moto G85 route matrix, 30-minute thermal/resource soak, underrun/clipping/latency telemetry, USB reconnect and daemon crash recovery.
7. Listening validation: level-matched blind A/B across multiple tracks; no improvement claim from unblinded preference alone.
8. External acoustics: Genezi stereo checks first. Transaural/WFS/ANC claims additionally require calibrated microphones and measured transfer functions.

A failed gate blocks the corresponding capability claim. It is not bypassed by changing a dashboard color or raising a parameter.

## Current implementation status at v2.5.211

The repository contains route detection, SAFMUS resource budgeting, thermal policy, DSP engines, HRTF/RIR code, FotoHolo assets and a shared-memory control path. Their presence in source does not prove every path is active on the Moto G85. The Genezi policy uses a bounded minimax route multiplier and a low external-line-out HRTF blend; both remain listening/measurement hypotheses pending physical acceptance tests.

### Implemented in this increment: control-plane evidence

- The daemon's consumer state now comes from the AudioEffect beacon, not from `OmegaControlBus::isWriterOpen()` (which only reports the daemon writer).
- The beacon ABI is 72 bytes and carries `applied_control_generation`. `omega_effect` publishes that generation only after completing the DSP block.
- `GET_STATUS` and rich command replies report the published generation, last DSP-applied generation, processed frames and effect state. `applied=true` requires a processing beacon and an applied generation at least as new as the published snapshot.
- The app exposes this data through `OmegaEngineBridge.requestControlPlaneEvidence()`.
- The app/daemon HELLO mismatch is corrected: the app negotiates protocol v2, matching `OMEGA_PROTO_VERSION=2`, and sends its version in the handshake.

This is evidence of snapshot consumption and completed DSP blocks. It is not an acoustic measurement, DAC clock read-back, or proof of perceptual improvement. The new native regression checks the beacon generation counter; full CI and on-device verification remain required.

Next implementation slices, each as a separate testable change:
1. Timestamp/frame-counter drift estimator and ASRC closed-loop validation.
2. Immutable DSP parameter-frame publication with callback WCET/underrun instrumentation.
3. Calibration-aware external-speaker eligibility; WFS/ANC stay disabled until measured.
4. Opt-in FotoHolo room model with uncertainty propagation.
5. Bounded, confidence-weighted lossy restoration with bypass and hallucination tests.

No release may claim the complete vision until each slice is implemented, wired, tested and verified on hardware.
