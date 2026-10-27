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

