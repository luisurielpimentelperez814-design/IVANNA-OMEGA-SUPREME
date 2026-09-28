#!/usr/bin/env python3
"""Genera un dataset HRTF de prueba en formato IHR1 (sin dependencias).
Valida que el loader del efecto funciona. Para calidad real usa sofa_to_ihr1.py."""
import struct, math, sys

SR = 48000
IR_LEN = 512
OUT = sys.argv[1] if len(sys.argv) > 1 else "hrtf_dataset.ihr1"

def gen_hrir(az_deg):
    theta = math.radians(abs(az_deg))
    tau = (0.0875 / 343.0) * (theta + math.sin(theta))   # Woodworth ITD
    delay = min(int(round(tau * SR)), IR_LEN // 2)
    L = [0.0] * IR_LEN
    R = [0.0] * IR_LEN
    near, far = (R, L) if az_deg >= 0 else (L, R)
    near[0] = 1.0
    shadow = abs(az_deg) / 90.0
    fc = 14000.0 - shadow * 10500.0
    alpha = (1.0 / SR) / (1.0 / (2 * math.pi * fc) + 1.0 / SR)
    gain = 1.0 - 0.3 * shadow
    state = 0.0
    for n in range(IR_LEN):
        imp = 1.0 if n == 0 else 0.0
        state += alpha * (imp - state)
        idx = n + delay
        if idx < IR_LEN:
            far[idx] += state * gain
    return L, R

azimuths = list(range(-90, 91, 15))   # 13 direcciones, plano horizontal
with open(OUT, "wb") as f:
    f.write(b"IHR1")
    f.write(struct.pack("<i", len(azimuths)))
    f.write(struct.pack("<i", IR_LEN))
    f.write(struct.pack("<i", SR))
    for az in azimuths:
        L, R = gen_hrir(az)
        f.write(struct.pack("<f", float(az)))
        f.write(struct.pack("<%df" % IR_LEN, *L))
        f.write(struct.pack("<%df" % IR_LEN, *R))
print(f"✅ {OUT}: {len(azimuths)} direcciones, IR_LEN={IR_LEN}, SR={SR}")
