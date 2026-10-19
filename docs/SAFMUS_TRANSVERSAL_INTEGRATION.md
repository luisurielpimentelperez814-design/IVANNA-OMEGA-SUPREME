# SAFMUS transversal integration — audit and acceptance contract

**Status at this commit:** shared deterministic policy is implemented and JVM-tested for the FotoHolo route adapter, HRTF/RIR/correction eligibility, and evolutionary proposal safety. This is not a claim that every application control is already wired to the policy, that a learned SAFMUS model exists, or that acoustic/perceptual performance has been validated.

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

## Remaining integration work

The shared kernel is an initial central policy boundary. Full universal integration still requires each control/UI environment and both app/system-effect processes to call it; a cross-process state bridge must be explicitly verified rather than inferred from a local snapshot. The native RIR coupling, HRTF read-back, evolutionary candidate validation/activation, and UI read-back must each be audited and tested on their real call paths. No final release should be claimed until Android/JVM/native CI and artifact-to-SHA checks pass.
