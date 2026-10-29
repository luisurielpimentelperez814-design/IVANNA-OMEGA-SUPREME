#!/usr/bin/env python3
"""Identificacion reproducible del estimador de deriva del reloj de salida USB (AudioTrackBufferPll).

Modelo de observacion (medido en el Moto G85 + DAC USB 'JieLi' full-speed, dump de AudioFlinger, captura
ivanna_capture_20261028_171142): timestamps del HAL con jitter gaussiano sigma=0.137 ms y picos atipicos
positivos de 2..109 ms (prob. ~5e-4 por muestra). Muestreo cada 250 ms. Compara:
  - OLS de 8 puntos + filtro alpha=0.2 (estimador anterior)  - Theil-Sen de 30 s (estimador actual)
Uso: usb_drift_estimator_sim.py [--seed N]   (exit 1 si el estimador actual supera los umbrales)
Umbrales (fijados antes de evaluar): std < 5 ppm y max|err| < 12 ppm en regimen (t > 100 s).
"""
import argparse, sys
import numpy as np
FS = 48000.0

def observe(true_ppm, rng, secs=300, poll=0.25, sigma=0.137e-3, p_spike=5e-4):
    n = int(secs / poll); t = np.arange(n) * poll
    tt = t + rng.normal(0, sigma, n); sp = rng.random(n) < p_spike
    tt[sp] += rng.uniform(0.002, 0.109, sp.sum())
    return t * FS * (1 + true_ppm * 1e-6), tt

def ols8(f, tt, W=8, alpha=0.2, gate=2000):
    est, out = None, []
    for i in range(W, len(tt)):
        t = tt[i-W:i] - tt[i-W]; x = f[i-W:i] - f[i-W]; t -= t.mean(); x -= x.mean()
        d = ((t * x).sum() / (t * t).sum() / FS - 1) * 1e6
        if abs(d) <= gate: est = d if est is None else est + alpha * (d - est)
        out.append(0.0 if est is None else est)
    return np.array(out)

def theil_sen(f, tt, N=120, pts=40, alpha=0.2):
    est, out = None, []
    for i in range(N, len(tt)):
        idx = np.arange(i - N, i)[::int(np.ceil(N / pts))]; t, x = tt[idx], f[idx]
        a, b = np.triu_indices(len(idx), 1)
        d = (np.median((x[b] - x[a]) / (t[b] - t[a])) / FS - 1) * 1e6
        est = d if est is None else est + alpha * (d - est); out.append(est)
    return np.array(out)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--seed", type=int, default=11); a = ap.parse_args()
    rng = np.random.default_rng(a.seed); ok = True
    print(f"{'drift real':>10} | {'OLS8 std':>9} {'max|err|':>9} | {'Theil-Sen 30 s std':>19} {'max|err|':>9}")
    for true in (+10.0, -18.0, 0.0, +100.0):
        f, tt = observe(true, rng); o = ols8(f, tt)[400:]; s = theil_sen(f, tt)[280:]   # ambos en regimen (t > 100 s)
        so, ss = o.std(), s.std(); mo, ms = np.abs(o - true).max(), np.abs(s - true).max()
        print(f"{true:+10.1f} | {so:9.2f} {mo:9.1f} | {ss:19.2f} {ms:9.2f}")
        ok &= (ss < 5.0 and ms < 12.0)
    print("UMBRALES (std<5 ppm, max<12 ppm):", "OK" if ok else "FALLA"); return 0 if ok else 1

if __name__ == "__main__": sys.exit(main())
