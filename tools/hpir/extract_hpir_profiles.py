#!/usr/bin/env python3
"""
Extrae perfiles AutoEQ paramétricos REALES desde los HpIR SOFA medidos.

Metodología:
  1. Promedia las M mediciones y los 2 canales del HpIR → respuesta |H(f)|
  2. Suavizado 1/12 de octava (estándar para EQ de auriculares — el oído no
     resuelve Q alto; ecualizar rizado fino suena peor)
  3. Compensación = inversa de la respuesta medida, normalizada para que la
     media 200 Hz–8 kHz quede a 0 dB (preserva el nivel percibido)
  4. Detección de los 4 picos/valles más prominentes → bandas peaking
     (freq, gain dB, Q estimado del ancho a -3 dB), gain clamped a ±8 dB
  5. Low-shelf de graves si la región 20–120 Hz difiere del target
Salida: JSON con los perfiles para integrar en AutoEqManager.kt
"""
import h5py, numpy as np, json, os, sys

SOFA_DIR = '/home/user/IVANNA-OMEGA-SUPREME/magisk_module/system/etc/ivanna_omega/sofa'

# Solo HpIR con modelo de auricular identificable
MODELS = {
    'hpir_SennheiserHD650_nh831.sofa':      'Sennheiser HD650',
    'hpir_BeyerdynamicDT770PRO_nh831.sofa': 'Beyerdynamic DT770 Pro',
    'hpir_BeyerdynamicDT990PRO_nh830.sofa': 'Beyerdynamic DT990 Pro',
    'hpir_AKGK271MKII_nh719.sofa':          'AKG K271 MKII',
    'hpir_AKGK272HD_nh719.sofa':            'AKG K272 HD',
}

def smooth_12th_octave(freqs, db, width=1.0/12.0):
    """Suavizado por promedio móvil logarítmico de 1/12 de octava."""
    out = np.zeros_like(db)
    for i, f in enumerate(freqs):
        lo, hi = f * 2**(-width), f * 2**width
        m = (freqs >= lo) & (freqs <= hi)
        out[i] = db[m].mean() if m.any() else db[i]
    return out

def measure_profile(path, name):
    f = h5py.File(path, 'r')
    ir = f['Data.IR'][:]          # (M, R, N)
    sr = float(f['Data.SamplingRate'][:][0])
    f.close()

    # Promediar sobre mediciones y canales: |H| promedio de potencia
    N = 8192
    H = np.zeros(N//2 + 1)
    for m in range(ir.shape[0]):
        for r in range(ir.shape[1]):
            h = np.abs(np.fft.rfft(ir[m, r], N))**2
            H += h
    H /= (ir.shape[0] * ir.shape[1])

    freqs = np.fft.rfftfreq(N, 1/sr)
    db = 10*np.log10(H + 1e-12)

    band = (freqs >= 20) & (freqs <= 18000)
    fr, resp = freqs[band], db[band]
    resp = smooth_12th_octave(fr, resp)

    # Normalizar: media 200Hz–8kHz → 0 dB (preserva loudness)
    mid = (fr >= 200) & (fr <= 8000)
    resp -= resp[mid].mean()

    # Compensación = inversa, clamped a ±8 dB (más de eso suena artificial)
    comp = np.clip(-resp, -8.0, 8.0)

    # Bandas: picos y valles prominentes de la curva de compensación
    bands = []
    work = comp.copy()
    for _ in range(4):
        idx = int(np.argmax(np.abs(work)))
        g = work[idx]
        if abs(g) < 0.8:            # menos de 0.8 dB: inaudible, parar
            break
        fc = fr[idx]
        # Ancho a medios puntos (-3 dB del pico) → Q
        half = abs(g) / 2
        lo = idx
        while lo > 0 and abs(work[lo]) > half: lo -= 1
        hi = idx
        while hi < len(work)-1 and abs(work[hi]) > half: hi += 1
        bw_oct = np.log2(fr[hi]/max(fr[lo], 20.0))
        q = max(0.4, min(4.0, 1.0/max(bw_oct, 0.05)))
        bands.append({'freq': round(float(fc), 1),
                      'gainDb': round(float(g), 2),
                      'q': round(float(q), 2)})
        work[max(0,lo-50):min(len(work),hi+50)] = 0   # suprimir vecindario

    bands.sort(key=lambda b: b['freq'])

    # Low-shelf: nivel medio 20–120 Hz vs target
    low = (fr >= 20) & (fr <= 120)
    shelfGain = float(np.clip(comp[low].mean(), -8.0, 8.0))
    shelf = None
    if abs(shelfGain) >= 0.8:
        shelf = {'freq': 105.0, 'gainDb': round(shelfGain, 2), 'q': 0.71}

    return {'model': name, 'sourceFile': os.path.basename(path),
            'shelf': shelf, 'bands': bands}

profiles = []
for fn, name in MODELS.items():
    p = os.path.join(SOFA_DIR, fn)
    if not os.path.exists(p):
        print(f'  FALTA: {fn}', file=sys.stderr)
        continue
    prof = measure_profile(p, name)
    profiles.append(prof)
    print(f"OK {name}: {len(prof['bands'])} bandas + shelf={prof['shelf']}")

out = '/home/user/hpir_profiles.json'
with open(out, 'w') as f:
    json.dump(profiles, f, indent=2)
print(f"\nEscrito: {out}")
