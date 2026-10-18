# DEH6 architecture and mathematical model

## Layering

1. **Existing audio engine (production):** Kotlin controls → JNI/native DSP → current spatial/audio processors. Reuse the current `HRTFDatabase` and `HRTFInterpolator` plus SAF calibration. Keep audio callback deterministic, bounded, allocation-free and independent of optical computation.
2. **DEH6 experiment coordinator:** owns run configuration, deterministic seeds, input hashes, results and validation gates. It is not a claim about an existing production class; this package defines the new boundary.
3. **Optics solver (host-side prototype):** sampled complex scalar field, angular-spectrum propagation, phase-only Gerchberg–Saxton loop, objective metrics.
4. **Evidence/adapter:** JSON records connect experiments without changing live audio state. A future Android/UI adapter can read results after a run; it must not block audio threads.
5. **Physical validation:** calibrated source/SLM/optics/camera, alignment, exposure controls, and comparison against predicted intensity are required before claiming a real holographic effect.

## Wave propagation

For wavelength `λ`, spatial frequencies `(fx, fy)`, and propagation distance `z`, angular-spectrum transfer is

`H(fx,fy;z) = exp(i 2π z sqrt(1/λ² - fx² - fy²))`.

For propagating components where `fx²+fy² ≤ 1/λ²`, multiply the Fourier transform of the input field by `H`, then inverse-transform. Evanescent components are discarded by default. The simulation assumes scalar, monochromatic, paraxial-independent angular-spectrum propagation on a sampled periodic grid; finite-aperture, polarization, aberration, pixel fill factor, sensor noise and optical hardware are not modeled.

## Phase-only reconstruction

Let desired target-plane amplitude be `A_t(x,y)` and source-plane illumination amplitude be `A_in`. Initialize phase `φ₀` from a seeded RNG. At iteration `k`:

1. `U'_k = P_z[A_in exp(i φ_k)]`
2. `V_k = A_t exp(i arg(U'_k))` (replace output amplitude while retaining phase)
3. `W_k = P_-z[V_k]`
4. `φ_(k+1) = arg(W_k)` and source field `U_(k+1)=A_in exp(i φ_(k+1))`.

`P_z` is the angular-spectrum propagation operator. The problem is non-convex; the algorithm can stagnate and a visually plausible result is not itself evidence of physical accuracy.

## Metrics

- Normalized amplitude RMSE: `sqrt(mean((|U_rec|/max|U_rec| - A_t)^2))` after normalizing `A_t` to unit maximum.
- Amplitude correlation: Pearson correlation between flattened normalized target and reconstruction amplitudes.
- Target-region energy fraction: energy in pixels where `A_t ≥ 0.5`, divided by total reconstructed energy. This is a concentration metric, not optical efficiency; optical efficiency needs calibrated incident and delivered power measurements.
- Repeatability: deterministic seed + config SHA-256 in the manifest.

## Audio interface rationale

Optical field phase is not interchangeable with acoustic phase or HRTF. There is no presumed benefit from wiring the hologram solver into audio. The only supported interface is an offline experiment record with acoustic route, sample rate, latency, true peak and optical metrics. A live bridge remains disabled unless a preregistered A/B test shows a repeatable audio benefit without regressions in latency, clipping, stability or accessibility.
