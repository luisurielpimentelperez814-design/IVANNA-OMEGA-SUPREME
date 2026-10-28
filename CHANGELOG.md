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

