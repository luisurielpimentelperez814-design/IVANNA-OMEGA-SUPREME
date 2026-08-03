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


with h5py.File(INPUT, "r") as f:

    ir = np.array(f["Data.IR"])
    sr = np.array(f["Data.SamplingRate"])[0]

    print("SOFA Data.IR shape:", ir.shape)
    print("Sample rate:", sr)

    ir = ir.astype(np.float32)


# Normalización segura
peak = np.max(np.abs(ir))

if peak > 1.0:
    ir /= peak


OUTPUT.parent.mkdir(
    parents=True,
    exist_ok=True
)


with open(OUTPUT, "wb") as out:

    # magic
    out.write(b"IVHRTF01")

    # sample rate
    out.write(struct.pack(
        "<f",
        float(sr)
    ))

    # dimensiones
    out.write(struct.pack(
        "<3I",
        *ir.shape
    ))

    # datos FIR
    out.write(
        ir.tobytes()
    )


print("Written:", OUTPUT)
print("Bytes:", OUTPUT.stat().st_size)
