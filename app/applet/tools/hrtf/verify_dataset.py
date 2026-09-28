#!/usr/bin/env python3
"""verify_dataset.py — puerta de validacion de datasets HRTF antes de publicarlos.

POR QUE EXISTE (flanco herramientas HRTF, 2026-09-09):
  El pipeline de conversion no tenia NINGUNA validacion post-conversion, y
  el resultado fue un desastre real en produccion: hrtf_database.bin estuvo
  corrupto en main durante semanas (pasado por un codec de texto UTF-8 —
  cabecera declaraba 45.9M posiciones x 33.5M taps sobre un archivo de
  2.9 MB). Ningun lector del repo podia cargarlo, y loadIVHRTF01() habria
  hecho OOM en dispositivo si alguien lo intentaba. Todo eso se habria
  detectado en 1 segundo con esta herramienta.

QUE VALIDA (reglas replicadas 1:1 de los lectores C++ — la fuente de
verdad es lo que el MOTOR acepta, no lo que el escritor cree haber
escrito):

  IHR1 (lector canonico: app/src/main/cpp/spatial/ihr1_format.hpp):
    - magic "IHR1" (4 bytes)
    - cabecera: i32 numPos, i32 irLen, i32 sampleRateHz
    - numPos y irLen en (0, 8192]  (mismo acotado que ihr1::read())
    - tamano del fichero EXACTO para el layout AZ   (16 + pos*(4 + 2*irLen*4))
                                o el layout AZEL (16 + pos*(8 + 2*irLen*4))
      — es lo unico que distingue los dos layouts; un fichero que no
      encaja con ninguno es rechazado por el motor.
    - datos: sin NaN/Inf, energia no nula, |x| <= 4.0 (un HRIR medido y
      normalizado no supera ~1; el margen x4 admite ganancia de medicion
      pero caza basura binaria, que da valores de 1e30).
    - angulos FISICAMENTE validos (finitos y dentro del dominio de la
      esfera), NO una convencion concreta: los datasets reales del repo
      usan azimut en [0, 360) (kemar.ihr1, cipic_011.ihr1 — verificado
      2026-09-09 leyendo sus tablas) mientras sofa_to_ihr1.py normaliza a
      [-180, 180]. Ambas convenciones son legitimas y el motor las acepta;
      la puerta solo rechaza lo que fisicamente no puede ser un angulo
      (|az| > 360, |el| > 90 + epsilon, NaN). CIPIC llega a el = +90
      exacto (cipic_011.ihr1), asi que el techo de elevacion INCLUYE 90.

  IVHRTF01 (lector legacy: app/src/main/cpp/HRTFBinLoader.cpp):
    - magic "IVHRTF01" (8 bytes)
    - cabecera: f32 sampleRate, u32 positions, u32 channels, u32 taps
    - positions y taps en (0, 8192]  (el lector NO los acota — la puerta
      debe ser mas estricta que el lector, nunca menos)
    - channels == 2 (el motor es estereo; otro valor es un error del
      escritor, no una feature)
    - tamano EXACTO: 24 + positions*channels*taps*4
    - mismas comprobaciones de datos.
    - AVISO, no error: el formato no trae tabla angular y el motor no lo
      prioriza — para datasets nuevos usa sofa_to_ihr1.py (IHR1).

USO:
  python3 tools/hrtf/verify_dataset.py <archivo> [<archivo> ...]
  exit 0 si TODOS pasan, 1 si alguno falla. Apto para CI/pre-commit.
"""

import struct
import sys

import numpy as np

MAX_POS = 8192   # mismo limite que ihr1::read() en ihr1_format.hpp
MAX_TAPS = 8192
ABS_LIMIT = 4.0  # HRIR normalizado ~<=1; x4 de margen, caza basura binaria


class Reject(Exception):
    pass


def _check_samples(arr: np.ndarray, what: str) -> None:
    if not np.isfinite(arr).all():
        raise Reject(f"{what}: contiene NaN o Inf")
    peak = float(np.abs(arr).max()) if arr.size else 0.0
    if peak == 0.0:
        raise Reject(f"{what}: energia nula — todos los taps son 0.0")
    if peak > ABS_LIMIT:
        raise Reject(f"{what}: |x| max = {peak:.3g} supera {ABS_LIMIT} "
                     f"(un HRIR real no llega; huele a basura binaria)")


def verify_ihr1(data: bytes, name: str) -> str:
    if len(data) < 16:
        raise Reject("fichero mas pequeno que la cabecera IHR1 (16 bytes)")
    num_pos, ir_len, sr_hz = struct.unpack_from("<iii", data, 4)
    if not (0 < num_pos <= MAX_POS):
        raise Reject(f"numPos={num_pos} fuera de (0, {MAX_POS}]")
    if not (0 < ir_len <= MAX_TAPS):
        raise Reject(f"irLen={ir_len} fuera de (0, {MAX_TAPS}]")
    if not (8_000 <= sr_hz <= 768_000):
        raise Reject(f"sampleRateHz={sr_hz} fuera de rango fisico")

    size_az = 16 + num_pos * (4 + 2 * ir_len * 4)
    size_azel = 16 + num_pos * (8 + 2 * ir_len * 4)
    if len(data) == size_az:
        layout = "AZ"
    elif len(data) == size_azel:
        layout = "AZEL"
    else:
        raise Reject(
            f"tamano {len(data)} no encaja con ningun layout "
            f"(AZ esperaria {size_az}, AZEL {size_azel}) — el motor lo "
            f"rechazaria en silencio y cargaria el HRTF sintetico")

    off = 16
    if layout == "AZEL":
        angles = np.frombuffer(data, dtype="<f4", count=num_pos * 2, offset=off)
        az, el = angles[0::2], angles[1::2]
        # Dominio fisico de la esfera, no una convencion: az en cualquier
        # rango de anchura 360 (0..360 o -180..180) y el en [-90, +90]
        # (CIPIC llega a +90 exacto — verificado en cipic_011.ihr1).
        if not np.isfinite(angles).all():
            raise Reject("tabla az/el contiene NaN o Inf")
        if (np.abs(az) > 360.0).any():
            raise Reject(f"azimut fuera de dominio fisico (max |az|={float(np.abs(az).max()):.1f} > 360)")
        if (np.abs(el) > 90.0 + 1e-3).any():
            raise Reject(f"elevacion fuera de dominio fisico (max |el|={float(np.abs(el).max()):.1f} > 90)")
        off += num_pos * 8
        hrir = np.frombuffer(data, dtype="<f4", count=num_pos * 2 * ir_len,
                             offset=off)
    else:
        # Layout AZ: [az][L x irLen][R x irLen] intercalado por posicion.
        block = np.frombuffer(data, dtype="<f4", offset=16)
        block = block.reshape(num_pos, 1 + 2 * ir_len)
        az = block[:, 0]
        if not np.isfinite(az).all() or (np.abs(az) > 360.0).any():
            raise Reject("azimuts con NaN/Inf o fuera de dominio fisico (|az| > 360)")
        hrir = block[:, 1:].reshape(-1)

    _check_samples(hrir, f"{name} IHR1/{layout}")
    return (f"IHR1/{layout}: {num_pos} pos x {ir_len} taps @ {sr_hz} Hz "
            f"({len(data)} bytes) — OK")


def verify_ivhrtf01(data: bytes, name: str) -> str:
    if len(data) < 24:
        raise Reject("fichero mas pequeno que la cabecera IVHRTF01 (24 bytes)")
    (sr,) = struct.unpack_from("<f", data, 8)
    positions, channels, taps = struct.unpack_from("<III", data, 12)
    if not (0 < positions <= MAX_POS):
        raise Reject(f"positions={positions} fuera de (0, {MAX_POS}] "
                     f"— OOM garantizado en loadIVHRTF01() si se carga")
    if channels != 2:
        raise Reject(f"channels={channels}: el motor es estereo (2)")
    if not (0 < taps <= MAX_TAPS):
        raise Reject(f"taps={taps} fuera de (0, {MAX_TAPS}]")
    if not (8_000.0 <= sr <= 768_000.0):
        raise Reject(f"sampleRate={sr} fuera de rango fisico")

    expected = 24 + positions * channels * taps * 4
    if len(data) != expected:
        raise Reject(f"tamano {len(data)} != cabecera {expected} — "
                     f"lectura desalineada garantizada")

    hrir = np.frombuffer(data, dtype="<f4", count=positions * channels * taps,
                         offset=24)
    _check_samples(hrir, f"{name} IVHRTF01")
    return (f"IVHRTF01: {positions} pos x {taps} taps @ {int(sr)} Hz "
            f"({len(data)} bytes) — OK  [AVISO: formato legacy sin tabla "
            f"angular; para datasets nuevos usa sofa_to_ihr1.py -> IHR1]")


def verify(path: str) -> str:
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] == b"IHR1":
        return verify_ihr1(data, path)
    if data[:8] == b"IVHRTF01":
        return verify_ivhrtf01(data, path)
    raise Reject("magic desconocido (ni 'IHR1' ni 'IVHRTF01') — el motor "
                 "lo rechazaria en silencio")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    failures = 0
    for path in sys.argv[1:]:
        try:
            print(f"PASS  {path}: {verify(path)}")
        except Reject as exc:
            failures += 1
            print(f"FAIL  {path}: {exc}", file=sys.stderr)
        except OSError as exc:
            failures += 1
            print(f"FAIL  {path}: no legible ({exc})", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
