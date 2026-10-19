#!/usr/bin/env python3
"""Fase 0 del roadmap 10D: puerta de verdad de los diez ejes perceptuales.

Ejecutable sin Android SDK. Falla si:
  1) el snapshot de escena no declara exactamente 10 ejes;
  2) un eje se marca AxisState.APPLIED sin una variable de confirmacion del motor;
  3) la confirmacion se hardcodea (engineConfirmed = true);
  4) el contrato no expone los estados honestos ni la marca de tiempo;
  5) el runtime no ejecuta ninguna lectura nativa (read-back) en su camino de estado;
  6) el roadmap 10D no enumera los diez ejes ni su fase 0.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PKG = ROOT / "app/src/main/java/com/ivanna/omega/perceptual"
RUNTIME, CONTRACT = PKG / "PerceptualSceneRuntime.kt", PKG / "PerceptualSceneContract.kt"
ROADMAP = ROOT / "docs/ROADMAP_10D_PERCEPTUAL_2026-10-19.md"
KAXES = 10
issues = []
fail = issues.append

for p in (RUNTIME, CONTRACT, ROADMAP):
    if not p.exists():
        fail("falta %s" % p.relative_to(ROOT))
if issues:
    print("=== [0.1] CHECK 10D AXIS TRUTH REPORT ===")
    for m in issues:
        print("[FAIL] %s" % m)
    sys.exit(1)

rt, ct, rm = (p.read_text(encoding="utf-8") for p in (RUNTIME, CONTRACT, ROADMAP))

i = rt.find("val axes = listOf(")
if i < 0:
    fail("PerceptualSceneRuntime: no se encontro 'val axes = listOf('")
    n_axes = 0
else:
    depth, j = 0, i
    while j < len(rt):
        if rt[j] == "(":
            depth += 1
        elif rt[j] == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    blk = rt[i:j]
    n_axes = len(re.findall(r"(?<![\w.])status\s*\(", blk))
    if n_axes != KAXES:
        fail("se esperaban %d ejes en el snapshot, encontrados %d" % (KAXES, n_axes))
    if "PerceptualSceneSnapshot" not in rt:
        fail("el snapshot no se construye como PerceptualSceneSnapshot")

lines = rt.splitlines()
for k, l in enumerate(lines):
    if "AxisState.APPLIED" not in l:
        continue
    ctx = "\n".join(lines[max(0, k - 3):k + 4])
    if "onfirmed" not in ctx and "Confirmed" not in ctx:
        fail("PerceptualSceneRuntime.kt:%d: AxisState.APPLIED sin confirmacion del motor" % (k + 1))

if re.search(r"engineConfirmed\s*=\s*true", rt):
    fail("engineConfirmed = true literal: la confirmacion debe venir de una lectura real")

for f in ("engineConfirmed", "updatedAtEpochMs", "AxisState.DEGRADED", "AxisState.UNAVAILABLE", "AxisState.ERROR"):
    if f not in ct:
        fail("PerceptualSceneContract.kt: falta %s" % f)

reads = len(re.findall(r"NativeBridge\.|readStemPosition\(|safeIs", rt))
if reads == 0:
    fail("PerceptualSceneRuntime: ninguna lectura nativa (read-back) en el camino de estado")

rm_axes = len(re.findall(r"^\d+\.\s+\*\*", rm, flags=re.M))
if rm_axes < KAXES:
    fail("roadmap: solo %d ejes enumerados (se exigen %d)" % (rm_axes, KAXES))
if "Fase 0" not in rm:
    fail("roadmap: no contiene la Fase 0 (seguridad de la verdad del sistema)")

print("=== [0.1] CHECK 10D AXIS TRUTH REPORT ===")
print("Ejes en el snapshot de escena   : %d" % n_axes)
print("Ejes enumerados en el roadmap   : %d" % rm_axes)
print("Lecturas nativas en el runtime  : %d" % reads)
if issues:
    for m in issues:
        print("[FAIL] %s" % m)
    print("TOTAL ISSUES: %d" % len(issues))
    sys.exit(1)
print("[PASS] 0 hallazgos: ningun eje se declara aplicado sin confirmacion del motor.")
sys.exit(0)
