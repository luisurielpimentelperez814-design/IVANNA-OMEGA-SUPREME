# SAFMUS — Geometric perceptual optimizer research track

Status: **experimental reference implementation**. The existing bounded MAP solver remains the production/reference path. This module is deliberately not wired into the production DSP, JNI, or Android audio thread.

## Mathematical contract

Given a supplied symmetric Fisher-information estimate \(G(p)\) and objective gradient \(\nabla J(p)\), compute a regularized natural descent direction

\[
d = -(G(p)+\mu I)^{-1}\nabla J(p),
\qquad
\|d\|_{G+\mu I}\le r,
\]

where \(\|d\|_A=\sqrt{d^T A d}\), \(\mu\ge0\) is bounded diagonal damping, and \(r\) is a caller-provided trust radius. The 7-D implementation uses Cholesky factorization, fixed-size storage, no heap allocation, finite-value checks, a symmetry check, and fail-closed behavior if a sufficiently positive-definite metric cannot be obtained.

This is a **natural-gradient step**, not a proof of a globally optimal geodesic. It does not estimate \(q(y\mid p)\), learn Fisher information from listeners, solve the Wasserstein gradient flow, construct a room RKHS kernel, or establish zero crosstalk. Those require measured/validated inputs and separate models.

## Why this is the safe route

1. Keep the current exact quadratic MAP solver as a deterministic baseline.
2. Feed this prototype only a validated Fisher matrix and a gradient from an explicit perceptual objective.
3. Project the resulting step onto physical parameter bounds outside this helper.
4. Compare against MAP and Euclidean-gradient baselines using identical input signals and room measurements.
5. Enable production use only after offline numerical tests, real-time CPU/latency checks, and blinded listening tests pass.

## Claims and limits

- Cholesky success verifies numerical positive-definiteness of the regularized matrix at that call; it does not validate that the matrix represents human perception.
- The trust-region radius bounds a step in the supplied metric, not in JND units unless that metric has been psychophysically calibrated.
- No claim is made that the full variational problem has a closed-form solution, that all geodesics are unique, or that acoustic artifacts reach 0.00 dB.
- This is proprietary IVANNA/SAFMUS product code; it does not add an open-source license.

## Test coverage

The host tests verify the diagonal closed-form case, Fisher-norm trust cap, asymmetric/non-finite input rejection, safe damping for a singular metric, and rejection of a strongly indefinite metric. The tests are added to the existing CTest target and run in CI.
