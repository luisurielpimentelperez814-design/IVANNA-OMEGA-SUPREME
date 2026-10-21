#!/usr/bin/env python3
"""
check_arcore_manifest.py — Puerta: ARCore declarado en Gradle => manifiesto de la APP completo.

Causa raíz que previene (visto en campo, pantalla «CAPTURA ESPACIAL · ARCORE»):
    «ARCore no disponible: Application manifest must contain meta-data com.google.ar.core»
La dependencia `com.google.ar:core` NO basta: el SDK exige en el AndroidManifest de la
APP un <meta-data android:name="com.google.ar.core" android:value="required|optional"/>.

Falla (exit 1) si build.gradle.kts declara `com.google.ar:core` y el manifiesto:
  1. no tiene ese meta-data dentro de <application>, o su valor no es required/optional;
  2. no declara el permiso CAMERA;
  3. no declara uses-feature android.hardware.camera.ar (o lo marca required=true
     mientras el meta-data es optional: la app dejaría de instalarse sin ARCore).
"""
from __future__ import annotations
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GRADLE = ROOT / "app" / "build.gradle.kts"
MANIFEST = ROOT / "app" / "src" / "main" / "AndroidManifest.xml"
A = "{http://schemas.android.com/apk/res/android}"


def main() -> int:
    if not re.search(r'com\.google\.ar:core', GRADLE.read_text(encoding="utf-8")):
        print("[OK] ARCore no está declarado en Gradle: nada que verificar.")
        return 0

    root = ET.parse(MANIFEST).getroot()
    errors: list[str] = []

    app = root.find("application")
    meta = [m for m in (app.findall("meta-data") if app is not None else [])
            if m.get(A + "name") == "com.google.ar.core"]
    value = meta[0].get(A + "value") if meta else None
    if not meta:
        errors.append('falta <meta-data android:name="com.google.ar.core"> dentro de <application>')
    elif value not in ("required", "optional"):
        errors.append(f'meta-data com.google.ar.core tiene valor {value!r}; debe ser "required" u "optional"')

    perms = {p.get(A + "name") for p in root.findall("uses-permission")}
    if "android.permission.CAMERA" not in perms:
        errors.append("falta <uses-permission android.permission.CAMERA>")

    feats = {f.get(A + "name"): f.get(A + "required", "true") for f in root.findall("uses-feature")}
    if "android.hardware.camera.ar" not in feats:
        errors.append('falta <uses-feature android:name="android.hardware.camera.ar">')
    elif value == "optional" and feats["android.hardware.camera.ar"] != "false":
        errors.append('meta-data es "optional" pero camera.ar es required=true: la app no se instalaría sin ARCore')

    if errors:
        print("[FAIL] ARCore declarado en Gradle pero el manifiesto está incompleto:")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f'[OK] ARCore: meta-data="{value}", permiso CAMERA y uses-feature camera.ar presentes.')
    return 0


if __name__ == "__main__":
    sys.exit(main())
