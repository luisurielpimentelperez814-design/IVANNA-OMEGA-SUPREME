#!/usr/bin/env python3
"""Convierte un HRTF en formato SOFA (CIPIC/KEMAR/etc.) a IHR1.
Uso: python3 sofa_to_ihr1.py cipic.sofa -o hrtf_dataset.ihr1
Dependencias: pip install sofar numpy   (o netCDF4 / h5py como fallback)

Notas de correccion (auditoria 2026-08-01), ambas con impacto audible:

1. RESAMPLEO OBLIGATORIO (--target-sr, por defecto 48000).
   El cargador C++ (spatial/synthetic_hrtf.hpp:54 loadDatasetFromFile) LEE
   el campo sampleRate de la cabecera pero NUNCA lo usa: en la linea 78
   llama a loadDataset(az, L, R, numDirs, irLen) sin pasarlo. Es decir, el
   motor reproduce los HRIR al sample rate con el que este corriendo, sea
   cual sea el del dataset. CIPIC esta medido a 44100 Hz y el efecto se
   inicializa a 48000 (omega_effect.cpp:122), asi que un volcado directo
   sonaria un 8.8% rapido: el ITD se encoge y los notches espectrales
   (las pistas de elevacion) se desplazan hacia arriba. Por eso el
   resampleo se hace AQUI, en la conversion, y no en tiempo real.

2. UNA MEDIDA POR AZIMUT (--dedupe-azimuth, activo por defecto).
   generateFromDataset() interpola entre los dos azimuts adyacentes de la
   lista ordenada. Con --max-elev 5 el CIPIC deja pasar varias medidas casi
   al mismo azimut pero de ELEVACIONES distintas (p.ej. 80.00, 80.05,
   80.19, 80.42, 80.75, 81.16). Interpolar entre ellas mezcla HRIR de
   elevaciones diferentes dentro de poco mas de un grado, lo que produce
   filtrado en peine e imagen inestable al girar la cabeza. Se conserva,
   por cada azimut, la medida con |elevacion| minima.
"""
import argparse, struct, sys
import numpy as np

def load_sofa(path):
    try:
        import sofar
        s = sofar.read_sofa(path)
        return (np.asarray(s.SourcePosition, dtype=float),
                np.asarray(s.Data_IR, dtype=float),
                float(np.asarray(s.Data_SamplingRate).flatten()[0]))
    except ImportError:
        pass
    try:
        import netCDF4
        ds = netCDF4.Dataset(path)
        return (np.asarray(ds.variables['SourcePosition'][:], dtype=float),
                np.asarray(ds.variables['Data.IR'][:], dtype=float),
                float(np.asarray(ds.variables['Data.SamplingRate'][:]).flatten()[0]))
    except ImportError:
        pass
    try:
        # Un .sofa es NetCDF4, que por dentro es HDF5: h5py lo abre sin
        # necesitar la pila netCDF completa.
        import h5py
        with h5py.File(path, 'r') as ds:
            return (np.asarray(ds['SourcePosition'][:], dtype=float),
                    np.asarray(ds['Data.IR'][:], dtype=float),
                    float(np.asarray(ds['Data.SamplingRate'][:]).flatten()[0]))
    except ImportError:
        sys.exit("Instala sofar, netCDF4 o h5py (+ numpy)")


def resample_irs(ir_block, src_sr, dst_sr):
    """Resamplea (n, taps) de src_sr a dst_sr conservando la fase.

    resample_poly usa un FIR polifasico de fase lineal: retrasa todos los
    coeficientes por igual, asi que la DIFERENCIA de tiempo entre oido
    izquierdo y derecho (el ITD, que es la pista de localizacion mas
    importante por debajo de 1.5 kHz) se conserva intacta.
    """
    if int(src_sr) == int(dst_sr):
        return ir_block
    from math import gcd
    g = gcd(int(round(dst_sr)), int(round(src_sr)))
    up, down = int(round(dst_sr)) // g, int(round(src_sr)) // g
    try:
        from scipy.signal import resample_poly
        return resample_poly(ir_block, up, down, axis=-1)
    except ImportError:
        # Fallback sin scipy: interpolacion lineal. Peor en la banda alta,
        # pero preserva el ITD y evita fallar la conversion por una
        # dependencia opcional.
        n_out = int(round(ir_block.shape[-1] * dst_sr / src_sr))
        x_old = np.arange(ir_block.shape[-1], dtype=float)
        x_new = np.linspace(0.0, ir_block.shape[-1] - 1.0, n_out)
        return np.stack([np.interp(x_new, x_old, row) for row in ir_block])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sofa", help="archivo .sofa de entrada")
    ap.add_argument("-o", "--out", default="hrtf_dataset.ihr1")
    ap.add_argument("--ir-len", type=int, default=512)
    ap.add_argument("--max-elev", type=float, default=5.0,
                    help="solo direcciones con |elevacion| <= este valor (grados)")
    ap.add_argument("--flip-azimuth", action="store_true",
                    help="invierte el signo del azimut (segun convencion del dataset)")
    ap.add_argument("--target-sr", type=int, default=48000,
                    help="sample rate de salida; los HRIR se resamplean a el "
                         "(el cargador C++ ignora el SR de la cabecera)")
    ap.add_argument("--no-dedupe-azimuth", action="store_true",
                    help="conserva todas las medidas aunque compartan azimut "
                         "(por defecto se deja solo la de |elevacion| minima)")
    ap.add_argument("--az-round", type=float, default=1.0,
                    help="grados a los que se redondea el azimut al deduplicar")
    args = ap.parse_args()

    pos, ir, sr = load_sofa(args.sofa)
    az_all, el_all = pos[:, 0], pos[:, 1]
    idx = np.where(np.abs(el_all) <= args.max_elev)[0]
    if len(idx) == 0:
        sys.exit("No hay direcciones con elevacion cercana a 0. Sube --max-elev.")

    # ── Deduplicado por azimut ───────────────────────────────────────────
    # Se queda la medida mas cercana al plano horizontal por cada azimut.
    if not args.no_dedupe_azimuth:
        best = {}
        for i in idx:
            key = round(float(az_all[i]) / args.az_round) * args.az_round
            key = round(key % 360.0, 4)
            if key not in best or abs(el_all[i]) < abs(el_all[best[key]]):
                best[key] = i
        idx = np.array(sorted(best.values()), dtype=int)

    # ── Resampleo al SR de destino ───────────────────────────────────────
    ir_sel = np.asarray(ir[idx], dtype=float)          # (n, 2, taps)
    n_sel, n_ears, n_taps = ir_sel.shape
    flat = ir_sel.reshape(n_sel * n_ears, n_taps)
    flat = resample_irs(flat, sr, args.target_sr)
    ir_sel = flat.reshape(n_sel, n_ears, flat.shape[-1])
    out_sr = int(args.target_sr)

    rows = []
    for k, i in enumerate(idx):
        az = float(az_all[i])
        if args.flip_azimuth:
            az = -az
        # SOFA: azimut positivo = izquierda; convolver: positivo = derecha
        az = -az
        while az < -180: az += 360
        while az > 180:  az -= 360
        hrL = np.zeros(args.ir_len, dtype=np.float32)
        hrR = np.zeros(args.ir_len, dtype=np.float32)
        n = min(args.ir_len, ir_sel.shape[2])
        hrL[:n] = ir_sel[k, 0, :n]
        hrR[:n] = ir_sel[k, 1, :n]
        rows.append((az, hrL, hrR))

    rows.sort(key=lambda r: r[0])

    # El cargador rechaza numDirs > 1024 o irLen > 8192
    # (synthetic_hrtf.hpp:67). Se avisa antes de escribir un fichero que el
    # motor descartaria en silencio, cayendo al HRTF sintetico.
    if len(rows) > 1024:
        sys.exit(f"{len(rows)} direcciones supera el limite de 1024 del cargador "
                 f"(synthetic_hrtf.hpp:67). Baja --max-elev o sube --az-round.")
    if args.ir_len > 8192:
        sys.exit("--ir-len supera el limite de 8192 del cargador.")

    with open(args.out, "wb") as f:
        f.write(b"IHR1")
        f.write(struct.pack("<i", len(rows)))
        f.write(struct.pack("<i", args.ir_len))
        f.write(struct.pack("<i", out_sr))
        for az, hrL, hrR in rows:
            f.write(struct.pack("<f", az))
            f.write(hrL.astype("<f4").tobytes())
            f.write(hrR.astype("<f4").tobytes())
    azs = [r[0] for r in rows]
    print(f"OK {args.out}: {len(rows)} direcciones "
          f"(SR {int(sr)} -> {out_sr}, IR={args.ir_len}, "
          f"az {azs[0]:.1f}..{azs[-1]:.1f})")

if __name__ == "__main__":
    main()
