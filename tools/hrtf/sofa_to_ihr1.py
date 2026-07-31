#!/usr/bin/env python3
"""Convierte un HRTF en formato SOFA (CIPIC/KEMAR/etc.) a IHR1.
Uso: python3 sofa_to_ihr1.py cipic.sofa -o hrtf_dataset.ihr1
Dependencias: pip install sofar numpy   (o netCDF4 como fallback)"""
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
        sys.exit("Instala sofar (pip install sofar) o netCDF4 + numpy")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sofa", help="archivo .sofa de entrada")
    ap.add_argument("-o", "--out", default="hrtf_dataset.ihr1")
    ap.add_argument("--ir-len", type=int, default=512)
    ap.add_argument("--max-elev", type=float, default=5.0,
                    help="solo direcciones con |elevacion| <= este valor (grados)")
    ap.add_argument("--flip-azimuth", action="store_true",
                    help="invierte el signo del azimut (segun convencion del dataset)")
    args = ap.parse_args()

    pos, ir, sr = load_sofa(args.sofa)
    az_all, el_all = pos[:, 0], pos[:, 1]
    idx = np.where(np.abs(el_all) <= args.max_elev)[0]
    if len(idx) == 0:
        sys.exit("No hay direcciones con elevacion cercana a 0. Sube --max-elev.")

    rows = []
    for i in idx:
        az = float(az_all[i])
        if args.flip_azimuth:
            az = -az
        # SOFA: azimut positivo = izquierda; convolver: positivo = derecha
        az = -az
        while az < -180: az += 360
        while az > 180:  az -= 360
        hrL = np.zeros(args.ir_len, dtype=np.float32)
        hrR = np.zeros(args.ir_len, dtype=np.float32)
        n = min(args.ir_len, ir.shape[2])
        hrL[:n] = ir[i, 0, :n]
        hrR[:n] = ir[i, 1, :n]
        rows.append((az, hrL, hrR))

    rows.sort(key=lambda r: r[0])
    with open(args.out, "wb") as f:
        f.write(b"IHR1")
        f.write(struct.pack("<i", len(rows)))
        f.write(struct.pack("<i", args.ir_len))
        f.write(struct.pack("<i", int(sr)))
        for az, hrL, hrR in rows:
            f.write(struct.pack("<f", az))
            f.write(hrL.astype("<f4").tobytes())
            f.write(hrR.astype("<f4").tobytes())
    print(f"✅ {args.out}: {len(rows)} direcciones (SR={int(sr)}, IR={args.ir_len})")

if __name__ == "__main__":
    main()
