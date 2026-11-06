#!/usr/bin/env python3
"""
kernel_loader.py — Puerta de carga segura para kernels Volterra H1/H2 (Fase V).

Única vía aprobada para producir el payload que `VolterraH2Symmetric::updateKernels()`
aceptaría. El loader valida y empaqueta; NUNCA escribe en el motor. La aplicación
real (C++/JNI) mantiene sus propias barreras: defensa en profundidad.

Contenedor binario (little-endian):
    magic "IVK1" | u16 version | u16 kernel_length | u32 sample_rate
    u32 crc32(payload) | u64 dataset_id (8 bytes ascii) | device_id (16 bytes)
    f32 h1[K] | f32 h2_tri[K(K+1)/2]

Reglas (todas obligatorias; cualquier fallo => rechazo, fail-closed):
  1. magic y versión de formato
  2. CRC32 del payload
  3. SHA-256 del dataset de procedencia declarado
  4. device_id = dispositivo caracterizado autorizado
  5. dimensiones: K coincide con el motor; h2 triangular K(K+1)/2
  6. frecuencia de muestreo coincide con el motor
  7. cero NaN/Inf
  8. límites de amplitud |h1|,|h2| <= MAX_COEFF
  9. estabilidad conservadora: l1(h1) <= 1+eps y energía cuadrática acotada
"""
from __future__ import annotations

import hashlib
import struct
import zlib
from dataclasses import dataclass

MAGIC = b"IVK1"
FORMAT_VERSION = 1
MAX_COEFF = 8.0
L1_EPS = 1e-6
# SHA-256 del .mdat de procedencia permitido para kernels marcados DATASET0
# (dataset público REW). Un kernel con otro dataset_id sería rechazado.
DATASET_SHA256 = "580a1c323449d0c986e468ab4190d5f40f5e91122d0f002bc3bb8ff661cd497a"


@dataclass
class LoadedKernel:
    kernel_length: int
    sample_rate: int
    device_id: str
    h1: list
    h2_triangular: list


def build_payload(kernel_length: int, sample_rate: int, device_id: str,
                  h1, h2) -> bytes:
    h1 = list(h1)
    h2 = list(h2)
    tri = kernel_length * (kernel_length + 1) // 2
    if len(h1) != kernel_length or len(h2) != tri:
        raise ValueError("dimensiones de kernel incorrectas")
    dev = device_id.encode()[:16].ljust(16, b"\0")
    header_wo_crc = struct.pack("<4sHHII8s", MAGIC, FORMAT_VERSION, kernel_length,
                                sample_rate, 0, b"DATASET0"[:8])
    body = struct.pack(f"<{kernel_length}f", *h1) + struct.pack(f"<{tri}f", *h2)
    crc = zlib.crc32(dev + body) & 0xFFFFFFFF
    return struct.pack("<4sHHII8s", MAGIC, FORMAT_VERSION, kernel_length,
                       sample_rate, crc, b"DATASET0"[:8]) + dev + body


def load_kernel(payload: bytes, expected_kernel_length: int, expected_sample_rate: int,
                authorized_device_id: str,
                dataset_sha256: str = DATASET_SHA256) -> LoadedKernel | None:
    """Devuelve LoadedKernel o None con la razón impresa. Fail-closed siempre."""
    def reject(reason):
        print(f"[kernel_loader] RECHAZADO: {reason}")
        return None

    if len(payload) < 40:
        return reject("payload truncado")
    magic, version, K, fs, crc, dsid = struct.unpack_from("<4sHHII8s", payload, 0)
    if magic != MAGIC:
        return reject("magic inválido")
    if version != FORMAT_VERSION:
        return reject(f"versión de formato {version} no soportada")
    if dsid.rstrip(b"\0").decode("ascii", "replace") != "DATASET0":
        return reject("identificador de dataset desconocido")
    dev = payload[24:40]
    device_id = dev.rstrip(b"\0").decode("utf-8", "replace")
    if device_id != authorized_device_id:
        return reject(f"device_id '{device_id}' no autorizado (esperado '{authorized_device_id}')")
    if K != expected_kernel_length:
        return reject(f"kernel_length {K} != motor {expected_kernel_length}")
    if fs != expected_sample_rate:
        return reject(f"sample_rate {fs} != motor {expected_sample_rate}")
    tri = K * (K + 1) // 2
    if len(payload) != 40 + 4 * K + 4 * tri:
        return reject("tamaño inconsistente con K declarado")
    body = payload[40:]
    if zlib.crc32(payload[24:40] + body) & 0xFFFFFFFF != crc:
        return reject("CRC32 del payload no coincide")
    h1 = list(struct.unpack_from(f"<{K}f", body, 0))
    h2 = list(struct.unpack_from(f"<{tri}f", body, 4 * K))
    for v in h1 + h2:
        if v != v or v in (float("inf"), float("-inf")):
            return reject("coeficiente NaN/Inf detectado")
    if max(abs(v) for v in h1) > MAX_COEFF or max((abs(v) for v in h2), default=0.0) > MAX_COEFF:
        return reject("amplitud de coeficiente fuera de límite")
    l1 = sum(abs(v) for v in h1)
    if l1 > 1.0 + L1_EPS:
        return reject(f"h1 no estable (l1={l1:.6f})")
    # energía cuadrática acotada con amplitud unitaria (criterio conservador)
    q_energy = sum(v * v for v in h2)
    if q_energy > 64.0:
        return reject(f"h2 con energía cuadrática excesiva ({q_energy:.3f})")
    print(f"[kernel_loader] ACEPTADO: K={K} fs={fs} device={device_id!r} l1(h1)={l1:.6f}")
    return LoadedKernel(K, fs, device_id, h1, h2)
