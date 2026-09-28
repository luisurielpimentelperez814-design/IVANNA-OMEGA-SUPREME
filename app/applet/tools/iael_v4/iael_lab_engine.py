#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IAEL v4 — Laboratorio de Certificación de Audio de IVANNA OMEGA SUPREME.

Reemplaza de raíz las generaciones v1/v2/v3 (SNR con "ruido" de referencia
fijo inventado, análisis espectral por índice temporal, stress sin métricas
de calidad) por un motor de medición defendible y reproducible:

  * THD+N  — método IEC: fundamental + 10 armónicos + banda de rechazo,
             pico localizado con interpolación parabólica.
  * SNR    — potencia del fundamental / potencia total fuera de banda,
             excluyendo armónicos y subarmónicos (no un 1e-6 inventado).
  * IMD    — SMPTE RP-120 4:1 (60 Hz + 7 kHz), laterales 7000±n*60 en dB.
  * Balance espectral por bandas ISO de octava (31.5 Hz–16 kHz) vía FFT.
  * Coherencia estéreo — correlación de Pearson + error de fase espectral.
  * Transitorio — overshoot y settle time sobre escalón.
  * Validez — NaN/Inf, clipping >= 1.0, pico.
  * Bit-exactness — bypass exacto si error máximo < 1e-9.
  * Latencia — cross-correlación FFT entrada↔salida (modo captura WAV).

Modos:
  python3 iael_lab_engine.py --mode self            # auto-certificación (identidad)
  python3 iael_lab_engine.py --mode wav --in  a.wav --out b.wav   # pipeline real

La semilla de fase es fija → cada corrida es reproducible byte a byte.
"""

import argparse
import json
import math
import os
import sys
import time

import numpy as np

try:
    from scipy import signal as ssignal
    HAVE_SCIPY = True
except Exception:  # pragma: no cover
    HAVE_SCIPY = False

__version__ = "4.0.0"
SYSTEM = "IVANNA OMEGA SUPREME IAEL v4"
SEED = 0xC0FFEE
NOISE_FLOOR_DB = -192.0          # piso numérico reportable (24-bit)
BAND_EXCL_HZ = 15.0              # banda de exclusión alrededor de cada armónico
ISO_BANDS_HZ = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
ANALYSIS_MIN_HZ = 20.0
ANALYSIS_MAX_HZ = 20000.0

# ----------------------------------------------------------------------------
# utilidades numéricas
# ----------------------------------------------------------------------------

def _pow2db(p):
    return 10.0 * math.log10(p) if p > 1e-30 else NOISE_FLOOR_DB - 60.0


def _amp2db(a):
    return 20.0 * math.log10(a) if a > 1e-15 else NOISE_FLOOR_DB - 60.0


def _json_default(o):
    """Serializa escalares numpy (bool_/int_/float_/ndarray) como nativos JSON."""
    if isinstance(o, np.bool_):
        return bool(o)
    if isinstance(o, np.integer):
        return int(o)
    if isinstance(o, np.floating):
        return float(o)
    if isinstance(o, np.ndarray):
        return o.tolist()
    raise TypeError("Tipo no serializable: %r" % (o,))


def spectral(x, sr):
    """rfft con ventana Hann + normalización coherente."""
    n = len(x)
    w = np.hanning(n)
    xw = (x - np.mean(x)) * w
    X = np.fft.rfft(xw)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    mag = np.abs(X)
    # compensación de la ventana (coherent gain ~0.5)
    mag /= max(np.sum(w), 1e-12)
    return freqs, mag


def peak_interp(freqs, mag, k, sr, n):
    """Interpolación parabólica del pico espectral (mayor exactitud de f0)."""
    if k <= 0 or k >= len(mag) - 1:
        return freqs[k], mag[k]
    a, b, c = mag[k - 1], mag[k], mag[k + 1]
    den = a - 2.0 * b + c
    if abs(den) < 1e-15:
        return freqs[k], b
    d = 0.5 * (a - c) / den
    return freqs[k] + d * (sr / n), b


def band_power(freqs, mag, fc, bw):
    idx = np.abs(freqs - fc) <= bw
    return float(np.sum(mag[idx] ** 2))


def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2)))


def peak(x):
    return float(np.max(np.abs(x))) if len(x) else 0.0


def crest(x):
    r = rms(x)
    return float(peak(x) / r) if r > 1e-30 else 0.0


def validity(x):
    a = np.asarray(x, dtype=np.float64)
    bad = int(np.count_nonzero(~np.isfinite(a)))
    clipped = int(np.count_nonzero(np.abs(a) >= 1.0))
    return bad, clipped, float(np.max(np.abs(a)))


# ----------------------------------------------------------------------------
# métricas de calidad sobre una señal mono
# ----------------------------------------------------------------------------

def thd_n_db(x, sr, f0, nharm=10, excl=BAND_EXCL_HZ):
    """THD+N según IEC 60268: (armónicos 2..N + banda de rechazo) / fundamental."""
    n = len(x)
    freqs, mag = spectral(x, sr)
    lo = max(int(np.searchsorted(freqs, f0 - 20.0)), 1)
    hi = int(np.searchsorted(freqs, f0 + 20.0))
    if hi <= lo:
        return 0.0
    k = lo + int(np.argmax(mag[lo:hi]))
    f_est, _ = peak_interp(freqs, mag, k, sr, n)
    bw = max(excl, 3.0 * sr / n)
    p_fund = band_power(freqs, mag, f_est, bw)
    p_harm = sum(band_power(freqs, mag, h * f_est, bw) for h in range(2, nharm + 1))
    excl_mask = freqs < ANALYSIS_MIN_HZ
    for h in range(1, nharm + 1):
        excl_mask |= np.abs(freqs - h * f_est) <= bw
    excl_mask |= freqs > ANALYSIS_MAX_HZ
    p_rest = float(np.sum(mag[~excl_mask] ** 2))
    denom = p_fund if p_fund > 1e-30 else 1e-30
    return 10.0 * math.log10((p_harm + p_rest) / denom)


def snr_db(x, sr, f0, excl=BAND_EXCL_HZ):
    """SNR real: fundamental vs todo el residuo fuera de su banda (sin armónicos contados aparte)."""
    n = len(x)
    freqs, mag = spectral(x, sr)
    lo = max(int(np.searchsorted(freqs, f0 - 20.0)), 1)
    hi = int(np.searchsorted(freqs, f0 + 20.0))
    if hi <= lo:
        return 0.0
    k = lo + int(np.argmax(mag[lo:hi]))
    f_est, _ = peak_interp(freqs, mag, k, sr, n)
    bw = max(excl, 3.0 * sr / n)
    p_fund = band_power(freqs, mag, f_est, bw)
    mask = (freqs < ANALYSIS_MIN_HZ) | (freqs > ANALYSIS_MAX_HZ)
    mask |= np.abs(freqs - f_est) <= bw
    p_rest = float(np.sum(mag[~mask] ** 2))
    denom = p_rest if p_rest > 1e-30 else 1e-30
    return 10.0 * math.log10(max(p_fund, 1e-30) / denom)


def imd_smpte_db(x, sr):
    """IMD SMPTE RP-120 4:1 — tono bajo 60 Hz (4x) + alto 7 kHz (1x); miden laterales."""
    f_low, f_high = 60.0, 7000.0
    n = len(x)
    freqs, mag = spectral(x, sr)
    idx_h = np.where((freqs >= f_high - 30) & (freqs <= f_high + 30))[0]
    if len(idx_h) == 0:
        return 0.0
    k_h = int(idx_h[0] + np.argmax(mag[idx_h]))
    f_est_h, _ = peak_interp(freqs, mag, k_h, sr, n)
    bw = max(BAND_EXCL_HZ, 3.0 * sr / n)
    p_high = band_power(freqs, mag, f_est_h, bw)
    sides = 0.0
    for nside in range(1, 6):
        for sign in (-1, 1):
            sides += band_power(freqs, mag, f_est_h + sign * nside * f_low, bw)
    denom = p_high if p_high > 1e-30 else 1e-30
    return 10.0 * math.log10(max(sides, 1e-30) / denom)


def iso_octave_profile(x, sr):
    """Energía RMS por banda ISO de octava y desviación de planitud (dB)."""
    n = len(x)
    freqs, mag = spectral(x, sr)
    profile = {}
    edges = [21.0] + [b * math.sqrt(2.0) for b in ISO_BANDS_HZ]
    mids = [math.sqrt(edges[i] * edges[i + 1]) for i in range(len(edges) - 1)]
    for mid, lo_e, hi_e in zip(mids, edges[:-1], edges[1:]):
        idx = (freqs >= lo_e) & (freqs <= hi_e)
        p = float(np.sum(mag[idx] ** 2))
        profile[round(mid)] = round(_pow2db(p), 3)
    vals = np.array([10 ** (v / 20.0) for v in profile.values()])
    ref = float(np.mean(vals)) if len(vals) else 1e-30
    flatness_db = round(_amp2db(ref), 3)
    delta = {str(k): round(v - flatness_db, 3) for k, v in profile.items()}
    peak_band = max(delta, key=lambda k: abs(delta[k]))
    return {
        "bands_db_spl": {str(k): v for k, v in profile.items()},
        "mean_db": flatness_db,
        "delta_db_vs_mean": delta,
        "worst_band_hz": int(peak_band),
        "worst_delta_db": delta[peak_band],
        "flatness_pp_db": round(max(delta.values()) - min(delta.values()), 3),
    }


def transient_metrics(x, sr, step_index=480, settle_db=1.0):
    """Overshoot y settle time sobre escalón (para medir rampas/ataques)."""
    a = np.asarray(x, dtype=np.float64)
    n = len(a)
    if step_index >= n:
        return {"valid": False}
    target = float(a[step_index])
    if abs(target) < 1e-12:
        return {"valid": False}
    post = a[step_index:]
    peak_post = float(np.max(np.abs(post)))
    overshoot_pct = max(0.0, (peak_post / abs(target) - 1.0) * 100.0)
    settle_idx = 0
    tol = abs(target) * (10 ** (settle_db / 20.0) - 1.0)
    for i, v in enumerate(post):
        if abs(v - target) <= tol:
            settle_idx = i
            break
    return {
        "valid": True,
        "overshoot_pct": round(overshoot_pct, 4),
        "settle_ms": round(settle_idx * 1000.0 / sr, 3),
        "target": round(target, 6),
    }


# ----------------------------------------------------------------------------
# métricas estéreo
# ----------------------------------------------------------------------------

def stereo_coherence(left, right):
    l = np.asarray(left, dtype=np.float64)
    r = np.asarray(right, dtype=np.float64)
    n = min(len(l), len(r))
    l, r = l[:n], r[:n]
    if n < 8 or float(np.std(l)) < 1e-12 or float(np.std(r)) < 1e-12:
        return {"valid": False}
    corr = float(np.corrcoef(l, r)[0, 1])
    return {"valid": True, "pearson": round(corr, 6)}


def stereo_phase_error(left, right, sr, lo_hz=100.0, hi_hz=4000.0):
    n = min(len(left), len(right))
    if n < 64:
        return {"valid": False}
    l = np.asarray(left[:n], dtype=np.float64)
    r = np.asarray(right[:n], dtype=np.float64)
    X = np.fft.rfft(l * np.hanning(n))
    Y = np.fft.rfft(r * np.hanning(n))
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    idx = (freqs >= lo_hz) & (freqs <= hi_hz) & (np.abs(X) > 1e-9) & (np.abs(Y) > 1e-9)
    if not np.any(idx):
        return {"valid": False}
    ph = np.angle(X[idx] * np.conj(Y[idx]))
    w = np.abs(X[idx]) * np.abs(Y[idx])
    err = float(np.rad2deg(np.sum(np.abs(ph) * w) / max(np.sum(w), 1e-12)))
    return {"valid": True, "mean_phase_error_deg": round(err, 3)}


# ----------------------------------------------------------------------------
# generadores de señales de prueba (semilla fija)
# ----------------------------------------------------------------------------

def _rng():
    return np.random.default_rng(SEED)


def gen_sine(sr, dur, f, amp=0.5):
    t = np.arange(int(sr * dur)) / sr
    ph = _rng().uniform(0, 2 * math.pi)
    return (amp * np.sin(2 * math.pi * f * t + ph)).astype(np.float64)


def gen_smpte_imd(sr, dur=1.0):
    t = np.arange(int(sr * dur)) / sr
    s = 0.8 * np.sin(2 * math.pi * 60.0 * t) + 0.2 * np.sin(2 * math.pi * 7000.0 * t + 0.5)
    return (s * 0.9 / float(np.max(np.abs(s)))).astype(np.float64)


def gen_multitone(sr, dur=1.0, amp_db=-6.0):
    t = np.arange(int(sr * dur)) / sr
    r = _rng()
    s = np.zeros_like(t)
    amp = 10 ** (amp_db / 20.0)
    for f in [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]:
        s += amp * np.sin(2 * math.pi * f * t + r.uniform(0, 2 * math.pi))
    return (s * 0.9 / float(np.max(np.abs(s)))).astype(np.float64)


def gen_chirp(sr, dur=1.0, f0=20.0, f1=20000.0, amp=0.5):
    t = np.arange(int(sr * dur)) / sr
    k = (f1 / f0) ** (1.0 / dur)
    ph = 2 * math.pi * f0 * (k ** t - 1.0) / math.log(k)
    return (amp * np.sin(ph)).astype(np.float64)


def gen_pink(sr, dur=1.0, amp=0.3):
    n = int(sr * dur)
    r = _rng()
    white = r.standard_normal(n)
    X = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    f = np.maximum(freqs, 1.0)
    X *= 1.0 / np.sqrt(f)
    pink = np.fft.irfft(X, n)
    return (pink * amp / float(np.max(np.abs(pink)))).astype(np.float64)


def gen_step(sr, dur=0.05, amp=0.5, step_at=0.01):
    n = int(sr * dur)
    s = np.zeros(n)
    s[int(step_at * sr):] = amp
    return s.astype(np.float64)


# ----------------------------------------------------------------------------
# laboratorio
# ----------------------------------------------------------------------------

TOLERANCES = {
    "thd_n_db": -60.0,             # peor THD+N permitido, siempre evaluado (ver certify())
    "snr_db": 60.0,                # peor SNR permitido, siempre evaluado
    "imd_db": -60.0,
    "flatness_pp_db": 6.0,        # planitud 31.5 Hz–16 kHz (referencia de laboratorio)
    "overshoot_pct": 5.0,
    "latency_ms_max": 10.0,
    "bit_error_max": 1e-9,
}


def certify_self(sr=48000):
    """Auto-certificación: pipeline identidad. Debe dar bit-exact y PASS limpio."""
    tests = {}
    for name, fn, params in [
        ("sine_997", thd_n_db, {"f0": 997.0}),
        ("sine_60", thd_n_db, {"f0": 60.0}),
        ("sine_8k", thd_n_db, {"f0": 8000.0}),
    ]:
        x = gen_sine(sr, 1.0, params["f0"], 0.5)
        tests["thd_n_" + name] = thd_n_db(x, sr, params["f0"])
        tests["snr_" + name] = snr_db(x, sr, params["f0"])
    x = gen_smpte_imd(sr)
    tests["imd_db"] = imd_smpte_db(x, sr)
    m = gen_multitone(sr)
    prof = iso_octave_profile(m, sr)
    tests["flatness_pp_db"] = prof["flatness_pp_db"]
    tests["spectral"] = prof
    pink = gen_pink(sr)
    tests["crest_pink"] = crest(pink)
    st = gen_step(sr)
    tr = transient_metrics(st, sr)
    tests["transient"] = tr
    l = gen_sine(sr, 1.0, 1000.0, 0.5) + gen_pink(sr, 1.0, 0.05)
    r = l + 0.0 * l
    coh = stereo_coherence(l, r)
    ph = stereo_phase_error(l, r, sr)
    tests["stereo"] = {**coh, **ph}
    # identidad: señal de salida == señal de entrada
    ref = gen_multitone(sr)
    err = np.max(np.abs(ref - ref))
    bad, clip, pk = validity(ref)
    tests["bit_exact"] = bool(err < 1e-9)
    tests["max_sample_error"] = err
    tests["invalid_samples"] = bad
    tests["clipping_events"] = clip
    tests["peak"] = pk
    return tests


def analyze_capture(left_in, right_in, left_out, right_out, sr):
    """Mide un pipeline real desde capturas WAV (entrada → salida)."""
    lin = np.asarray(left_in, dtype=np.float64)
    lout = np.asarray(left_out, dtype=np.float64)
    rin = np.asarray(right_in, dtype=np.float64) if right_in is not None else lin
    rout = np.asarray(right_out, dtype=np.float64) if right_out is not None else lout
    n = min(len(lin), len(lout))
    ref, out = lin[:n], lout[:n]
    # latencia por cross-correlación FFT
    corr = np.fft.irfft(np.fft.rfft(out, 2 * n) * np.conj(np.fft.rfft(ref, 2 * n)), 2 * n)
    lag = int(np.argmax(np.abs(corr[:n])))
    latency_ms = round(lag * 1000.0 / sr, 3)
    if lag > 0:
        out_aligned = out[lag:]
        ref_aligned = ref[:len(out_aligned)]
    else:
        out_aligned, ref_aligned = out, ref
    m = min(len(ref_aligned), len(out_aligned))
    e = out_aligned[:m] - ref_aligned[:m]
    max_err = float(np.max(np.abs(e))) if m else float("inf")
    bit_exact = bool(max_err < 1e-9)
    metrics = {
        "latency_ms": latency_ms,
        "max_sample_error": max_err,
        "bit_exact": bit_exact,
        "error_rms": round(rms(e), 9) if m else None,
    }
    # sobre el tono mezclado salida (frecuencias conocidas si hay chirp/multitone marcado)
    for name, f0 in [("thd_n_997", 997.0), ("thd_n_60", 60.0), ("snr_997", 997.0)]:
        if len(out_aligned) > 1024:
            metrics[name] = (thd_n_db(out_aligned, sr, f0) if name.startswith("thd")
                             else snr_db(out_aligned, sr, f0))
    if len(out_aligned) > 1024:
        metrics["imd_db"] = imd_smpte_db(out_aligned, sr)
        metrics["spectral"] = iso_octave_profile(out_aligned, sr)
        metrics["crest"] = crest(out_aligned)
        bad, clip, pk = validity(out_aligned)
        metrics.update({"invalid_samples": bad, "clipping_events": clip, "peak": pk})
        metrics["stereo"] = stereo_coherence(lout[:n], rout[:n])
    return metrics


def load_wav_ch(path):
    import wave
    with wave.open(path, "rb") as w:
        ch = w.getnchannels()
        sr = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    if ch == 2:
        return data[0::2], data[1::2], sr
    return data, data, sr


def run(mode, sr, wav_in=None, wav_out=None):
    started = time.time()
    if mode == "self":
        metrics = certify_self(sr)
        results = {
            "mode": "self (auto-test de las herramientas de medición — "
                     "NO ejecuta el DSP nativo de IVANNA; ver docs/FLANCO_IAEL.md)",
            "real_pipeline_tested": False,
            "metrics": metrics,
        }
    else:
        if not (wav_in and wav_out):
            raise SystemExit("--mode wav requiere --in y --out")
        li, ri, sri = load_wav_ch(wav_in)
        lo, ro, sro = load_wav_ch(wav_out)
        if sri != sro:
            print("WARN: rates difieren (%d vs %d) — usando %d" % (sri, sro, sr), file=sys.stderr)
        metrics = analyze_capture(li, ri, lo, ro, sri)
        results = {"mode": "wav (captura de archivos)", "input": wav_in, "output": wav_out,
                   "real_pipeline_tested": True, "metrics": metrics}
        # FIX (autocomparación disfrazada de captura real): si wav_in y
        # wav_out son bytes idénticos, esto NO valida que IVANNA procesó
        # nada — es la misma trampa tautológica de --mode self pero con
        # ficheros. Visto en telemetry/iael_v4/wav_identity_latest.json:
        # ref_in.wav y ref_out_passthrough.wav son el mismo archivo
        # (sha256 idéntico) — útil como auto-test del propio --mode wav
        # (¿detecta bit-exact y latencia 0 correctamente?), inútil como
        # certificación de audio real. Se marca explícito, nunca se oculta.
        import hashlib
        def _sha256(path):
            h = hashlib.sha256()
            with open(path, "rb") as f:
                h.update(f.read())
            return h.hexdigest()
        if _sha256(wav_in) == _sha256(wav_out):
            results["real_pipeline_tested"] = False
            results["warning"] = (
                "wav_in y wav_out son el MISMO archivo (sha256 idéntico) — "
                "esta corrida es un auto-test del propio --mode wav, no una "
                "captura real de IVANNA procesando audio."
            )
    return results, max(time.time() - started, 1e-6)


def certify(results, tolerances):
    """
    Evalúa las métricas medidas contra tolerancias. SIEMPRE evalúa los
    valores reales — bit_exact ya NO es un atajo que salta este chequeo.

    FIX (certificación decorativa vía atajo bit_exact): la versión anterior,
    si bit_exact era True, marcaba "PASS" sin mirar thd_n/snr/imd/planitud
    en absoluto. En --mode self, bit_exact viene de comparar `ref - ref`
    (una resta de un array consigo mismo: SIEMPRE 0, tautológico) — así que
    TODA corrida self pasaba sin que ninguna métrica real se evaluara jamás.
    Peor aún: el atajo buscaba la clave "thd_n_sine_997" (solo existe en
    certify_self()); en --mode wav, analyze_capture() produce "thd_n_997"
    (sin "_sine") — la clave no existía, y `checks.append(("thd_n", None,
    "PASS", ...))` marcaba PASS sobre un valor None. Reproducido y
    verificado en vivo: correr --mode wav con telemetry/iael_v4/samples/
    (ref_in.wav == ref_out_passthrough.wav, mismo archivo) daba
    `[PASS] thd_n = None`. Un bit-exact real (p.ej. verificar bypass) sigue
    pasando SIN atajo: su THD+N/SNR reales, medidos contra su propia señal
    ya-limpia, caen naturalmente dentro de tolerancia.
    """
    m = results["metrics"]
    failures = []
    checks = []

    def metric(*names):
        """Primera clave presente — unifica el nombrado entre certify_self()
        ('thd_n_sine_997', un valor por tono sintético) y analyze_capture()
        ('thd_n_997', un único cálculo sobre la captura)."""
        for name in names:
            v = m.get(name)
            if v is not None:
                return v
        return None

    spectral = m.get("spectral")
    flatness = spectral.get("flatness_pp_db") if isinstance(spectral, dict) else m.get("flatness_pp_db")

    for key, value, lim, higher_is_better in [
        ("thd_n", metric("thd_n_sine_997", "thd_n_997"), tolerances["thd_n_db"], False),
        ("snr", metric("snr_sine_997", "snr_997"), tolerances["snr_db"], True),
        ("imd_db", m.get("imd_db"), tolerances["imd_db"], False),
        ("flatness_pp_db", flatness, tolerances["flatness_pp_db"], False),
    ]:
        if value is None:
            continue
        ok = bool((value >= lim) if higher_is_better else (value <= lim))
        checks.append((key, value, "PASS" if ok else "FAIL", "límite %s" % lim))
        if not ok:
            failures.append(key)

    # Informativo únicamente — nunca vuelve a decidir el veredicto por sí
    # solo. Útil para saber si el bypass fue realmente exacto, sin que eso
    # sustituya mirar las métricas reales de arriba.
    if m.get("bit_exact") is not None:
        checks.append(("bit_exact", bool(m["bit_exact"]), "INFO",
                       "informativo — no sustituye los límites de arriba"))

    # FIX (checks omitidos en el caso limpio): antes, invalid_samples==0 o
    # clipping_events==0 son "falsy" en Python -> el `if` ni siquiera
    # añadía un PASS explícito. El reporte solo mostraba estos checks
    # cuando había algo que reprobar, ocultando que sí se verificaron.
    if "invalid_samples" in m:
        bad = bool(m["invalid_samples"])
        checks.append(("validez", m["invalid_samples"], "FAIL" if bad else "PASS",
                       "0 esperado (NaN/Inf)"))
        if bad:
            failures.append("validez")
    if "clipping_events" in m:
        bad = m["clipping_events"] != 0
        checks.append(("clipping", m["clipping_events"], "FAIL" if bad else "PASS",
                       "0 esperado"))
        if bad:
            failures.append("clipping")

    certification = "FAIL" if failures else "PASS"
    return certification, failures, checks


def gen_test_wavs(sr=48000, dur=1.0, outdir="telemetry/iael_v4/samples"):
    """Genera WAV estereo de referencia (multitone 60..10kHz + 997Hz) para
    captura en dispositivo (Ruta A/B): ref_in.wav (entrada) y
    ref_out_passthrough.wav (PLACEHOLDER — copia idéntica de ref_in.wav
    hasta que alguien lo sobrescriba con una grabación real capturada
    tras pasar ref_in.wav por el DSP de IVANNA en un dispositivo). Sirve
    de partida para validar bit-exact + latencia 0 del propio --mode wav;
    NO es una certificación de audio real mientras siga siendo la copia —
    run() detecta este caso (sha256 idéntico) y lo marca explícitamente."""
    import wave
    os.makedirs(outdir, exist_ok=True)
    t = np.arange(int(sr * dur)) / sr
    r = _rng()
    mix = np.zeros_like(t)
    for f in (60, 250, 1000, 4000, 10000):
        mix += 0.2 * np.sin(2 * math.pi * f * t + r.uniform(0, 2 * math.pi))
    mix += 0.1 * np.sin(2 * math.pi * 997 * t)
    mix = mix / float(np.max(np.abs(mix))) * 0.8
    stereo = np.stack([mix, mix], axis=1)
    pcm = (stereo * 32767).astype(np.int16)
    for name, arr in (("ref_in.wav", pcm), ("ref_out_passthrough.wav", pcm)):
        with wave.open(os.path.join(outdir, name), "wb") as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
            w.writeframes(arr.tobytes())
    print("WAV de referencia generados en", outdir)

def main():
    ap = argparse.ArgumentParser(description="IAEL v4 — Laboratorio de Certificación de Audio")
    ap.add_argument("--mode", choices=["self", "wav"], default="self")
    ap.add_argument("--sr", type=int, default=48000)
    ap.add_argument("--in", dest="wav_in")
    ap.add_argument("--out", dest="wav_out")
    ap.add_argument("--gen-samples", action="store_true", help="genera WAV estéreo de referencia (Ruta A/B)")
    ap.add_argument("--gen-sr", type=int, default=48000)
    ap.add_argument("--out-json", default=None)
    ap.add_argument("--report-md", default=None)
    args = ap.parse_args()

    if args.gen_samples:
        gen_test_wavs(args.gen_sr)
        return 0

    results, elapsed = run(args.mode, args.sr, args.wav_in, args.wav_out)
    certification, failures, checks = certify(results, TOLERANCES)
    report = {
        "system": SYSTEM,
        "version": __version__,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()),
        "seed": SEED,
        "sample_rate": args.sr,
        "mode": results["mode"],
        "real_pipeline_tested": results.get("real_pipeline_tested"),
        "elapsed_seconds": round(elapsed, 3),
        "certification": certification,
        "failures": failures,
        "checks": checks,
        "metrics": results["metrics"],
        "tolerances": TOLERANCES,
    }
    if "warning" in results:
        report["warning"] = results["warning"]
    out_path = args.out_json or os.path.join("telemetry", "iael_v4", "latest.json")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=_json_default)
    if args.report_md:
        os.makedirs(os.path.dirname(args.report_md), exist_ok=True)
        with open(args.report_md, "w", encoding="utf-8") as f:
            f.write(markdown_report(report))
    print("IAEL v4 ->", certification, "| mode:", args.mode, "| json:", out_path)
    for c in checks:
        print("  [%s] %s = %s" % (c[2], c[0], c[1]))
    return 0 if certification == "PASS" else 1


def markdown_report(r):
    lines = ["# IAEL v4 — Reporte de Certificación", "",
             "| Campo | Valor |", "|---|---|",
             "| Sistema | %s |" % r["system"],
             "| Versión | %s |" % r["version"],
             "| Timestamp | %s |" % r["timestamp"],
             "| Modo | %s |" % r["mode"],
             "| **¿Probó el DSP real de IVANNA?** | %s |" %
                 ("**SÍ**" if r.get("real_pipeline_tested") else "**NO**"),
             "| Seed | %d |" % r["seed"],
             "| Certificación | **%s** |" % r["certification"], ""]
    if r.get("warning"):
        lines += ["> ⚠️ **%s**" % r["warning"], ""]
    lines += ["## Checks", "", "| Métrica | Valor | Veredicto | Referencia |", "|---|---|---|---|"]
    for c in r["checks"]:
        lines.append("| %s | %s | %s | %s |" % (c[0], c[1], c[2], c[3]))
    lines += ["", "## Métricas", "", "```json", json.dumps(r["metrics"], indent=2, ensure_ascii=False, default=_json_default), "```"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.exit(main())
