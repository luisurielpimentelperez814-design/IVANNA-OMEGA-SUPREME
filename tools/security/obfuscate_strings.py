#!/usr/bin/env python3
"""Genera el objeto SecStrings de IntegrityGate.kt (fase 2 anti-piratería).

Los literales sensibles de la puerta de integridad (nombre del archivo de
prefs, claves, tag de log, nombre del asset de hashes) NO van en claro en el
dex: van cifrados con XOR de un byte y se descifran en memoria al usarlos.
Este script regenera el objeto Kotlin completo a partir de los literales,
para no calcular los bytes a mano (error-prone).

Uso:
    python3 obfuscate_strings.py [--key 0x5A] [--check]

  --key 0x5A     Clave XOR (un byte, hex o decimal). Default 0x5A.
  --check        No genera: verifica que el objeto actual en
                 app/src/main/java/com/ivanna/omega/security/IntegrityGate.kt
                 coincide con lo que generaría este script (exit 1 si difiere).

La salida es el texto Kotlin del objeto SecStrings (para pegar en
IntegrityGate.kt o verificar con --check). El objeto generado es idéntico
en estructura al actual: las propiedades son `val ... get() = dec(...)`
(descifrado bajo demanda, sin guardar el claro en un campo).

CONFIDENCIAL (interno). Ver docs/SECURITY_INTERNAL.md.
"""
import argparse
import re
import sys
from pathlib import Path

# Literales de seguridad de IntegrityGate (nombre -> valor en claro).
# Si cambia un literal, se actualiza aquí y se regenera el objeto.
STRINGS = {
    "prefsFile": "ivanna_integrity",
    "keyAndroidId": "android_id",
    "keyVersionCode": "version_code",
    "tag": "IntegrityGate",
    "soHashesAsset": "ivanna_so_hashes.txt",
}

KT_PATH = Path(__file__).resolve().parent.parent.parent / "app/src/main/java/com/ivanna/omega/security/IntegrityGate.kt"


def encrypt(text: str, key: int) -> list[int]:
    return [ord(c) ^ key for c in text]


def generate(key: int) -> str:
    lines = [
        "internal object SecStrings {",
        f"    private const val KEY: Int = 0x{key:02X}",
        "",
        "    private fun dec(bytes: ByteArray): String {",
        "        val out = ByteArray(bytes.size)",
        "        for (i in bytes.indices) out[i] = (bytes[i].toInt() xor KEY).toByte()",
        "        return String(out, Charsets.UTF_8)",
        "    }",
        "",
    ]
    for name, value in STRINGS.items():
        enc = encrypt(value, key)
        bytes_txt = ", ".join(str(b) for b in enc)
        lines.append(f'    // "{value}"')
        lines.append("    val " + name + ": String")
        lines.append(f"        get() = dec(byteArrayOf({bytes_txt}))")
    lines.append("}")
    return "\n".join(lines) + "\n"


def extract_current(path: Path) -> str | None:
    text = path.read_text(encoding="utf-8")
    m = re.search(r"internal object SecStrings \{(.*?)\n\}", text, re.DOTALL)
    if not m:
        return None
    return "internal object SecStrings {" + m.group(1) + "\n}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--key", default="0x5A",
                    help="Clave XOR (un byte, hex o decimal). Default 0x5A.")
    ap.add_argument("--check", action="store_true",
                    help="Verifica que el objeto actual coincide con el generado.")
    args = ap.parse_args()
    try:
        key = int(args.key, 0)
    except ValueError:
        print(f"obfuscate_strings: ERROR: clave inválida: {args.key}", file=sys.stderr)
        return 1
    if not (0 <= key <= 0xFF):
        print("obfuscate_strings: ERROR: la clave debe ser un byte (0-255)",
              file=sys.stderr)
        return 1

    generated = generate(key)
    if args.check:
        current = extract_current(KT_PATH)
        if current is None:
            print(f"obfuscate_strings: ERROR: no se encontró SecStrings en {KT_PATH}",
                  file=sys.stderr)
            return 1
        if current.strip() == generated.strip():
            print("obfuscate_strings: SecStrings coincide con los literales (OK)")
            return 0
        print("obfuscate_strings: ERROR: SecStrings NO coincide con los literales.",
              file=sys.stderr)
        print("Regenerar con: python3 tools/security/obfuscate_strings.py",
              file=sys.stderr)
        return 1
    sys.stdout.write(generated)
    return 0


if __name__ == "__main__":
    sys.exit(main())
