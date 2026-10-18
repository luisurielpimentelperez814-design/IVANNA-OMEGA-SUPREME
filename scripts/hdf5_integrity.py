#!/usr/bin/env python3
"""
Integridad estructural de archivos HDF5/SOFA SIN dependencias (ni h5py): firma, versión de superbloque,
dirección de fin de archivo (EOF) == tamaño real y checksum Jenkins lookup3 del superbloque v2/v3.

Por qué existe: 216 .sofa de app/src/main/assets/sofa/ se corrompieron (cada byte >= 0x80 pasó por una conversión
de texto UTF-8 y se sustituyó por U+FFFD) y ninguna puerta lo vio, porque solo se comparaba el árbol
assets/ivanna_omega con el del módulo. La firma por sí sola NO basta (un parche de 1 byte la restauró en su día
dejando todo el contenido roto): el EOF del superbloque y su checksum sí detectan truncado y datos alterados.

NO busque la secuencia EF BF BD como señal de corrupción: aparece por azar en ficheros binarios sanos grandes
(8 .sofa íntegros la contienen). Lo que decide es la estructura.
"""
import struct
import sys
from pathlib import Path

_M = 0xFFFFFFFF
SIGNATURE = b"\x89HDF\r\n\x1a\n"


def _rot(x: int, k: int) -> int:
    return ((x << k) | (x >> (32 - k))) & _M


def lookup3(data: bytes, initval: int = 0) -> int:
    """Jenkins lookup3 (hashlittle), el checksum que usa HDF5 en superbloque v2/v3 y objetos v2."""
    n = len(data)
    a = b = c = (0xDEADBEEF + n + initval) & _M
    i = 0
    while n > 12:
        a = (a + struct.unpack_from("<I", data, i)[0]) & _M
        b = (b + struct.unpack_from("<I", data, i + 4)[0]) & _M
        c = (c + struct.unpack_from("<I", data, i + 8)[0]) & _M
        a = (a - c) & _M; a ^= _rot(c, 4);  c = (c + b) & _M
        b = (b - a) & _M; b ^= _rot(a, 6);  a = (a + c) & _M
        c = (c - b) & _M; c ^= _rot(b, 8);  b = (b + a) & _M
        a = (a - c) & _M; a ^= _rot(c, 16); c = (c + b) & _M
        b = (b - a) & _M; b ^= _rot(a, 19); a = (a + c) & _M
        c = (c - b) & _M; c ^= _rot(b, 4);  b = (b + a) & _M
        n -= 12
        i += 12
    if n == 0:
        return c
    tail = data[i:] + b"\0" * 12
    words = []
    for w in range(3):
        k = max(0, min(4, n - 4 * w))
        v = struct.unpack_from("<I", tail, 4 * w)[0]
        words.append(v & ((1 << (8 * k)) - 1) if k < 4 else v)
    a = (a + words[0]) & _M
    b = (b + words[1]) & _M
    c = (c + words[2]) & _M
    c ^= b; c = (c - _rot(b, 14)) & _M
    a ^= c; a = (a - _rot(c, 11)) & _M
    b ^= a; b = (b - _rot(a, 25)) & _M
    c ^= b; c = (c - _rot(b, 16)) & _M
    a ^= c; a = (a - _rot(c, 4)) & _M
    b ^= a; b = (b - _rot(a, 14)) & _M
    c ^= b; c = (c - _rot(b, 24)) & _M
    return c


def check_bytes(d: bytes) -> str:
    """'ok' o un motivo corto del fallo."""
    if len(d) < 48 or d[:8] != SIGNATURE:
        return "firma HDF5 ausente o alterada"
    v = d[8]
    if v in (2, 3):
        eof = struct.unpack_from("<Q", d, 28)[0]
        if eof != len(d):
            return f"EOF del superbloque {eof} != tamaño real {len(d)} (truncado/alterado)"
        if lookup3(d[:44]) != struct.unpack_from("<I", d, 44)[0]:
            return "checksum del superbloque inválido"
        return "ok"
    if v in (0, 1):
        # superbloque v0/v1 (sin checksum): base, free-space, EOF
        off = 24 if v == 0 else 28
        eof = struct.unpack_from("<Q", d, off + 16)[0]
        return "ok" if eof == len(d) else f"EOF del superbloque {eof} != tamaño real {len(d)}"
    return f"versión de superbloque {v} no soportada"


def check_file(path) -> str:
    return check_bytes(Path(path).read_bytes())


if __name__ == "__main__":
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    bad = 0
    for p in sorted(root.rglob("*.sofa")):
        r = check_file(p)
        if r != "ok":
            bad += 1
            print(f"[FAIL] {p}: {r}")
    print(f"{'[FAIL]' if bad else '[PASS]'} {bad} .sofa con estructura dañada")
    sys.exit(1 if bad else 0)
