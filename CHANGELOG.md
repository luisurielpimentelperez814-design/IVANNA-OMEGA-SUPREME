## v2.5.189 — SAFMUS bidirectional clock-drift polarity proof

- Adds a deterministic JVM regression for a consumer clock running 1,000 ppm slower than nominal, complementing the positive-drift test.
- Verifies signed OLS drift, measured rate, and actuator polarity: a slower consumer clock must not receive the same correction sign as a faster one.
- Advances all release metadata to v2.5.189 / versionCode 480 because v2.5.188 is already immutable and its latest build cannot be attached to that tag.
- Release remains contingent on GitHub Actions passing the Android/JVM/native gates and verifying the exact APK and Magisk ZIP assets; no physical HAL/PAL or Bluetooth clock-control claim is made.

## v2.5.188 — SAFMUS clears stale clock drift when presented frames stall

- Treats an advancing monotonic timestamp with a frozen presented-frame position as a stalled/invalid clock observation instead of retaining an old OLS drift estimate in feed-forward control.
- Clears stale measured rate and drift while retaining the independent buffer-occupancy PI, so queue recovery can continue without trusting a stopped clock.
- Adds a JVM regression for a previously locked +1,000 ppm clock that then stops advancing; verifies drift lock and stale telemetry are cleared and the correction remains bounded.
- Aligns release metadata at v2.5.188 / versionCode 479. CI and device playback measurements remain required; this does not claim physical HAL/PAL or Bluetooth clock control.

## v2.5.187 — SAFMUS anti-windup saturation boundary and drift deadband

- Fixes the PI anti-windup decision to evaluate the current unsaturated actuator demand, allowing the controller to reach the exact ±500 ppm clamp instead of freezing one integral step early.
- Adds a 0.5 ppm feed-forward deadband so numerical OLS noise around a nominal clock cannot create non-zero resampler corrections while buffer occupancy is inside its deadband.
- Keeps the ±500 ppm actuator clamp, bounded slew, clock-estimator validation and release JVM tests enabled.
- Release candidate only: GitHub Actions must pass all JVM/native tests and verify APK/ZIP integrity before v2.5.187 is considered released.

## v2.5.186 — SAFMUS clock reacquisition and release metadata gate

- Includes the clock-regression outlier fix: invalid OLS windows discard stale drift feed-forward and reacquire from a fresh monotonic pair.
- Adds release-time consistency checks across version.properties, module.prop, update.json version/versionCode and the canonical ZIP URL; publication fails closed on mismatch.
- Aligns app, module, update manifest and README at v2.5.186 / versionCode 477.
- CI must pass and publish/verify the exact commit artifacts before this version is considered released.

## v2.5.185 — SAFMUS rejects contaminated clock-regression windows

- When an OLS clock-rate estimate exceeds the validated range, clear the contaminated rolling window and stale drift feed-forward, then reacquire from the newest monotonic frame/timestamp pair.
- Keep the independently measured buffer-occupancy PI active; the rejected clock estimate no longer silently remains in control.
- Add a JVM regression proving one impossible frame/timestamp sample drops drift lock and clears stale rate telemetry.
- Version metadata aligned at v2.5.185 / versionCode 476. CI and device measurements remain the acceptance gates.

## v2.5.184 — SAFMUS buffer deadband and anti-windup on the live AudioTrack servo

- Implements a continuous 2% normalized buffer-occupancy deadband in the existing single production controller, preventing small queue-measurement noise from hunting the resampler.
- Adds conditional integration anti-windup so persistent error cannot wind the integral farther into saturation of the ±500 ppm actuator.
- Adds JVM regressions for deadband quietness and sustained saturation bounds. The live AudioTrackBufferPll → StereoAudioResampler.setClockCorrectionPpm() wiring remains the sole actuator path for IVANNA's owned player.
- No second controller is introduced; daemon/PAL telemetry is not cross-fed into the app-owned AudioTrack. Physical DAC/HAL/PAL or remote Bluetooth clock control remains unclaimed.
- Aligns version metadata at v2.5.184 / versionCode 475; CI publication and device validation remain required.

## v2.5.183 — ASRC telemetry expires when its producer stops

- Adds a monotonic publication timestamp to the existing shared snapshot. Readers reject future or stale samples (default maximum age: 1 second), preventing persisted TRACKING state from masquerading as live control after producer failure.
- Adds a regression test for stale-snapshot rejection with a shorter explicit age budget.
- Aligns version metadata at v2.5.183 / versionCode 474.

## v2.5.182 — ASRC controller rejects untrackable and nonphysical configurations

- The regression estimator has a fixed nine-anchor window. Configurations requesting nine or more intervals could never enter TRACKING; the constructor now rejects them fail-closed.
- Correction and rejection thresholds must remain below 1,000,000 ppm so the documented inverse-ratio mapping cannot reach a zero or negative denominator. Invalid settings publish FAULT/unity instead of silently running an impossible controller.
- Adds regression tests for an oversized observation window and a non-positive-ratio configuration.
- Aligns version metadata at v2.5.182 / versionCode 473.

## v2.5.181 — ASRC shared-state IPC removes the non-atomic reason-string race

- Replaces the shared-memory reason[24] character array with six lock-free atomic 32-bit words. A sequence counter alone does not make concurrent non-atomic string copies race-free.
- Publisher rejects invalid enum states, non-finite drift/target/ratio values, and non-positive ratios. Reader validates state and numeric fields after a stable sequence read, rejecting corrupt telemetry.
- Adds host regression coverage for NaN, infinity, and non-positive ASRC ratios.
- Aligns version.properties, module.prop, Magisk updater metadata and README at v2.5.181 / versionCode 472.
- This hardens software telemetry; it does not claim physical HAL/PAL ASRC actuation or device-level acoustic certification.

## v2.5.180 — Stereo ASRC rejects incomplete interleaved frames safely

- Closes a malformed-input edge case in `StereoAudioResampler.process()`: an odd-length interleaved L/R float array previously passed through unchanged on the unity-rate fast path, while resampling paths silently ignored the final unmatched sample. The two paths therefore had inconsistent frame contracts.
- The resampler now removes only the unmatched trailing sample before either fast-path or FIR processing. This preserves L/R parity, prevents a partial frame from leaking through, and leaves the state/output for valid even-length PCM unchanged.
- Adds JVM regressions for unity-rate transparency with odd-length input and equivalence between malformed input and its complete stereo prefix during resampling.
- Updates README release metadata and aligns `version.properties`, `module.prop`, and Magisk updater metadata at v2.5.180 / versionCode 471.
- Release acceptance remains gated on CI tests, Android build, APK/ZIP integrity and publication. This software fix does not certify physical Bluetooth, DAC, PAL or HAL clock correction.

## v2.5.179 — TinyML ring-buffer overflow is now counted, not silently dropped

- `IvannaTinyMLAudioKernel::ingestPcmBlock()` called `SpscAudioRingBuffer::push()` (declared `[[nodiscard]]` for a reason) and discarded its `bool` result, twice. If the inference worker thread ever fell behind the audio producer (thermal throttling, priority inversion, scheduling starvation) a full ring silently dropped decimated audio feature samples with zero counter, zero log, zero way to observe it from outside. Normal load never hits this (ring holds ~1s of decimated audio against a worker that drains every 2ms), but a silent failure mode in a real-time acoustic-classification path is still a defect in its own right.
- Fix: both `push()` call sites now check the return value and increment a new relaxed atomic `m_ringOverflowCount` on failure — one `fetch_add`, no branch in the common (success) path beyond the existing check, no allocation, no I/O, RT-safe. Exposed via `ringOverflowCount()`.
- Also fixed the matching consumer-side `pop()` in `workerInferenceLoop()`: it is an SPSC invariant that pop() cannot fail right after the `available() >= kHopLengthSamples` check, but the code previously discarded the result anyway. Now guarded: on an (expected-never) pop failure, the loop skips that hop instead of running inference on a stale/partially-shifted frame buffer.
- Two new regression tests in `test_tinyml_kernel.cpp`: `RingOverflowCounterStaysZeroUnderNormalLoad` (realistic load keeps the counter at 0) and `RingOverflowIsCountedNotSilentlyDropped` (a single synchronous call producing ~8x ring capacity in decimated samples must register at least one counted overflow, and the kernel must remain in a valid, crash-free state afterward — confirmed by a follow-up ingest + control read producing finite values).
- Verified: full native CTest suite rebuilt and run clean host-side with clang-18 (598/598, up from 596 before this change), including both new tests actually passing against the real fix, not just compiling.

## v2.5.178 — re-applies two previously-correct dead-code deletions

- Re-deletes `AudioResampler.kt` and `HeadTrackingManager.kt`. Both were already deliberately removed once, with the right justification: `6ed52c42` ("0 referencias; AudioPipeline ya hace el 48→16 kHz inline") and `a901e749` ("0 referencias; duplicado de IvannaHeadTracker, ya cableado vía IvannaSpatialManager"). Both got silently resurrected by an unrelated commit (`88c4bc9d`, the Acoustic Unity Engine feature) that evidently merged from a branch based on an older tree — git history shows the exact same two files being re-added by that single commit alongside otherwise-legitimate new work.
- Re-verified before deleting again, not just trusted the old commit messages: grepped the full `app/src/main` tree for both class names outside their own declaration files — zero hits for either. `StereoAudioResampler` (not `AudioResampler`) is the real, tested, production resampler (`IvannaBridgePlayer.kt`, `AudioTrackBufferPll`, and three JVM test suites). `IvannaHeadTracker` (not `HeadTrackingManager`) is the real, tested, production head tracker, wired in `IvannaSpatialManager.kt` via `nativeObjectRendererSetHeadTracker`.
- No functional/behavioral change: this removes dead weight, it does not touch the live audio or spatial path. Flagging in case any other session's branch still carries the old resurrected copies — merging it back in would undo this a third time.

## v2.5.177 — reroute-vs-drift guard, AutoEQ corpus/Kotlin gate, dead-code removal

- Fixes a real Bluetooth-vs-clock-drift ambiguity in the app-owned AudioTrack path: Android can silently reroute a playing `AudioTrack` to a different output device (A2DP connect/disconnect, output-policy change) without the app recreating the track. That reroute is a one-step latency change, not a sustained clock-rate error, but `AudioTrackBufferPll`'s rolling regression had no way to tell the two apart. Adds `AudioTrackRerouteGuard`, a single pure function wired into `IvannaBridgePlayer`'s existing 250ms clock poll: on a confirmed `routedDevice.id` change it resets the PLL and fails closed to unity correction instead of feeding the discontinuity into the drift estimate.
- Removes `AudioTrackClockDriftServo` and its test: a pre-unification (pre-v2.5.173) drift estimator left with zero production consumers after `AudioTrackBufferPll` became the single controller authority. Confirmed via repo-wide grep before deletion.
- Adds `tests/test_autoeq_kotlin_matches_extractor.py`, wired into CI: re-runs `tools/hpir/extract_hpir_profiles.py` against the real HpIR SOFA corpus and parses `AutoEqManager.kt`'s hardcoded `PROFILES` literal, failing the build if they diverge. Closes the exact drift risk `AutoEqManager.kt` already documented from an earlier incident (2026-09-10) with no automated gate behind it.
- Documents a verified, unresolved data-provenance issue found while building that gate: `Data.IR` in `hpir_AKGK271MKII_nh719.sofa` and `hpir_AKGK272HD_nh719.sofa` is bit-for-bit identical (`np.array_equal` true) despite being distinct SOFA files for distinct headphone models. The "AKG K272 HD" AutoEQ profile is therefore the K271 MKII measurement under another name, not an independent measurement — flagged in code and here rather than presented as real. No fix is possible without a genuine K272 HD measurement, which is not in this repository.
- Host verification performed this change: `test_shm_lifecycle` and the end-to-end SCM_RIGHTS handshake reproduction (49 + 7 checks, unrelated to this change but re-run to confirm a clean baseline), `test_variable_asrc`/`AsrcClockController` (13/13, including the `InvertedSignWouldBeDetected` negative test), `validate_sofa_hdf5.py` against all 216 real `.sofa` assets (0 failures), `check_safmus_transversal_wiring.py` (PASS), `sync_asrc.py --self-test` (PASS), and the new `AudioTrackRerouteGuardTest`/`AudioTrackBufferPllTest` compiled and run standalone with a host `kotlinc` + JUnit (6/6, 2/2). The new `AudioTrackRerouteGuard` wiring inside `IvannaBridgePlayer.kt` itself could **not** be compiled or run in this environment — the Android Gradle plugin's dependencies (`com.android.tools:r8`, etc.) are not reachable through this sandbox's network allowlist. That compile/build/instrumented verification is left to CI and real-device testing; it is not claimed here.

## v2.5.176 — SAFMUS ASRC actuator polarity regression

- Adds an Android JVM regression proving that positive clock-correction ppm produces more output frames than negative ppm in the actual app-owned `StereoAudioResampler` path.
- Exercises the bounded ±500 ppm actuator at 48 kHz with deterministic stereo audio and rejects a reversed correction sign or a correction that has no measurable output-rate effect.
- This verifies the software resampler contract only; it does not certify USB DAC, Bluetooth sink-clock, PAL/HAL, or system-wide clock correction.
- Version metadata: v2.5.176 / versionCode 467. Availability requires the current commit's native tests, JVM tests, Android build, artifact-integrity checks and release publication to pass.

## v2.5.175 — SAFMUS time-evolving acoustic regression and release candidate

- Carries forward the SAFMUS bounded clock/FIFO servo, Q0.32 fractional phase stepping, 64-tap Kaiser ASRC reference, clock-loop feasibility checks, and GCC-PHAT delay-polarity regression gates.
- Includes the latest native WFS time-evolving acoustic-trajectory regression added on main; CI must execute it with the complete native DSP suite.
- Aligns version metadata for a fresh release pipeline run so the verified APK and ARM64 Magisk ZIP are built from the same commit.
- Scope remains explicit: the host ASRC reference is not a PAL/HAL/DAC actuator. Live drift correction claims remain limited to the app-owned AudioTrack route; Bluetooth sink-clock and system-wide correction require separate device evidence.
- Release is considered usable only after the current commit's Android build, JVM tests, APK/ZIP integrity gates and published assets all pass.
- Version metadata: v2.5.175 / versionCode 466.

## v2.5.174 — SAFMUS sync/ASRC mathematical reference and release verification

- Adds a deterministic host reference for the bounded clock/FIFO servo, Q0.32 fractional phase stepping, 64-tap Kaiser ASRC, clock-loop bandwidth feasibility, and GCC-PHAT fractional-delay polarity.
- Adds regression gates for unit-DC phase kernels, unity-ratio frame advance, accumulated fractional phase, infeasible jitter headroom, bounded controller output/covariance, and GCC-PHAT delay sign.
- Makes CI execute the reference self-test and its regression suite alongside the native DSP and Android packaging gates.
- Evidence boundary: the reference model is not a runtime actuator. Live correction remains scoped to IVANNA's app-owned AudioTrack; no system-wide, PAL/HAL, DAC or Bluetooth sink-clock correction is claimed.
- Release acceptance requires the current commit's Android build, staged ZIP/APK integrity checks, and publication workflow to pass.
- Version metadata: v2.5.174 / versionCode 465.

## v2.5.173 — SAFMUS unified app-owned clock servo

- Unifies the app player's clock-drift feed-forward and buffer-occupancy PI into one actuator path; removes the separate diagnostic estimator from the live correction loop.
- Adds centered rolling least-squares estimation over paired AudioTrack presented-frame/timestamp samples, bounded drift filtering, ±500 ppm output limits, and 25 ppm/update slew limiting.
- Preserves fail-closed unity correction on invalid timestamps, regressing frame/time pairs, impossible queue occupancy, or controller reset.
- Adds JVM regression tests for clock-slope polarity, feed-forward correction, bounded control, and timestamp-regression reset.
- Scope is the app-owned AudioTrack only. The daemon's native controller remains separate; no claim of PAL/HAL, physical DAC, Bluetooth sink-clock, or system-wide correction.
- Version metadata: v2.5.173 / versionCode 464. Release requires passing native tests, Android JVM tests, build, and artifact verification.

## v2.5.172 — SAFMUS clock-drift estimator and ASRC ratio contract

- Replaces interval-only drift smoothing with a fixed-size rolling least-squares estimate over paired cumulative frame positions and monotonic timestamps.
- Defines the native ASRC ratio as input frames consumed per output frame; positive source-clock drift advances input consumption rather than increasing output production.
- Adds finite/range validation, timestamp/frame regression rejection, outlier hold, a configurable deadband, bounded correction and slew limiting, with unity-ratio fail-closed snapshots.
- Adds native regression tests for drift estimation, ratio polarity, deadband, invalid-clock behavior, bounds and slew.
- Documents that the native controller is not wired into the live player actuator; the existing player continues to use its AudioTrack buffer PLL and StereoAudioResampler. No claim of PAL/HAL, system-wide, Bluetooth-sink oscillator control, or hardware certification.
- Version metadata: v2.5.172 / versionCode 463. CI and release-artifact verification must pass before the release is considered available.

## v2.5.171 — Resilient adaptive telemetry UI

- Rejects incomplete or non-finite native telemetry snapshots before they can paint plausible-but-invalid meters; shows a clear invalid-data message instead.
- Clamps signal and spectral display ranges and guards meter normalization against NaN, infinity and invalid ranges to prevent broken progress bars.
- Uses `Locale.ROOT` for numeric telemetry so decimal separators remain stable across device locales and comparisons.
- Keeps telemetry read-only; no synthetic fallback values are presented as measured audio.
- Version metadata: v2.5.171 / versionCode 462. CI must pass native tests, Android build, artifact integrity and release publication before the package is considered ready.

## v2.5.170 — Responsive OMNI visual hierarchy

- Replaces the single-line OMNI status-chip row with a responsive FlowRow capped at three chips per line. Route and processing indicators remain visible on narrow screens and under larger font settings instead of competing horizontally with the product title.
- Preserves the existing Aurora Obsidiana palette and real-state labels; no decorative status is presented as proof of active audio processing.
- Updates README release badge and replaces stale v2.5.155/v2.5.150 notes with the current v2.5.170 scope and explicit hardware-validation limits.
- Version metadata: v2.5.170 / versionCode 461.

## v2.5.169 — Limiter telemetry correctness

- Fixes a control-loop blind spot: SafetyLimiter previously reported 0 dB reduction for peaks above its threshold but below its ceiling, despite applying attenuation. Telemetry now derives target gain reduction from the limiter's bounded block gain.
- Adds a regression test for the threshold-to-ceiling region.
- Aligns AdaptiveDecisionEngine's headroom reference and conversion default with the actual SafetyLimiter ceiling (0.98855).
- Version metadata: v2.5.169 / versionCode 460.

## v2.5.168 — Adaptive engine audit and coordination-state repair

- Reconciles the agent coordination ledger with current code: `AdaptiveDecisionEngine` is wired in Ruta A (JNI) and Ruta B (`omega_effect`); the old “not consumed / tests empty” statements are explicitly marked historical.
- Preserves the evidence boundary: CI verifies host tests, Android packaging, staged ZIP/APK integrity and publication; it does not claim physical-device acoustic or real-time jitter certification.
- Release metadata synchronized: `version.properties`, `magisk_module/module.prop`, and `magisk_module/update.json` use v2.5.168 / versionCode 459.

## v2.5.167 — SOFA integrity gate and verified pipeline

- Adds real HDF5 open/parse and dataset-read validation for every repository `.sofa` file, with bounded-memory traversal.
- Adds regression tests for valid SOFA-like HDF5, corrupt files, and invalid convention metadata.
- Makes the native CI gate run SOFA validation, SAFMUS wiring checks, adversarial/synthetic audio tests, auditory-gate policy tests, and the native CTest suite before Android packaging.
- Verifies the downloaded staged APK/ZIP bundle and runs OEM stability/performance and IAEL audio certification gates before publication.
- This release does not claim measured acoustic superiority: real-device loopback, route-specific latency, HRTF perceptual validation, and calibrated listening measurements remain evidence-gated.

## v2.5.166 — Release recovery and Genezi Spatial AUX

- Genezi AUX profile uses the conservative stereo width multiplier (1.08) with bass/dialog boosts disabled in the route profile.
- Documents the ideal Mid/Side model and distinguishes it from the frequency-dependent native DSP implementation.
- CI Android SDK provisioning retries failed package downloads and clears partial SDK download caches after corrupted NDK archives.
- Release remains gated on successful Android build, unit tests, and artifact integrity verification.

 — v2.5.165 (456) — Genezi Spatial publication repair

## Release integrity
- Carries forward the Genezi Spatial AUX beta and canonical Mid/Side model specification.
- Corrects the updater metadata versionCode mismatch and aligns module.prop, update.json, and version.properties at 456.
- Release is considered published only after CI publishes and verifies the versioned Magisk ZIP and APK assets.

