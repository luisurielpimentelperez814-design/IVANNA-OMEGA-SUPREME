# Validation gates and remaining work

## Included and testable now
- Input validation and finite-value checks.
- Angular-spectrum propagation with zero-distance identity test.
- Energy conservation test for a smooth, propagating-band-limited field.
- Deterministic seeded phase-retrieval test and metrics sanity check.
- CLI outputs metrics, arrays and a config-hash manifest.

## Not certified by this package
- No Android/NDK/JNI build, device run, audio callback integration or route-specific audio test is claimed.
- No physical holographic effect is demonstrated; there is no SLM, laser, calibrated camera or optical bench.
- No independent solver comparison or experimentally measured optical efficiency yet.
- No evidence that optical holography improves acoustic output; live acoustic bridge is intentionally disabled.

## Required next steps
1. Expand tests across sampling pitch, propagation distance, wavelength, aliasing and grid-edge wraparound.
2. Compare numerical propagation with an independent Fresnel/angular-spectrum reference.
3. Add source aperture, phase quantization, pixel fill factor, aberrations and camera noise models if the actual hardware is specified.
4. Verify actual SOFA file HRIR extraction in the audio branch; signature checking/reconstruction from a master basis is not equivalent to reading measured SOFA impulse responses.
5. Correct/measure HRIR onset and ITD alignment before interpolation, then run listening/A-B tests.
6. Add JVM unit tests to CI (`testDebugUnitTest`) as well as native CTest.
7. Define a real SAF convergence metric and validated mapping from directional error inputs to latent target; a fixed placeholder is not convergence evidence.
8. Reconcile branch history with `main`, run full release CI and verify signed/reproducible artifacts.
9. For a real holographic output, validate against physical hardware and calibrated measurements.
