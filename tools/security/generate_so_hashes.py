#!/usr/bin/env python3
"""Genera el asset con los hashes SHA-256 de las .so (fase 2 anti-piratería).

Uso (invocado por la tarea Gradle `generateSoHashes` tras externalNativeBuild):
    python3 generate_so_hashes.py --so <ruta/libivanna_omega.so> [--so ...] --out <ruta/ivanna_so_hashes.txt>

Escribe una línea por .so: "<nombre> <sha256-hex>\\n". El asset va en
app/src/main/assets/ivanna_so_hashes.txt y se empaqueta en el APK.
IntegrityGate.kt (paso 4) compara el hash de la .so instalada con este
asset en runtime (detecta sustitución de la .so por una copia modificada).

FALLA (exit 1) si alguna .so no existe o no se puede leer: la tarea Gradle
falla el build antes que empaquetar un APK sin asset. Si el asset falta en
release, IntegrityGate bloquea (fail-closed); en debug se omite.

CONFIDENCIAL (interno). Ver docs/SECURITY_INTERNAL.md.
"""
import argparse
import hashlib
import os
import sys


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--so", action="append", required=True,
                    help="Ruta de una .so a hashear (repetible).")
    ap.add_argument("--out", required=True,
                    help="Ruta del asset a escribir (ivanna_so_hashes.txt).")
    args = ap.parse_args()

    lines = []
    for so_path in args.so:
        if not os.path.isfile(so_path):
            print(f"generate_so_hashes: ERROR: .so no encontrada: {so_path}",
                  file=sys.stderr)
            return 1
        try:
            digest = sha256_file(so_path)
        except OSError as e:
            print(f"generate_so_hashes: ERROR: no se pudo leer {so_path}: {e}",
                  file=sys.stderr)
            return 1
        name = os.path.basename(so_path)
        lines.append(f"{name} {digest}\n")
        print(f"generate_so_hashes: {name} {digest}")

    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.writelines(lines)
    print(f"generate_so_hashes: escrito {args.out} ({len(lines)} entrada(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
