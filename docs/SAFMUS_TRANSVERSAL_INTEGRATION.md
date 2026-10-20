# SAFMUS transversal integration — audit and acceptance contract

**Implementation status:** SAFMUS now guards the principal live numeric control paths and the HRTF/RIR, room-correction, and evolutionary application boundaries listed below. This is a code-integration statement, not a claim that every control has been acoustically validated on hardware or that a learned SAFMUS model exists. The final Android/JVM CI and release-artifact-to-SHA verification remain mandatory before publishing.

## Non-negotiable contract for every control and processing path

Each slider, toggle, preset, environment, calibration action, HRTF/HRIR/IHR1 selection, RIR/BRIR selection, evolutionary-kernel proposal, and visual status surface must trace this chain:

1. UI intent and parameter validation.
2. Route/context/evidence validation through the shared SAFMUS policy.
3. Bounded candidate generation off the real-time audio callback.
4. Apply to the one route that owns the DSP chain.
5. Native acknowledgement/read-back of effective state.
6. UI status derived from read-back, including degraded/unavailable/error reasons.
7. Regression test covering success, stale/missing evidence, route conflict, non-finite input, latency/headroom failure, and rollback.

## Evidence honesty

- HRTF/HRIR provenance must distinguish measured, interpolated, and synthetic data.
- RIR/BRIR must distinguish measured, simulated, and user-selected impulse responses. A preset RT60 is not a room measurement.
- Optimizer fitness and kernel generations are internal metrics, not proof of audible or perceptual improvement.
- No route may infer room geometry, SPL, RT60, or speaker response from genre, a scene label, or a device name.
- SAFMUS must abstain on stale/conflicting evidence and preserve the last known-safe parameters.

## Implemented safeguards in code

- **All shared UI sliders:** `SafmusComposeSlider` intercepts Material 3 `Slider` calls in the main UI package, rejects non-finite values, clamps to each declared range, and disables invalid ranges. The OEM shared slider uses the same numeric policy.
- **Five-axis state:** `SupremeAxesPrefs.sanitizeState` is applied to UI updates, persistence, app-process native dispatch, and daemon dispatch. Invalid persisted floats fall back to safe defaults.
- **Control-plane bridge:** `OmegaEngineBridge` recursively rejects JSON payloads containing NaN/Inf and validates/clamps the typed setters for room RT60/wet, SAF latent q[7], adaptive state, perceptual state, YAMNet scores, pinna metrics, and the Supreme axes. Generic legacy vararg paths reject non-finite numeric arguments.
- **HRTF:** `IvannaSpatialManager.setHrtfSubject` runs the shared dataset/pose preflight before loading a subject. Native HRTF entrypoints reject non-finite latent vectors before mutating cached state; the manager still requires actual load/read-back confirmation.
- **RIR:** `OmegaEngineBridge.setRoom` validates RT60/wet and requires a loaded, indexed room's metadata before selecting it. The native loader resamples source IRs to the engine rate. The convolver's SAF latent coupling rejects invalid q[7]. Its 512-frame delayed tail is kept only when the sample-rate-derived algorithmic latency fits the 20 ms budget; otherwise it preserves the zero-latency head and omits the delayed tail, exposing a native degradation flag.
- **Measured room correction:** `RoomMeasurement.runVerified` gates a candidate before application using measurement, playback-chain, finite/bounded-gain, and headroom-protection evidence. `RoomCorrectionEq` sanitizes active-path samples and applies a stereo-linked post-EQ ceiling at ±0.98; bypass remains bit-exact.
- **Evolutionary kernel:** non-finite audio cues are rejected without overwriting the last valid cues; non-finite fitness is scored as worst-case. `audio_orchestrator` requires an enabled route, finite block, finite score above the existing threshold, bounded candidate gain, and block headroom before accepting a genome. Applied gain is headroom-limited and the output is capped at ±0.98. Missing Kotlin-side stability/latency evidence now fails closed by default.
- **Native latent boundaries:** the shared finite-vector guard is applied at the system effect, app-process publisher, fusion engine, HrtfManager, SyntheticHRTF, SafSpatialModifier, ObjectRenderer, HRTFConvolver, and RirConvolver.
- **Regression contract:** `scripts/check_safmus_transversal_wiring.py` runs in CI before native DSP tests and fails if key safeguards or their tests become disconnected.

## Verification still required before release

- CI must pass on the exact final `main` SHA, including the new wiring audit, native CTest, Android/JVM tests, and APK integrity checks. No release is valid for an earlier SHA if later commits exist.
- The Kotlin `gateRir` policy is still not fed measured per-block DSP timing. The native RIR convolver enforces its deterministic 512-frame algorithmic-tail budget from the session sample rate, but that is not a measurement of callback CPU time. The app-process/local-route degradation flag is now exposed to the 10D scene snapshot and prevents the reverberant-field axis from being reported as applied when the delayed tail was omitted. The daemon's independent RIR convolver still needs its own status/read-back channel; app-process status must not be treated as daemon telemetry.
- Acoustic/perceptual validation on supported devices, measured output SPL/headroom, and end-to-end read-back for every individual non-slider toggle/setter still require device-level evidence. UI presence or a successful IPC send is not proof of effective DSP application.
- No learned SAFMUS model, calibrated room model, or measured HRTF provenance is created by these guards. They enforce safe integration and truthful status, not perceptual superiority by themselves.

## New capability acceptance gates — Surround and augmented reality

These gates extend the shared SAFMUS contract to the open implementation work. They are requirements for merge/release, not a claim that the PRs are already part of `main`.

### IVANNA X SURROUND (PR #87)

- Reuse the existing Android audio-effect/DSP route; never activate a second competing route.
- SAFMUS must validate finite values and profile-specific bounds before EQ, bass, virtualizer, gain, compressor, or reverb changes are applied.
- Keep artificial reverb disabled for the external Genezy Surround profile unless a measured/explicitly validated configuration proves it safe; avoid stacking an unverified room response.
- Distinguish command/IPC success from native effect activation and read-back. Report unsupported effects and partial application honestly.
- Acceptance evidence: JVM/native tests for invalid values, route conflict, unavailable effects, safe fallback, and profile switching; Android build on the exact merged SHA; level-matched A/B listening test before any superiority claim.

### FotoHolo / ARCore capture

- SAFMUS validates each coordinate as finite and within the declared capture domain before it reaches the exported cloud; apply voxel reduction and output caps deterministically.
- Preserve provenance: ARCore depth samples, estimated visual points, pose/tracking state, and inferred/unknown data must remain distinguishable. Never label estimated geometry as measured acoustics.
- Capture quality is a gate for export integrity, not a claim of architectural accuracy. Invalid data must be rejected; weak but structurally valid captures may be exported only with a degraded-quality flag.
- Export metadata must record optimizer version/parameters, input/output/rejected/reduced/truncated counts, tracking/pose quality, and the explicit fact that acoustic calibration is not ready without real acoustic measurements.
- Acceptance evidence: deterministic JVM tests for non-finite/out-of-domain coordinates, voxel boundaries, cap/truncation counts, missing poses, degraded capture, metadata/PLY consistency, and preservation of `acousticCalibrationReady=false`; ARCore device validation remains mandatory.

### 10D plan — integration order and exit criteria

1. **Route arbitration:** prove that exactly one route owns DSP and SAFMUS abstains on conflict/stale telemetry.
2. **Control plane:** trace each visible control through sanitation, bounded dispatch, acknowledgement/read-back, UI state, and rollback.
3. **Spatial path:** verify pose azimuth/elevation/distance propagation, HRIR selection by spherical angular distance, and ABI-compatible native snapshots.
4. **RIR/BRIR:** verify source provenance, sample-rate compatibility, deterministic latency fallback, and separate app-process versus daemon status.
5. **Room calibration:** permit correction only with actual measurements and an identified playback chain.
6. **Evolution:** require finite fitness, stable bounded candidates, headroom, latency evidence, and rollback.
7. **AR capture:** validate geometry/provenance and deterministic cloud optimization without converting geometry estimates into acoustic measurements.
8. **Surround profile:** apply bounded settings to the existing route and confirm effect availability/read-back.
9. **Regression/CI:** run the transversal wiring audit, JVM tests, native CTest, Android build, and APK integrity checks on the exact candidate SHA.
10. **Release/device validation:** publish only artifacts traceable to the green SHA; separately document device tests and level-matched listening results.

A step is not complete merely because its UI exists, a command was sent, a PR was opened, or a build started. Mark it complete only when its specified evidence is attached to the exact candidate commit.


## Hardware adaptation through the existing Magisk module (new acceptance contract)

SAFMUS must improve the way IVANNA uses available device hardware through the **same installed IVANNA Magisk module and the existing Android audio/camera service paths**. This is a capability and safety contract, not a promise that software can change the physical limits of a sensor or transducer.

### Shared architecture and permissions

- Keep a single policy/orchestration layer: SAFMUS evaluates capability, permission, route ownership, thermal/battery constraints, freshness, and confidence; IVANNA's existing subsystem remains the owner of the actual device API and DSP chain.
- Never open a second camera or microphone session just to run SAFMUS. Consume the same authorized capture/session or its explicitly shared metadata. If Android/vendor APIs do not permit safe sharing, report `UNAVAILABLE` and do not bypass permissions, privacy indicators, AppOps, SELinux, or foreground-service requirements.
- The Magisk module may provide privileged device-level integration only where the target device, Android version, vendor implementation, and policy explicitly support it. No blind writes to vendor camera/audio configuration, no global gain changes without bounded validation, and no system-file replacement as a generic optimization strategy.
- Every hardware feature is capability-detected at runtime. No sensor, HAL, effect, camera mode, or audio endpoint is assumed from a model name. Unsupported paths remain unchanged and report a reason.
- Separate requested settings from confirmed effective settings. A successful command, property write, IPC response, or permission grant is not proof that the HAL or physical hardware changed behavior.
- Keep a last-known-safe configuration and a reversible feature flag for every device-level modification. On crash, stale evidence, thermal pressure, route conflict, invalid read-back, or failed startup, restore the safe path and leave stock behavior intact.
- Store no camera frames or raw microphone audio for optimization telemetry by default. Prefer derived, bounded metadata; require explicit user consent for any diagnostic capture or persistence.

### Camera path

- SAFMUS may optimize only modes actually exposed by Camera2/CameraX or a supported vendor API: supported resolution/FPS combinations, capture-session configuration, stabilization/HDR/low-light modes, focus/exposure/white-balance behavior, and bounded frame-processing scheduling.
- Query camera characteristics and stream combinations first; validate requested mode against advertised capabilities, thermal budget, memory budget, and the active session. Do not claim higher sensor resolution, optical capability, or improved image quality solely because a setting was requested.
- IVANNA's FotoHolo/ARCore capture must use one coordinated camera session, preserve timestamps and tracking/depth provenance, and mark frames/poses stale when synchronization is lost. SAFMUS must not silently reconfigure the camera while ARCore owns a session.
- Any image enhancement runs in an explicitly budgeted pipeline with frame-drop/backpressure limits. If latency or thermal limits are exceeded, reduce optional processing or revert to the platform path; never block the camera callback on optimization work.
- Acceptance tests: unsupported capability, permission denial/revocation, camera busy/in-use, session reconfiguration failure, stale frame timestamps, ARCore session ownership, memory/thermal pressure, and rollback.

### Microphone path

- SAFMUS may refine capture only through a permitted, existing capture session and supported platform/vendor controls: source selection, sample-rate/channel configuration, supported noise suppression/echo cancellation/automatic gain controls, and bounded downstream filtering.
- Query the actual audio route and capture format. Do not assume that a requested sample rate, microphone array, beamforming mode, or vendor effect is active until read-back or a reliable platform status confirms it.
- Do not force-enable multiple competing noise suppressors, AGC, or echo cancellers. Determine which effect is already owned by the route and avoid stacking processing that causes pumping, clipping, phase distortion, or speech loss.
- Preserve consent and Android microphone privacy behavior. If IVANNA is not recording or does not hold the required permission, SAFMUS must not open a hidden microphone stream or keep a background capture alive.
- Acceptance tests: permission denied/revoked, input device change, unavailable effect, format mismatch, clipping/NaN, stale route telemetry, competing effect ownership, thermal/CPU overload, and exact restoration of prior settings.

### Native speakers, headphones, USB/Bluetooth audio

- Route optimization must reuse the existing AudioTrack/AudioEffect/AAudio/Oboe/daemon path selected by IVANNA. SAFMUS validates sample rate, channel mask, output device, supported effects, latency, gain, and headroom before proposing changes.
- Distinguish built-in speakers, wired output, USB DAC, Bluetooth codecs/endpoints, and vendor DSP using runtime route evidence. Never apply a speaker-specific EQ or calibration profile to a different endpoint after a route switch.
- Use measured device/room response for device-specific correction. A device model, preset name, or inferred speaker layout is not a measurement. Without measurements, permit only conservative bounded processing and label it uncalibrated.
- Enforce peak/headroom limits and a safe rollback. Do not change system-wide volume or vendor mixer controls unless the interface is explicitly supported, authorized, bounded, and has verified read-back. Protect the stock audio path if the daemon or module is unavailable.
- Keep Surround, HRTF, room correction, and vendor effects under one route arbiter. Prevent duplicate virtualizers, spatializers, bass enhancers, or reverbs; a requested effect that is unsupported or already owned elsewhere must be reported as unavailable/skipped, not falsely marked active.
- Acceptance tests: route hot-swap, unsupported endpoint, Bluetooth latency/codec change, effect contention, native effect read-back mismatch, clipping/headroom, daemon disconnect/restart, and rollback to the pre-optimization route.

### Shared device optimization lifecycle

1. Discover capabilities and current route without mutating hardware.
2. Obtain required runtime permission and confirm the owning session.
3. Capture freshness, thermal, memory, latency, and baseline evidence.
4. SAFMUS produces a bounded proposal; the existing IVANNA subsystem applies it.
5. Read back the effective configuration and compare against the requested change.
6. Accept only if the capability-specific postconditions pass; otherwise roll back.
7. Publish truthful UI status: `APPLIED`, `DEGRADED`, `UNAVAILABLE`, `DENIED`, or `ROLLED_BACK`, including a reason.
8. Log versioned, privacy-preserving metadata and test evidence; never record raw AV data by default.

### Release gates for device-level SAFMUS

- Unit/contract tests for capability parsing, numeric bounds, permissions, stale state, route ownership, read-back mismatch, and rollback.
- Instrumentation tests on supported Android versions and representative hardware for camera, microphone, built-in speaker, wired/USB, and Bluetooth paths where available.
- A/B comparisons with fixed content, route, environment, and level; record frame timing/drop rate for camera, capture latency/clipping for microphone, and output latency/true peak/underruns for audio.
- Clearly label host tests, emulator tests, device tests, and measured laboratory results. Emulator/host success is not physical-hardware validation.
- No global or vendor-level tuning is enabled by default until device-specific compatibility and recovery are demonstrated.


## Magisk runtime coordinator: whole-device environment

The existing `magisk_module/service.sh` now invokes
`magisk_module/core/safmus_environment_optimizer.sh` before starting the shared daemon.
The coordinator atomically publishes a permission-restricted snapshot at
`/data/adb/ivanna_omega/safmus/environment_profile.env` and a human-readable report beside it.
The snapshot records Android/device identity, available-memory estimate, CPU thread count,
readable thermal-zone maximum, and presence-only camera/media and sound device nodes. Missing
telemetry remains unknown; it is not guessed. A conservative resource budget is recorded when
memory is low or a readable thermal zone is at least 75 C, and SAFE_MODE takes precedence.

This is a read-only inventory and supervisory input, not a claim that the script changes camera
HAL controls or tunes every physical endpoint. Audio DSP thermal degradation remains owned by
the existing native `ThermalGovernor`; room correction remains owned by the existing
`RoomMeasurement` / `gateRoomCorrection` path and requires an explicit, user-authorized
measurement. Any future actuator must consume verified capabilities, respect Android permission
and privacy indicators, validate effective read-back, and retain a rollback path. Do not disable
SELinux, bypass AppOps, or write guessed vendor/sysfs values to manufacture a success state.
## Whole-device Android adapters — wired runtime path

`SafmusHardwareCoordinator` is started from `IVANNAApplication.onCreate()` alongside the existing `HardwareProbe`, `AudioRouteManager`, and `ThermalGovernor`. It continuously samples the high-priority signals (available memory, battery percentage, power-save state and thermal status), while capability inventories are sampled at normal load. `SafmusAdapterPolicy` adapts the coordinator cadence to 5 seconds at normal load, 8 seconds under conservative pressure, and 12 seconds in SAFE mode; display/sensor/camera inventory probes are skipped during pressure and the last known values are retained. It stores a local last-snapshot and resource budget in private app preferences; no telemetry is uploaded.

The coordinator feeds the shared resource budget into `ThermalGovernor.setExternalResourceBudget()`. The governor arbitrates the strongest of Android thermal pressure and SAFMUS resource pressure, then uses IVANNA's existing native DSP setters to reduce exciter, spatial width, and compressor load. Returning to the normal budget restores the baseline values unless thermal policy still requires a reduction. The same policy is applied to the active `AudioRouteManager` profile: under pressure it bounds extra dialogue boost and widener work, then reapplies the adjusted profile to the in-process DSP and daemon path without flushing active audio histories. This is an effective connection to existing audio paths, not a claim that the CPU/GPU clocks or camera/display HAL were directly modified.

### Adapter boundary and honest status

- **CPU / memory / power / thermal:** live signals drive the shared resource budget and existing DSP workload governor.
- **GPU / display:** current public API capability and refresh are observed. No generic global GPU clock or refresh write is attempted because Android/OEM support is device-specific and display mode selection belongs to a foreground window.
- **Audio input/output:** route inventory is observed; `AudioRouteManager`, the existing audio effects and the explicitly authorized room-measurement path remain owners of changes.
- **Camera / microphone:** capability is inventoried only. The coordinator does not open parallel sessions or change capture settings behind an app's owner; a future camera/capture adapter must be integrated into the actual owning session and verify effective read-back.
- **Sensors / haptics:** capability is inventoried without continuously activating sensors or triggering vibration.

The runtime budget is a bounded supervisory signal, not a universal hardware overclock. Device-specific actuators must be added only where the OEM API or an explicitly supported module control exists, with read-back, rollback, permission and thermal checks. The wiring checker now enforces the application → coordinator → existing DSP governor path.
