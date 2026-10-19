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
