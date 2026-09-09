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
import h5py, numpy as np, json, os, sys, argparse

# Por defecto: los HpIR del modulo Magisk, resueltos RELATIVOS a este
# script (antes: ruta absoluta /home/user/IVANNA-OMEGA-SUPREME/... — solo
# existia en una maquina concreta; en cualquier otra el script no encontraba
# ni un solo .sofa y escribia un JSON vacio sin error). Igual para la
# salida, que iba a /home/user/hpir_profiles.json aunque el README y el
# consumidor (AutoEqManager) esperan tools/hpir/hpir_profiles_measured.json.
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DEFAULT_SOFA_DIR = os.path.join(REPO_ROOT, 'magisk_module', 'system', 'etc',
                                'ivanna_omega', 'sofa')
DEFAULT_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           'hpir_profiles_measured.json')

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

    # Low-shelf se mide sobre la compensacion ORIGINAL (ver mas abajo: la
    # region de graves se excluye SOLO de la deteccion de bandas peaking).
    low = (fr >= 20) & (fr <= 120)
    shelfGain = float(np.clip(comp[low].mean(), -8.0, 8.0))
    shelf = None
    if abs(shelfGain) >= 0.8:
        shelf = {'freq': 105.0, 'gainDb': round(shelfGain, 2), 'q': 0.71}

    # ── Excluir la region del shelf de la deteccion de bandas ─────────────
    # Verificado 2026-09-09 ejecutando sobre los 5 HpIR del repo: TODOS
    # producian una banda identica a 23.4 Hz +8.0 dB (clamped) — artefacto
    # de los SOFA HpIR bajo ~50 Hz (la FFT acumula energia de borde en el
    # primer bin; ninguna medida de auricular es fiable ahi) que ADEMAS
    # solapa con el low-shelf de 105 Hz: doble compensacion de los mismos
    # graves (shelf +6..8 dB y encima un peaking +8 dB = graves hinchados
    # audibles). El JSON publicado (bc986a93) ya curaba esto a mano — cero
    # bandas bajo 120 Hz. Esa curaduria manual ahora es parte de la
    # herramienta: el peaking vive por encima del shelf, el shelf solo en
    # graves. Se trabaja sobre una COPIA: comp original se conserva para el
    # shelf (ya calculado) y para futura inspeccion.
    compPeaking = comp.copy()
    compPeaking[fr < 120.0] = 0.0

    # Bandas: picos y valles prominentes de la curva de compensación
    bands = []
    work = compPeaking
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

    return {'model': name, 'sourceFile': os.path.basename(path),
            'shelf': shelf, 'bands': bands}

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--sofa-dir', default=DEFAULT_SOFA_DIR,
                    help='directorio con los HpIR .sofa medidos')
    ap.add_argument('-o', '--out', default=DEFAULT_OUT,
                    help='JSON de salida (por defecto hpir_profiles_measured.json '
                         'junto a este script — el que consume AutoEqManager)')
    args = ap.parse_args()

    profiles = []
    missing = 0
    for fn, name in MODELS.items():
        p = os.path.join(args.sofa_dir, fn)
        if not os.path.exists(p):
            print(f'  FALTA: {fn}', file=sys.stderr)
            missing += 1
            continue
        prof = measure_profile(p, name)
        profiles.append(prof)
        print(f"OK {name}: {len(prof['bands'])} bandas + shelf={prof['shelf']}")

    # Escribir un JSON vacio como si fuera exito era el fallo silencioso
    # original: si falta TODA la entrada, es un error (ruta equivocada),
    # no un perfil vacio legitimo. Faltan parciales: avisar y continuar.
    if not profiles:
        print(f'ERROR: ningun HpIR encontrado en {args.sofa_dir} — '
              f'nada que escribir', file=sys.stderr)
        return 1
    with open(args.out, 'w') as f:
        json.dump(profiles, f, indent=2)
    print(f"\nEscrito: {args.out} ({len(profiles)} perfiles"
          + (f", {missing} faltantes" if missing else "") + ")")
    return 0


if __name__ == '__main__':
    sys.exit(main())
