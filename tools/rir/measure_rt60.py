#!/usr/bin/env python3
"""Mide el RT60 REAL de cada RIR (T20: integral de Schroeder hacia atras, ajuste -5..-25 dB,
extrapolado a -60 dB) y lo escribe/verifica como columna `RT60_T20_measured_s` en metadata.csv.

Contexto (medido en las 200 salas): la columna historica `RT60_s` NO es el tiempo de caida de los WAV;
el WAV mide 2.284 +/- 0.022 veces ese valor. El orden entre salas se conserva, por eso la seleccion
relativa sigue funcionando, pero el valor absoluto no es un RT60 fisico. Esta columna anade la medicion real
sin alterar `RT60_s` (los parsers C++/Kotlin leen >=12 columnas y toleran la 13a).

Uso:  measure_rt60.py --write   # (re)genera la columna
      measure_rt60.py --check   # exit 1 si la columna no coincide con una re-medicion (tolerancia 1 %)
      measure_rt60.py --selftest# valida el estimador con decaimientos sinteticos de RT60 conocido
"""
import argparse, csv, io, sys, wave
from pathlib import Path
import numpy as np

RIR_DIR = Path(__file__).resolve().parents[2] / "app/src/main/assets/ivanna_omega/rir"
COL = "RT60_T20_measured_s"

def rt60_t20(x: np.ndarray, fs: int):
    e = np.cumsum((x.astype(np.float64) ** 2)[::-1])[::-1]
    if e[0] <= 0: return None
    db = 10.0 * np.log10(np.maximum(e / e[0], 1e-14)); t = np.arange(len(x)) / fs
    i5, i25 = int(np.argmax(db <= -5.0)), int(np.argmax(db <= -25.0))
    if i25 <= i5 + 8: return None
    slope = np.polyfit(t[i5:i25], db[i5:i25], 1)[0]
    return None if slope >= 0 else float(-60.0 / slope)

def load_ch0(path: Path):
    with wave.open(str(path)) as w:
        assert w.getsampwidth() == 2, "se espera PCM16"
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, w.getnchannels())
        return x[:, 0].astype(np.float64) / 32768.0, w.getframerate()

def measure_all(rows):
    out = {}
    for r in rows:
        x, fs = load_ch0(RIR_DIR / r["filename"]); out[r["filename"]] = rt60_t20(x, fs)
    return out

def selftest() -> int:
    rng = np.random.default_rng(7); fs = 16000; worst = 0.0
    for T in (0.3, 0.6, 1.2, 2.0):
        t = np.arange(int(fs * T * 1.6)) / fs
        est = rt60_t20(rng.standard_normal(len(t)) * 10 ** (-3 * t / T), fs)
        worst = max(worst, abs(est - T) / T)
    print(f"selftest: peor error relativo {worst*100:.2f} %"); return 0 if worst < 0.05 else 1

def main() -> int:
    ap = argparse.ArgumentParser(); g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--write", action="store_true"); g.add_argument("--check", action="store_true"); g.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest: return selftest()
    meta = RIR_DIR / "metadata.csv"; raw = meta.read_bytes().decode("utf-8")
    rows = list(csv.DictReader(io.StringIO(raw))); m = measure_all(rows)
    if a.check:
        bad = [(r["filename"], r.get(COL), m[r["filename"]]) for r in rows
               if COL not in r or m[r["filename"]] is None or abs(float(r[COL]) - m[r["filename"]]) > 0.01 * m[r["filename"]]]
        print(f"check: {len(rows)-len(bad)}/{len(rows)} salas coinciden con la re-medicion (tol 1 %)")
        for b in bad[:5]: print("  MISMATCH", b)
        return 1 if bad else 0
    nl = "\r\n" if "\r\n" in raw else "\n"
    cols = [c for c in rows[0].keys() if c != COL] + [COL]
    buf = io.StringIO(); wr = csv.writer(buf, lineterminator=nl); wr.writerow(cols)
    for r in rows:
        if m[r["filename"]] is None: raise SystemExit(f"sin medicion valida: {r['filename']}")
        wr.writerow([r[c] for c in cols[:-1]] + [f"{m[r['filename']]:.6f}"])
    meta.write_bytes(buf.getvalue().encode("utf-8")); print(f"escrito {COL} en {len(rows)} salas"); return 0

if __name__ == "__main__": sys.exit(main())
