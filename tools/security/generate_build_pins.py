#!/usr/bin/env python3
"""Genera el pin de certificado de firma para el módulo de integridad.

CONFIDENCIAL (interno): el pin es el SHA-256 del certificado con el que se
firma ESE build. No se publica ni se documenta en el README. Ver
docs/SECURITY_INTERNAL.md.

Uso:
    python3 tools/security/generate_build_pins.py \
        --keystore <ruta.keystore> --storepass <pass> --alias <alias> \
        --out app/src/main/cpp/security/ivanna_build_pins.h

El script extrae el SHA-256 del certificado con `keytool -list -v` y escribe
un header generado (gitignored). Falla con exit != 0 si no puede obtener el
pin: un build sin pin NO debe pasar en silencio (en release, la verificación
nativa fallaría fail-closed y el DSP quedaría bloqueado).

El keystore es el MISMO que firma el APK (ver signingConfig en
app/build.gradle.kts): KEYSTORE_FILE o ~/.android/debug.keystore, alias
androiddebugkey por defecto. Como el pin se genera del keystore que firma
ese build concreto, funciona tanto con keystore estable (secreto CI) como
con llave efímera (el pin de ese build coincide con su propio certificado).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

SHA256_RE = re.compile(r"SHA256:\s*([0-9A-Fa-f:]{95,})")


def ensure_debug_keystore(keystore: Path, storepass: str, alias: str) -> None:
    """Crea el keystore de depuración si no existe (mismos parámetros que
    usa Android Gradle Plugin para generarlo: RSA 2048, validez 10000 días,
    CN=Android Debug). AGP solo genera el debug keystore si no existe, así
    que si lo creamos aquí primero, AGP lo usará tal cual y el pin generado
    coincidirá con el certificado que firma el build. Solo se auto-crea
    cuando la ruta es el debug keystore por defecto (termina en
    debug.keystore) y las credenciales son las de depuración; un keystore
    personalizado ausente es un error de configuración y debe fallar."""
    if keystore.name != "debug.keystore" or storepass != "android" or alias != "androiddebugkey":
        raise SystemExit(f"ERROR: keystore no existe: {keystore}")
    keystore.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "keytool", "-genkeypair", "-v",
        "-keystore", str(keystore),
        "-storepass", storepass,
        "-keypass", storepass,
        "-alias", alias,
        "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
        "-dname", "CN=Android Debug,O=Android,C=US",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        raise SystemExit(
            f"ERROR: no se pudo crear el debug keystore {keystore}.\n{proc.stdout}{proc.stderr}"
        )
    print(f"NOTA: debug keystore creado en {keystore} (no existía; AGP lo usará tal cual).")


def cert_sha256_hex(keystore: Path, storepass: str, alias: str) -> str:
    cmd = [
        "keytool", "-list", "-v",
        "-keystore", str(keystore),
        "-storepass", storepass,
        "-alias", alias,
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    except FileNotFoundError:
        raise SystemExit("ERROR: 'keytool' no encontrado (se necesita el JDK).")
    except subprocess.TimeoutExpired:
        raise SystemExit("ERROR: keytool tardó demasiado (timeout 60s).")
    out = proc.stdout + proc.stderr
    if proc.returncode != 0:
        raise SystemExit(
            f"ERROR: keytool falló (exit {proc.returncode}) para keystore={keystore} alias={alias}.\n{out}"
        )
    m = SHA256_RE.search(out)
    if not m:
        raise SystemExit(
            f"ERROR: no se encontró la huella SHA256 en la salida de keytool para alias={alias}.\n{out}"
        )
    hexv = m.group(1).replace(":", "").lower()
    if len(hexv) != 64 or not all(c in "0123456789abcdef" for c in hexv):
        raise SystemExit(f"ERROR: huella SHA256 inválida: {hexv!r}")
    return hexv


def main() -> int:
    ap = argparse.ArgumentParser(description="Genera ivanna_build_pins.h")
    ap.add_argument("--keystore", required=True, help="Ruta al keystore de firma")
    ap.add_argument("--storepass", required=True, help="Password del keystore")
    ap.add_argument("--alias", required=True, help="Alias de la clave")
    ap.add_argument("--out", required=True, help="Header generado de salida")
    args = ap.parse_args()

    keystore = Path(args.keystore).expanduser()
    if not keystore.is_file():
        # En preBuild el debug keystore aún no existe en una máquina limpia
        # (AGP lo genera en el paso de firma, después de preBuild). Si es el
        # debug keystore por defecto, crearlo aquí con los parámetros de AGP.
        ensure_debug_keystore(keystore, args.storepass, args.alias)

    hexv = cert_sha256_hex(keystore, args.storepass, args.alias)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    header = (
        "// GENERATED — no editar a mano.\n"
        "// Escrito por tools/security/generate_build_pins.py en build-time\n"
        "// desde el keystore que firma ESE build. No se commitea (gitignored).\n"
        "// CONFIDENCIAL: no publicar este valor.\n"
        "#pragma once\n"
        f'#define IVANNA_PIN_CERT_SHA256 "{hexv}"\n'
        "#define IVANNA_BUILD_PINS_PRESENT 1\n"
    )
    out.write_text(header, encoding="utf-8")
    print(f"OK: pin de certificado escrito en {out} (sha256={hexv[:16]}...)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
