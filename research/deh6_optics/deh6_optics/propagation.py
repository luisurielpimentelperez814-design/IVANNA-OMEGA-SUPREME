"""Scalar monochromatic angular-spectrum propagation for sampled complex fields."""
from __future__ import annotations
import numpy as np


def angular_spectrum(field: np.ndarray, wavelength_m: float, dx_m: float,
                     distance_m: float, *, evanescent: str = "discard") -> np.ndarray:
    """Propagate a 2-D complex field by distance z using the angular spectrum.

    Uses the exp(+i 2π z sqrt(1/λ²-fx²-fy²)) convention. Evanescent terms are
    discarded by default; `decay` applies their physical exponential decay.
    Array samples are square with pitch dx_m. No paraxial approximation is used.
    """
    u = np.asarray(field, dtype=np.complex128)
    if u.ndim != 2 or min(u.shape) < 2:
        raise ValueError("field must be a 2-D array at least 2x2")
    if not np.isfinite(u).all():
        raise ValueError("field must be finite")
    if not (np.isfinite(wavelength_m) and wavelength_m > 0):
        raise ValueError("wavelength_m must be finite and > 0")
    if not (np.isfinite(dx_m) and dx_m > 0):
        raise ValueError("dx_m must be finite and > 0")
    if not np.isfinite(distance_m):
        raise ValueError("distance_m must be finite")
    if evanescent not in ("discard", "decay"):
        raise ValueError("evanescent must be 'discard' or 'decay'")

    ny, nx = u.shape
    fx = np.fft.fftfreq(nx, d=dx_m)[None, :]
    fy = np.fft.fftfreq(ny, d=dx_m)[:, None]
    q = 1.0 / wavelength_m**2 - fx**2 - fy**2
    propagating = q >= 0
    kz = 2.0 * np.pi * np.sqrt(np.maximum(q, 0.0))
    transfer = np.exp(1j * kz * distance_m)
    if evanescent == "discard":
        transfer = transfer * propagating
    else:
        alpha = 2.0 * np.pi * np.sqrt(np.maximum(-q, 0.0))
        transfer = np.where(propagating, transfer, np.exp(-alpha * abs(distance_m)))
    return np.fft.ifft2(np.fft.fft2(u) * transfer)
