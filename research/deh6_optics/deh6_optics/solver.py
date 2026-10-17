"""Phase-only Gerchberg–Saxton solver and reproducible reconstruction metrics."""
from __future__ import annotations
import numpy as np
from .propagation import angular_spectrum


def _validate_target(target: np.ndarray) -> np.ndarray:
    t = np.asarray(target, dtype=np.float64)
    if t.ndim != 2 or min(t.shape) < 2:
        raise ValueError("target amplitude must be a 2-D array at least 2x2")
    if not np.isfinite(t).all() or (t < 0).any():
        raise ValueError("target amplitude must be finite and non-negative")
    if float(t.max()) <= 0:
        raise ValueError("target amplitude must contain non-zero energy")
    return t / t.max()


def gerchberg_saxton(target_amplitude: np.ndarray, wavelength_m: float, dx_m: float,
                     distance_m: float, *, iterations: int = 100, seed: int = 7,
                     input_amplitude: np.ndarray | None = None) -> tuple[np.ndarray, np.ndarray, list[float]]:
    """Return (phase, reconstructed complex field, normalized error history).

    Constraints: phase-only modulator at source plane; target amplitude at output.
    Input amplitude defaults to a uniform plane wave. This is a non-convex solver;
    convergence and visual quality are not guaranteed for arbitrary targets.
    """
    target = _validate_target(target_amplitude)
    if not isinstance(iterations, int) or iterations < 1 or iterations > 10000:
        raise ValueError("iterations must be an integer in [1, 10000]")
    shape = target.shape
    if input_amplitude is None:
        ain = np.ones(shape, dtype=np.float64)
    else:
        ain = np.asarray(input_amplitude, dtype=np.float64)
        if ain.shape != shape or not np.isfinite(ain).all() or (ain < 0).any() or ain.max() <= 0:
            raise ValueError("input_amplitude must match target shape and be finite/non-negative/nonzero")
        ain = ain / ain.max()
    rng = np.random.default_rng(seed)
    source = ain * np.exp(1j * rng.uniform(-np.pi, np.pi, shape))
    history: list[float] = []
    for _ in range(iterations):
        out = angular_spectrum(source, wavelength_m, dx_m, distance_m)
        out_amp = np.abs(out)
        scale = float(np.linalg.norm(target.ravel()) / max(np.linalg.norm(out_amp.ravel()), 1e-15))
        normalized_out = out_amp * scale
        err = float(np.linalg.norm((normalized_out - target).ravel()) / max(np.linalg.norm(target.ravel()), 1e-15))
        history.append(err)
        constrained_out = target * np.exp(1j * np.angle(out))
        back = angular_spectrum(constrained_out, wavelength_m, dx_m, -distance_m)
        source = ain * np.exp(1j * np.angle(back))
    reconstruction = angular_spectrum(source, wavelength_m, dx_m, distance_m)
    return np.angle(source), reconstruction, history


def reconstruction_metrics(target_amplitude: np.ndarray, reconstruction: np.ndarray) -> dict[str, float]:
    """Compute scale-normalized RMSE, correlation and energy concentration metrics."""
    target = _validate_target(target_amplitude)
    field = np.asarray(reconstruction, dtype=np.complex128)
    if field.shape != target.shape or not np.isfinite(field).all():
        raise ValueError("reconstruction must be finite and have target shape")
    amp = np.abs(field)
    norm = float(amp.max())
    if norm <= 0:
        raise ValueError("reconstruction has zero amplitude")
    amp = amp / norm
    rmse = float(np.sqrt(np.mean((amp - target) ** 2)))
    a, b = target.ravel(), amp.ravel()
    if np.std(a) == 0 or np.std(b) == 0:
        corr = 1.0 if np.allclose(a, b) else 0.0
    else:
        corr = float(np.corrcoef(a, b)[0, 1])
    target_power = target**2
    rec_power = amp**2
    mask = target >= 0.5
    target_energy = float(target_power.sum())
    concentration = float(rec_power[mask].sum() / max(rec_power.sum(), 1e-15))
    return {
        "normalized_rmse": rmse,
        "amplitude_correlation": corr,
        "target_region_energy_fraction": concentration,
        "target_energy_fraction_reference": float(target_power[mask].sum() / max(target_energy, 1e-15)),
    }
