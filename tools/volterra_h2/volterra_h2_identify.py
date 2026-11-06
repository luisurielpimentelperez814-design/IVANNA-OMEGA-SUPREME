#!/usr/bin/env python3
"""
volterra_h2_identify.py — Fase IV: Volterra H2 identification pipeline.

Scope (evidence-first):
  * H1 (linear kernel): directly identifiable from the REW sweep dataset —
    the stored impulse response IS the measured H1 (room + chain included).
  * H2 (symmetric quadratic kernel): NOT identifiable from this public
    dataset. The .mdat stores only post-processed curves (magnitude, phase,
    THD+N, harmonic magnitudes) — no raw excitation, no raw output, no
    excitation phase, no two-tone/multitone records, and a single
    excitation level per modality. This module implements the estimator
    that WOULD apply if probes existed, validates it exclusively on
    explicitly synthetic Volterra systems, and refuses underdetermined
    data instead of fabricating coefficients.

Model (discrete time, symmetric truncated kernels):
    y[n] = sum_{k=0}^{K-1} h1[k] x[n-k]
         + sum_{k=0}^{K-1} sum_{l=k}^{K-1} h2[k,l] x[n-k] x[n-l]

The quadratic design uses the row-major upper-triangle index map used by
VolterraH2Symmetric::processInterleaved:
i = k*K - k*(k+1)/2 + l  (0-based, k<=l).
Do not replace this with column-major packing without changing the C++ reader.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field

import numpy as np


# ----------------------------------------------------------------------------
# Index map: symmetric quadratic kernel (triangular storage)
# ----------------------------------------------------------------------------
def triangular_index_map(K: int):
    """Return [(k, l)] pairs in the order used for the flattened h2 vector."""
    pairs = []
    for k in range(K):
        for l in range(k, K):
            pairs.append((k, l))
    return pairs


def triangular_size(K: int) -> int:
    return K * (K + 1) // 2


# ----------------------------------------------------------------------------
# Synthetic Volterra system (explicitly labeled synthetic; never a calibration)
# ----------------------------------------------------------------------------
def apply_volterra(x: np.ndarray, h1: np.ndarray, h2: np.ndarray) -> np.ndarray:
    """Apply y = H1{x} + H2{x,x} with symmetric h2 in triangular storage.

    Single, verifiable formulation:
        y[n] = sum_k h1[k] x[n-k]
             + sum_{(k,l), k<=l} h2[i(k,l)] x[n-k] x[n-l]
    """
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    K = len(h1)
    y = np.zeros(n, dtype=np.float64)
    for k in range(K):
        y[k:] += h1[k] * x[: n - k]
    pairs = triangular_index_map(K)
    for idx, (k, l) in enumerate(pairs):
        m = n - l
        if m <= 0:
            continue
        # y[l:] += h2[idx] * x[l-k]*x[...]: x[n-k]*x[n-l] evaluated for n >= l
        # C++ expands the symmetric matrix sum: off-diagonal (k,l)
        # contributes both (k,l) and (l,k), hence the factor 2.
        symmetry = 1.0 if k == l else 2.0
        y[l:] += symmetry * h2[idx] * x[: n - l] * x[l - k: n - k]
    return y


# ----------------------------------------------------------------------------
# Identification
# ----------------------------------------------------------------------------
@dataclass
class IdentifyResult:
    ok: bool
    reason: str = ""
    h1: np.ndarray | None = None
    h2: np.ndarray | None = None
    cond_number: float = float("nan")
    n_samples: int = 0
    n_params: int = 0
    relative_residual: float = float("nan")
    ridge: float = 0.0
    synthetic: bool = False
    details: dict = field(default_factory=dict)


def estimate_kernels_time_domain(x: np.ndarray, y: np.ndarray, K: int,
                                 ridge: float = 1e-6,
                                 max_cond: float = 1e8) -> IdentifyResult:
    """Jointly estimate h1 (K) and symmetric h2 (K(K+1)/2) from ONE known
    input/output record via regularized least squares.

    Refuses (ok=False) when the record is too short relative to the parameter
    count or the design is numerically underdetermined. Never invents
    coefficients for a rank-deficient system.
    """
    x = np.asarray(x, dtype=np.float64)
    y = np.asarray(y, dtype=np.float64)
    n = len(y)
    n_lin = K
    n_quad = triangular_size(K)
    n_params = n_lin + n_quad
    if n < 4 * n_params:
        return IdentifyResult(ok=False,
                              reason=f"insufficient data: {n} samples < 4x{n_params} params")

    # linear convolution design: cols = x[n-k]
    cols = [np.zeros(n) for _ in range(K)]
    for k in range(K):
        cols[k][k:] = x[: n - k]
    # quadratic design: cols[j] = x[j-k]*x[j-l] for j >= l (k<=l), 0 elsewhere
    pairs = triangular_index_map(K)
    for (k, l) in pairs:
        c = np.zeros(n)
        symmetry = 1.0 if k == l else 2.0
        c[l:] = symmetry * x[: n - l] * x[l - k: n - k]
        cols.append(c)
    A = np.column_stack(cols)

    # Column normalization (standard least-squares conditioning): solve in the
    # scaled space, then unscale. Prevents the inherently large dynamic range
    # of quadratic product columns from ruining the Gram matrix conditioning.
    norms = np.linalg.norm(A, axis=0)
    if np.any(norms <= 0.0):
        return IdentifyResult(ok=False, reason="zero-energy design column",
                              n_samples=n, n_params=n_params)
    An = A / norms

    # regularized least squares (Tikhonov); refuse if effectively underdetermined
    AtA = An.T @ An
    AtA += ridge * np.eye(n_params)
    AtY = An.T @ y
    try:
        cond = float(np.linalg.cond(AtA))
    except np.linalg.LinAlgError:
        return IdentifyResult(ok=False, reason="singular Gram matrix", n_samples=n,
                              n_params=n_params)
    if not np.isfinite(cond) or cond > max_cond:
        return IdentifyResult(ok=False, reason=f"design ill-conditioned (cond={cond:.3g})",
                              n_samples=n, n_params=n_params, cond_number=cond)
    theta_n = np.linalg.solve(AtA, AtY)
    theta = theta_n / norms
    resid = y - A @ theta
    denom = float(np.sum(y * y)) or 1.0
    return IdentifyResult(
        ok=True, h1=theta[:K], h2=theta[K:K + n_quad], cond_number=cond,
        n_samples=n, n_params=n_params,
        relative_residual=float(np.sum(resid ** 2) / denom), ridge=ridge)


def identify_h1_from_impulse_response(ir: np.ndarray) -> dict:
    """H1 identification from a measured impulse response (Fase IV step 1).

    The REW sweep impulse response is the measured linear kernel of the
    whole chain (source -> room -> mic). Returns frequency-domain H1.
    """
    ir = np.asarray(ir, dtype=np.float64)
    if ir.size == 0 or not np.all(np.isfinite(ir)):
        return {"ok": False, "reason": "empty or non-finite impulse response"}
    H = np.fft.rfft(ir)
    f = np.fft.rfftfreq(ir.size)
    return {"ok": True, "H1_complex": H, "freqs": f,
            "magnitude_db": 20.0 * np.log10(np.maximum(np.abs(H), 1e-30))}


# ----------------------------------------------------------------------------
# Identifiability verdict for the public dataset
# ----------------------------------------------------------------------------
DATASET_FACTS = {
    "raw_excitation_signal": False,
    "raw_output_signal": False,
    "excitation_phase_known": False,
    "two_tone_measurements": False,
    "multitone_measurements": False,
    "excitation_level_series": False,
    "harmonic_magnitude_curves": True,
    "harmonic_phase_data": False,
    "thdn_curves_31_points": True,
    "sweep_impulse_responses": True,
    "imd_records": False,
}


def identifiability_verdict(facts: dict | None = None) -> dict:
    """Enumerate exactly which Volterra H2 parameters are observable."""
    f = dict(DATASET_FACTS)
    if facts:
        f.update(facts)
    observable = []
    unobservable = []
    observable.append("H1 (linear chain response): identified from the 6 stored "
                      "impulse responses (1,048,576 samples stored at 96 kHz, "
                      "acquired from 48 kHz sweeps).")
    if not (f["raw_excitation_signal"] and f["raw_output_signal"]):
        unobservable.append(
            "h2[k,l] time-domain kernel values: require concurrent raw excitation "
            "and raw output records (none are stored; MeasData.ir=null, "
            "audioDataLength=0 in all 12 measurements).")
    if not f["excitation_phase_known"]:
        unobservable.append(
            "complex H2 (magnitude AND phase) per harmonic: only magnitudes are "
            "stored (dBHarmonicData), no phase, so the complex quadratic transfer "
            "function cannot be formed.")
    if not f["excitation_level_series"]:
        unobservable.append(
            "separation of H2 from compression/AGC and level-dependent behavior: "
            "requires a level series (>=2 levels); the sweeps use -12 dBFS only "
            "and the stepped-sine runs -20 dBFS only.")
    if not f["two_tone_measurements"]:
        unobservable.append(
            "intermodulation terms h2(f1,f2) at sum/difference frequencies: "
            "distortionData.isIMD=false and imddB placeholders are 0.0; no "
            "two-tone record exists.")
    return {"observable": observable, "unobservable": unobservable,
            "verdict": ("H2 is NOT identifiable from this dataset; H1 is. Any H2 "
                        "loaded from this data would be fabricated.")}


# ----------------------------------------------------------------------------
# Synthetic validation harness (algorithm validation ONLY)
# ----------------------------------------------------------------------------
def validate_pipeline_synthetic(seed: int = 2026) -> dict:
    rng = np.random.default_rng(seed)
    K = 8
    n = 8192
    h1_true = np.zeros(K)
    h1_true[0] = 1.0
    h1_true[1] = -0.35
    h1_true[3] = 0.12
    h2_true = 0.02 * rng.standard_normal(triangular_size(K))
    x = 0.5 * rng.standard_normal(n)
    y = apply_volterra(x, h1_true, h2_true)
    res = estimate_kernels_time_domain(x, y, K, ridge=1e-8)
    out = {"synthetic": True, "K": K, "n": n, "ok": res.ok,
           "cond_number": res.cond_number, "relative_residual": res.relative_residual}
    if res.ok:
        err_h1 = float(np.linalg.norm(res.h1 - h1_true) / np.linalg.norm(h1_true))
        err_h2 = float(np.linalg.norm(res.h2 - h2_true) / np.linalg.norm(h2_true))
        out.update({"h1_relative_error": err_h1, "h2_relative_error": err_h2})
    # insufficient-data refusal path
    short = estimate_kernels_time_domain(x[:64], y[:64], K)
    out["short_record_refused"] = not short.ok
    out["short_record_reason"] = short.reason
    return out


if __name__ == "__main__":
    verdict = identifiability_verdict()
    synth = validate_pipeline_synthetic()
    payload = {"identifiability": verdict, "synthetic_validation": synth}
    print(json.dumps(payload, indent=1))
