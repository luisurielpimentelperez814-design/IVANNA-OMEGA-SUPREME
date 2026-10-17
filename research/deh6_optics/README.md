# IVANNA OMEGA SUPREME — DEH6 / Optics-Holography Integration Package

**Purpose:** integration-ready, isolated numerical prototype for the requested DEH6 architecture and photoholography/nanotomix research track. It is deliberately separated from the production audio callback. It reuses the repository's existing spatial concepts (`HRTFDatabase`, `HRTFInterpolator`, and SAF calibration) through a narrow, optional measurement interface; it does not replace them or claim optical effects are an audio DSP effect.

## Status and scope
- Includes executable scalar-wave propagation (angular-spectrum method), Gerchberg–Saxton phase retrieval, target-plane reconstruction, numerical quality metrics, JSON CLI, tests and an example.
- Integration boundary is defined in `interfaces/`. No optical result is injected into live audio. An acoustic bridge must be justified by measured A/B improvement first.
- This is a **simulation prototype**, not a physical holographic display or a validated Android feature. Physical optics needs calibrated hardware and laboratory validation.
- It does not merge or modify the GitHub repository automatically. Apply the integration plan only on the work branch after reviewing the current diff.

## Requirements
Python 3.10+, NumPy. Install with `python -m pip install -r requirements.txt`.

## Run
```bash
python -m pip install -r requirements.txt
python -m unittest discover -s tests -v
python -m deh6_optics.cli --config examples/target.json --out results/demo
```
Outputs: `metrics.json`, `target.npy`, `reconstruction.npy`, `phase.npy`, and `run_manifest.json`.

## Suggested integration order
1. Run tests and preserve the current HRTF/SAF branch.
2. Keep `deh6_optics` as a host-side research module; do not add it to the real-time audio callback.
3. Reuse validated HRTF assets/interpolator for audio spatialization; keep the optics solver independent.
4. Define an adapter only for offline analysis/visualization. Require reproducible A/B evidence before any audio-control bridge.
5. Merge/reconcile branch history with `main` before opening a production PR; never force-merge divergent histories.

See `docs/ARCHITECTURE_AND_MATH.md`, `docs/INTEGRATION_PLAN.md`, and `docs/VALIDATION_GATES.md`.
