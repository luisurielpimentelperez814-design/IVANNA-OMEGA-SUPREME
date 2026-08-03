#!/usr/bin/env python3

import h5py
import numpy as np
import struct
from pathlib import Path


INPUT = Path(
"app/src/main/assets/saf/sofa_elite/MIT_KEMAR_normal_pinna.sofa"
)

OUTPUT = Path(
"app/src/main/assets/saf/processed/hrtf_database.bin"
)


with h5py.File(INPUT,"r") as f:

    ir = np.array(f["Data.IR"], dtype=np.float32)

    sr = int(np.array(
        f["Data.SamplingRate"]
    )[0])

    positions = np.array(
        f["SourcePosition"]
    )

    azimuth = positions[:,0].astype(np.float32)


# normalización segura
peak=np.max(np.abs(ir))

if peak > 1.0:
    ir /= peak


# SOFA:
# (direcciones, canales, taps)

L = ir[:,0,:]
R = ir[:,1,:]

dirs = L.shape[0]
taps = L.shape[1]


OUTPUT.parent.mkdir(
    parents=True,
    exist_ok=True
)


with open(OUTPUT,"wb") as out:

    out.write(b"IVHRTF01")

    out.write(struct.pack(
        "<fIII",
        float(sr),
        dirs,
        2,
        taps
    ))

    for i in range(dirs):

        out.write(
            L[i].astype(np.float32).tobytes()
        )

        out.write(
            R[i].astype(np.float32).tobytes()
        )


print("Written:",OUTPUT)
print("Rate:",sr)
print("Dirs:",dirs)
print("Taps:",taps)
print("Bytes:",OUTPUT.stat().st_size)
