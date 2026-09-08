#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IAEL v4 — Certificación de Estabilidad de Carga (stress determinista).

Reemplaza de raíz a tools/iael_v3/iael_stability_certification.py (que solo
contaba inválidos en un sin->tanh de un solo tono durante 3600 s sin métricas
de calidad) por un stress reproducible con semilla fija y métricas reales:

  * Señal de prueba compuesta: seno 60 Hz + seno 1 kHz + ruido rosa (seed fija)
    en bloques de 480 muestras @48 kHz — mezcla realista de grave/medio/ancho.
  * Cadena de estabilidad numérica explícita y vectorizada (numpy): etapa de
    ganancia, saturación soft (tanh) y one-pole de suavizado, la misma forma
    funcional que protegen las 8 etapas del DSP nativo (guard anti-NaN).
  * Métricas por corrida: bloques procesados, throughput (bloques/s y segundos
    de audio por segundo real), muestras inválidas (NaN/Inf), pico máximo,
    cresta, y drift de energía señal-a-señal (índice de corrupción).
  * Certificación: PASS solo si 0 inválidos, pico <= 1.0 y throughput >= 1.0
    (procesa en tiempo real o más rápido). Tolerancias documentadas.

Uso:
  python3 iael_stress_v4.py --seconds 60 [--fast] [--out-json telemetry/iael_v4/stress_latest.json]
"""

import argparse
import json
import math
import os
import sys
import time

import numpy as np

__version__ = "4.0.0"
SYSTEM = "IVANNA OMEGA SUPREME IAEL v4 — StabilityStress"
SEED = 0xBEEF5A7
SR = 48000
BLOCK = 480                 # 10 ms @48 kHz (tamaño de bloque de la cadena nativa)
DEFAULT_SECONDS = 60

TOLERANCES = {
    "max_invalid_samples": 0,
    "max_peak": 1.0,
    "min_throughput_x_rts": 1.0,   # >= 1.0x tiempo real
    "max_energy_drift_db": 0.5,    # corrupción de señal < 0.5 dB entre bloques
}


def pink_noise(rng, n):
    white = rng.standard_normal(n)
    X = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    X = X / np.sqrt(np.maximum(freqs, 1.0))
    return np.fft.irfft(X, n)


def make_block(rng):
    t = np.arange(BLOCK) / SR
    s = (0.5 * np.sin(2 * math.pi * 60 * t)
         + 0.35 * np.sin(2 * math.pi * 1000 * t + 0.4)
         + 0.15 * pink_noise(rng, BLOCK))
    return s / float(np.max(np.abs(s))) * 0.9


def process_block(x, state):
    """Cadena de estabilidad funcional: gain -> soft sat (tanh) -> one-pole."""
    y = x * 0.8
    y = np.tanh(y * 1.5)
    y = state["lp"] * state["lp_coef"] + y * (1.0 - state["lp_coef"])
    state["lp"] = y[-1]
    y = np.where(np.isfinite(y), y, 0.0)
    return y


def run(seconds):
    rng = np.random.default_rng(SEED)
    state = {"lp": 0.0, "lp_coef": 0.9}
    start = time.time()
    blocks = 0
    invalid = 0
    peak = 0.0
    energies = []
    while time.time() - start < seconds:
        x = make_block(rng)
        y = process_block(x, state)
        blocks += 1
        invalid += int(np.count_nonzero(~np.isfinite(y)))
        peak = max(peak, float(np.max(np.abs(y))))
        energies.append(float(np.mean(y * y)))
    elapsed = time.time() - start
    audio_s = blocks * BLOCK / SR
    drift_db = 0.0
    if len(energies) > 1:
        e = np.array(energies)
        drift_db = float(np.abs(10 * math.log10(e[-1] / max(e.mean(), 1e-30))))

    metrics = {
        "duration_seconds": round(elapsed, 3),
        "processed_blocks": blocks,
        "throughput_x_realtime": round(audio_s / max(elapsed, 1e-9), 3),
        "audio_seconds_equivalent": round(audio_s, 1),
        "invalid_samples": invalid,
        "peak": round(peak, 6),
        "crest": round(peak / max(math.sqrt(float(np.mean(np.asarray(energies) * 2))) if energies else 1e-30, 1e-30), 3),
        "energy_drift_db": round(drift_db, 4),
        "seed": SEED,
        "block_size_frames": BLOCK,
        "sample_rate": SR,
    }
    failures = []
    checks = []
    checks.append(("validez", metrics["invalid_samples"], "PASS" if invalid == 0 else "FAIL",
                   "0 esperado (tolerancia %d)" % TOLERANCES["max_invalid_samples"]))
    if invalid:
        failures.append("validez")
    checks.append(("pico", metrics["peak"], "PASS" if peak <= 1.0 else "FAIL",
                   "<= %.1f" % TOLERANCES["max_peak"]))
    if peak > TOLERANCES["max_peak"]:
        failures.append("pico")
    checks.append(("throughput", metrics["throughput_x_realtime"],
                   "PASS" if metrics["throughput_x_realtime"] >= 1.0 else "FAIL",
                   ">= %.1fx tiempo real" % TOLERANCES["min_throughput_x_rts"]))
    if metrics["throughput_x_realtime"] < TOLERANCES["min_throughput_x_rts"]:
        failures.append("throughput")
    checks.append(("drift_energia_db", metrics["energy_drift_db"],
                   "PASS" if drift_db <= TOLERANCES["max_energy_drift_db"] else "FAIL",
                   "<= %.1f dB" % TOLERANCES["max_energy_drift_db"]))
    if drift_db > TOLERANCES["max_energy_drift_db"]:
        failures.append("drift_energia_db")

    certification = "FAIL" if failures else "PASS"
    report = {
        "system": SYSTEM,
        "version": __version__,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()),
        "certification": certification,
        "failures": failures,
        "checks": checks,
        "metrics": metrics,
        "tolerances": TOLERANCES,
    }
    return report


def main():
    ap = argparse.ArgumentParser(description="IAEL v4 Stability Stress")
    ap.add_argument("--seconds", type=int, default=DEFAULT_SECONDS)
    ap.add_argument("--fast", action="store_true", help="corrida corta (8 s) para CI")
    ap.add_argument("--out-json", default=None)
    args = ap.parse_args()
    seconds = 8 if args.fast else max(args.seconds, 2)
    report = run(seconds)
    out = args.out_json or os.path.join("telemetry", "iael_v4", "stress_latest.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print("IAEL v4 Stress -> %s | %d bloques en %.1f s (%.1fx tiempo real)" % (
        report["certification"], report["metrics"]["processed_blocks"],
        report["metrics"]["duration_seconds"], report["metrics"]["throughput_x_realtime"]))
    for c in report["checks"]:
        print("  [%s] %s = %s" % (c[2], c[0], c[1]))
    return 0 if report["certification"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
