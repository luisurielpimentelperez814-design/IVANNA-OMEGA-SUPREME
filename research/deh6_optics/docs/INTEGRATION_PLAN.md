# Integration plan against IVANNA OMEGA SUPREME

## Reuse rather than duplicate
- Reuse the branch's measured-IHR1 loading/validation in `app/src/main/cpp/spatial/HRTFDatabase.cpp` and spatial interpolation in `HRTFInterpolator.cpp` for audio. Do not build a second HRTF database for this research package.
- Reuse existing SAF calibration/optimizer only for its existing acoustic purpose. Do not reinterpret its 5 directional energies or 7-D latent representation as optical phase without a separately validated mapping.
- Keep optical arrays and NumPy out of the Android real-time path. The prototype is host-side Python for quick numerical iteration and evidence capture.
- Before production merge, inspect current branch diff against `main`; the known branch has diverged (reported 15 commits ahead and 13 behind in the last snapshot). Reconcile changes and resolve conflicts explicitly; no blind merge/force push.

## Recommended repository destination
Place the package at `research/deh6_optics/` or a separate repository. If vendored into Android, exclude it from the APK and native audio build initially. Add the Python tests to a host CI job, separate from Gradle release assembly.

## Adapter contract
`interfaces/acoustic_bridge.schema.json` defines an offline JSON measurement record. The producer should capture the actual audio route and measured latency/true peak from instrumented runs; do not fabricate fields. The optical solver produces `metrics.json`. An experiment coordinator may combine them only when both records refer to the same experiment ID.

## Gates before any live integration
1. Numerical tests pass in clean environment.
2. Solver baseline documented with config and SHA-256.
3. Optical reconstruction validated against analytical/simple targets and an independent reference implementation.
4. Acoustic A/B test is preregistered, blind where practical, repeatable, and shows meaningful benefit.
5. No increased clipping, dropouts, latency, CPU, memory or thermal risk.
6. Android device/NDK/JNI integration tested on supported devices.
7. Physical holography claims require calibrated optical bench results; software simulation alone is insufficient.
