#!/usr/bin/env python3
"""SAFMUS synchronization/ASRC executable reference model and deterministic smoke test.

This is a host-side mathematical reference, not a claim of HAL/DAC control.
Production routing must bind measurements and the ASRC actuator to the same
owned stream. Sustained rho > 1 with a rate-scaled kernel remains unsupported.
"""
from __future__ import annotations

import argparse
import math
import sys
import numpy as np

H = 32
A_DB = 130.0
BETA = 0.1102 * (A_DB - 8.7)
P = 1024
ZETA = 1.0
UMAX = 500e-6
WN_DT_MAX = 0.1
WN_TD_MAX = 0.265
Q32 = 1 << 32


def kernel(x):
    x = np.asarray(x, dtype=np.float64)
    inside = np.abs(x) < H
    w = np.where(
        inside,
        np.i0(BETA * np.sqrt(np.clip(1.0 - (x / H) ** 2, 0.0, None))) / np.i0(BETA),
        0.0,
    )
    return np.sinc(x) * w


def make_table():
    phase = np.arange(P + 2, dtype=np.float64)[:, None] / P
    tap = np.arange(2 * H, dtype=np.float64)[None, :]
    table = kernel(phase + (H - 1) - tap)
    # Unit-DC normalization per phase prevents phase-dependent level modulation.
    sums = table.sum(axis=1, keepdims=True)
    if np.any(~np.isfinite(sums)) or np.any(np.abs(sums) < 1e-12):
        raise ValueError("invalid ASRC phase kernel")
    return (table / sums).astype(np.float32)


def asrc_step(buf, i, mu, rho, table):
    """One output sample. rho is input frames consumed per output frame.

    mu is an unsigned Q0.32 fractional phase. The caller owns the input history
    and must provide at least H past frames and H+1 available frames.
    """
    if not math.isfinite(rho) or rho <= 0.0:
        raise ValueError("rho must be finite and positive")
    if i < H - 1 or i + H >= len(buf):
        raise ValueError("input window does not contain the required 64 taps")
    if not 0 <= mu < Q32:
        raise ValueError("mu must be an unsigned Q0.32 fraction")
    phase = mu >> 22
    alpha = (mu & 0x3FFFFF) / float(1 << 22)
    taps = table[phase] * (1.0 - alpha) + table[phase + 1] * alpha
    sample_window = np.asarray(buf[i - (H - 1): i + H + 1], dtype=np.float64)
    out = float(np.dot(taps, sample_window))
    advance = int(rho * Q32)
    phase_sum = mu + advance
    i += phase_sum >> 32
    mu = phase_sum & 0xFFFFFFFF
    return out, i, mu


class SyncLoop:
    """Two-state Kalman estimate [FIFO occupancy error, fractional drift]."""

    def __init__(self, fs, dt, fn, sigma_nu, Sg=1e-16, umax=UMAX,
                 nis_thr=9.0, nis_n=5, boost=1e6):
        if not all(math.isfinite(v) for v in (fs, dt, fn, sigma_nu, Sg, umax)):
            raise ValueError("non-finite SyncLoop configuration")
        if fs <= 0 or dt <= 0 or fn <= 0 or sigma_nu < 0 or Sg < 0 or umax <= 0:
            raise ValueError("invalid SyncLoop configuration")
        self.fs, self.dt, self.umax = fs, dt, umax
        wn = 2.0 * math.pi * fn
        if wn * dt > WN_DT_MAX:
            raise ValueError("wn*dt exceeds the validated discrete-time limit")
        self.Kp = 2.0 * ZETA * wn / fs
        self.Ki = wn * wn / fs
        self.Kt = 1.0 / self.Kp
        self.F = np.array([[1.0, dt * fs], [0.0, 1.0]])
        self.G = np.array([-dt * fs, 0.0])
        self.Sg = Sg
        self.R = sigma_nu * sigma_nu
        self.x = np.zeros(2)
        self.P = np.diag([10.0, (20e-6) ** 2])
        self.I = 0.0
        self.nis_thr, self.nis_n, self.boost = nis_thr, nis_n, boost
        self.cnt, self.mult = 0, 1.0

    def step(self, z):
        if not math.isfinite(z):
            self.I = 0.0
            return 0.0
        S = float(self.P[0, 0] + self.R)
        if not math.isfinite(S) or S <= 0.0:
            self.x[:] = 0.0
            self.P = np.diag([10.0, (20e-6) ** 2])
            self.I = 0.0
            self.cnt, self.mult = 0, 1.0
            return 0.0
        innov = float(z - self.x[0])
        nis = innov * innov / S
        self.cnt = self.cnt + 1 if nis > self.nis_thr else 0
        self.mult = self.boost if self.cnt >= self.nis_n else max(1.0, self.mult * 0.9)
        K = self.P[:, 0] / S
        self.x = self.x + K * innov
        # Joseph-stabilized scalar-measurement covariance update.
        KH = np.outer(K, np.array([1.0, 0.0]))
        I2 = np.eye(2)
        self.P = (I2 - KH) @ self.P @ (I2 - KH).T + np.outer(K, K) * self.R
        self.P = 0.5 * (self.P + self.P.T)
        eh, yh = self.x
        u = yh + self.Kp * eh + self.Ki * self.I
        us = float(np.clip(u, -self.umax, self.umax))
        self.I += self.dt * (eh + self.Kt * (us - u))
        self.x = self.F @ self.x + self.G * us
        Q = np.diag([1e-3 * self.dt, self.Sg * self.dt * self.mult])
        self.P = self.F @ self.P @ self.F.T + Q
        self.P = 0.5 * (self.P + self.P.T)
        if not np.all(np.isfinite(self.x)) or not np.all(np.isfinite(self.P)):
            self.x[:] = 0.0
            self.P = np.diag([10.0, (20e-6) ** 2])
            self.I = 0.0
            return 0.0
        return us


def seleccionar_wn(fs, Td, dt, Dy_max, B_t, B_min, B_max, sigma_jit,
                   t_set, ydot_max, ess_max):
    values = (fs, Td, dt, Dy_max, B_t, B_min, B_max, sigma_jit,
              t_set, ydot_max, ess_max)
    if not all(math.isfinite(v) for v in values):
        raise ValueError("non-finite design input")
    if fs <= 0 or Td <= 0 or dt <= 0 or Dy_max < 0 or B_min >= B_t or B_t >= B_max:
        raise ValueError("invalid clock/buffer design bounds")
    if sigma_jit < 0 or t_set <= 0 or ydot_max < 0 or ess_max <= 0:
        raise ValueError("invalid noise or settling design bounds")
    w_max = min(WN_TD_MAX / Td, WN_DT_MAX / dt)
    e_bud = min(B_t - B_min, B_max - B_t) - 3.0 * sigma_jit
    if e_bud <= 0:
        raise ValueError("INFACTIBLE: no buffer headroom after jitter margin")
    w_buf = 0.368 * fs * Dy_max / e_bud
    w_set = 6.8 / t_set
    w_ramp = math.sqrt(fs * ydot_max / ess_max)
    wn = 1.2 * max(w_buf, w_set, w_ramp)
    if wn > w_max:
        raise ValueError("INFACTIBLE: mas buffer, menos Td o mejor oscilador")
    return wn


def gcc_phat(left, right, fs, taumax_s=0.75e-3, eps=1e-3, up=8, band=(100, 16000)):
    left, right = np.asarray(left, dtype=np.float64), np.asarray(right, dtype=np.float64)
    if left.ndim != 1 or right.ndim != 1 or len(left) != len(right) or len(left) < 4:
        raise ValueError("GCC-PHAT inputs must be equal-length 1-D arrays")
    if not all(math.isfinite(v) for v in (fs, taumax_s, eps)) or fs <= 0 or taumax_s <= 0 or eps <= 0 or up < 1:
        raise ValueError("invalid GCC-PHAT configuration")
    n = len(left)
    G = np.fft.fft(left) * np.conj(np.fft.fft(right))
    scale = eps * float(np.mean(np.abs(G)))
    G = G / (np.abs(G) + scale + np.finfo(float).tiny)
    f = np.fft.fftfreq(n, 1.0 / fs)
    G *= ((np.abs(f) >= band[0]) & (np.abs(f) <= band[1]))
    Gp = np.zeros(n * up, dtype=np.complex128)
    half = n // 2
    Gp[:half] = G[:half]
    Gp[-half:] = G[-half:]
    cc = np.real(np.fft.ifft(Gp)) * up
    tm = min(int(round(taumax_s * fs)), n // 2 - 1)
    lags = np.arange(-tm * up, tm * up + 1)
    corr = cc[lags % len(cc)]
    idx = int(np.argmax(corr))
    frac = 0.0
    if 0 < idx < len(corr) - 1:
        denom = corr[idx - 1] - 2.0 * corr[idx] + corr[idx + 1]
        if abs(denom) > 1e-15:
            frac = 0.5 * (corr[idx - 1] - corr[idx + 1]) / denom
            frac = float(np.clip(frac, -1.0, 1.0))
    return (lags[idx] + frac) / up / fs


def self_test():
    table = make_table()
    assert table.shape == (P + 2, 2 * H)
    assert np.all(np.isfinite(table))
    assert np.max(np.abs(table.sum(axis=1) - 1.0)) < 2e-6
    dc_out, dc_i, dc_mu = asrc_step(np.ones(128, dtype=np.float32), 64, 0, 1.0, table)
    assert abs(dc_out - 1.0) < 2e-6 and (dc_i, dc_mu) == (65, 0)

    # Q0.32 phase accumulator must preserve fractional phase without wrap drift.
    rho = 1.0 + 10e-6
    mu, idx = 0, H
    for _ in range(200_000):
        advance = int(rho * Q32)
        total = mu + advance
        idx += total >> 32
        mu = total & 0xFFFFFFFF
    expected = 200_000 * rho
    assert abs((idx + mu / Q32) - (H + expected)) < 1e-3

    # Closed-loop sign and safety: positive drift grows FIFO unless u is positive.
    loop = SyncLoop(48_000, 0.01, 0.1, 0.5, Sg=1e-16)
    u_values = []
    e = 0.0
    y = 8e-6
    for k in range(500):
        u = loop.step(e)
        u_values.append(u)
        e += 0.01 * 48_000 * (y - u)
    assert all(math.isfinite(v) and abs(v) <= UMAX for v in u_values)
    assert np.all(np.isfinite(loop.P))
    assert np.allclose(loop.P, loop.P.T, atol=1e-12)

    # Deterministic design example from the specification.
    wn = seleccionar_wn(48_000, 0.05, 0.01, 10e-6, 2000, 0, 4000,
                        2, 30, 1e-6 / 60, 0.01)
    assert 0.33 < wn < 0.35, wn

    # GCC-PHAT sign convention tau = t_L - t_R.
    rng = np.random.default_rng(20261027)
    x = rng.normal(size=8192)
    delay = 7
    left = x
    right = np.concatenate([np.zeros(delay), x[:-delay]])
    tau = gcc_phat(left, right, 48_000, taumax_s=0.75e-3, up=8)
    assert tau < 0.0 and abs(tau + delay / 48_000) < 1.0 / (48_000 * 8) * 1.5, tau

    print("sync_asrc.py self-test: PASS")
    print(f"ASRC: taps={2*H}, phases={P}, phase-sum-error={np.max(np.abs(table.sum(axis=1)-1.0)):.3g}")
    print(f"loop: max-|u|={max(abs(v) for v in u_values)*1e6:.3f} ppm, covariance finite/symmetric")
    print(f"selector: wn={wn:.4f} rad/s, fn={wn/(2*math.pi):.4f} Hz")
    print(f"GCC-PHAT: tau={tau*1e6:.3f} us")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="run deterministic numerical smoke tests")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        print("Reference module loaded. Run with --self-test for deterministic validation.")
