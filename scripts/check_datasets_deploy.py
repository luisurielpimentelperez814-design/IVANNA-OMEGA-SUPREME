#!/usr/bin/env python3
"""
[0.6] CHECK DATASETS DEPLOY — SOFA / HRTF / RIR / SAF desplegados de punta a punta (v2.5.31).

Por qué existe: durante meses el APK (build.gradle.kts: ignoreAssetsPatterns "*.sofa") y el zip del módulo
(build.yml: -x "*.sofa") EXCLUÍAN los SOFA, al repo del módulo le faltaban 200 HpIR de ari/ y nadie lo notó:
customize.sh imprimía «SOFA no encontrado» y los paneles decían «no desplegado». Ningún chequeo miraba que los
datasets llegaran de verdad a donde se consumen. Este mira:

  1. Paridad assets del APK <-> árbol del módulo (mismos archivos y tamaños; csv/json normalizando fin de línea).
  2. Integridad básica: cabecera mágica (HDF5 en .sofa, IHR1, RIFF), sin punteros Git-LFS ni archivos truncados.
  3. RIR: nº de .wav == filas de metadata.csv. HRTF: cada .ihr1 coincide con el SHA-256 de hrtf_index.json.
  4. SAF_model.json idéntico en assets, módulo (saf/ y system/etc) y JSON válido.
  5. Nada en build.gradle.kts ni en los workflows excluye datasets del APK/zip.
  6. customize.sh despliega los cuatro datasets (SOFA de forma recursiva) y la app llama al despliegue.
"""
import hashlib, json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "app/src/main/assets/ivanna_omega"
MODULE = ROOT / "magisk_module/system/etc/ivanna_omega"
errors, notes = [], []

def err(msg): errors.append(msg)

def files(base: Path, pattern: str):
    return {p.relative_to(base).as_posix(): p for p in base.rglob(pattern) if p.is_file()}

def norm(p: Path) -> bytes:
    return p.read_bytes().replace(b"\r\n", b"\n")

# 1) paridad + 2) integridad ---------------------------------------------------------------------------
MAGIC = {".sofa": b"\x89HDF", ".ihr1": b"IHR1", ".wav": b"RIFF"}
for ds, ext in (("sofa", ".sofa"), ("hrtf", ".ihr1"), ("rir", ".wav")):
    a, m = files(ASSETS / ds, f"*{ext}"), files(MODULE / ds, f"*{ext}")
    if not a: err(f"[{ds}] el APK no tiene ningún {ext} en assets/ivanna_omega/{ds}")
    if not m: err(f"[{ds}] el módulo no tiene ningún {ext} en system/etc/ivanna_omega/{ds}")
    for rel in sorted(set(a) - set(m)): err(f"[{ds}] falta en el módulo: {ds}/{rel}")
    for rel in sorted(set(m) - set(a)): err(f"[{ds}] falta en el APK (assets): {ds}/{rel}")
    for rel in sorted(set(a) & set(m)):
        if a[rel].stat().st_size != m[rel].stat().st_size: err(f"[{ds}] tamaño distinto APK/módulo: {ds}/{rel}")
    for where, d in (("APK", a), ("módulo", m)):
        for rel, p in d.items():
            head = p.open("rb").read(16)
            if head.startswith(b"version https://git-lfs"): err(f"[{ds}] puntero Git-LFS en {where}: {ds}/{rel}")
            elif not head.startswith(MAGIC[ext]): err(f"[{ds}] cabecera inválida en {where}: {ds}/{rel}")
            elif p.stat().st_size < 600: err(f"[{ds}] archivo truncado ({p.stat().st_size} B) en {where}: {ds}/{rel}")
    notes.append(f"{ds}: {len(a)} en APK / {len(m)} en módulo")

# 3) RIR metadata + HRTF índice ------------------------------------------------------------------------
for where, base in (("APK", ASSETS), ("módulo", MODULE)):
    meta = base / "rir/metadata.csv"
    if not meta.is_file(): err(f"[rir] falta metadata.csv en {where}"); continue
    rows = [l for l in norm(meta).decode("utf-8", "replace").split("\n") if l.strip()][1:]
    n = len(list((base / "rir").glob("*.wav")))
    if len(rows) != n: err(f"[rir] {where}: metadata.csv tiene {len(rows)} filas y hay {n} .wav")
    idx = base / "hrtf/hrtf_index.json"
    if idx.is_file():
        try:
            data = json.loads(idx.read_text(encoding="utf-8"))
            ents = data.get("subjects") or data.get("files") or data
            ents = ents if isinstance(ents, list) else list(ents.values())
            for e in ents:
                if isinstance(e, dict) and e.get("file") and e.get("sha256"):
                    f = base / "hrtf" / e["file"]
                    if not f.is_file(): err(f"[hrtf] {where}: índice menciona {e['file']} que no existe")
                    elif hashlib.sha256(f.read_bytes()).hexdigest() != e["sha256"]:
                        err(f"[hrtf] {where}: SHA-256 distinto en {e['file']}")
        except Exception as ex:
            err(f"[hrtf] {where}: hrtf_index.json ilegible ({ex})")
    elif where == "módulo":
        err("[hrtf] falta hrtf_index.json en el módulo")
if (ASSETS / "rir/metadata.csv").is_file() and (MODULE / "rir/metadata.csv").is_file():
    if norm(ASSETS / "rir/metadata.csv") != norm(MODULE / "rir/metadata.csv"):
        err("[rir] metadata.csv distinto entre APK y módulo (ignorando fin de línea)")

# 4) SAF -----------------------------------------------------------------------------------------------
saf = [ASSETS / "SAF_model.json", ROOT / "magisk_module/saf/SAF_model.json", MODULE / "SAF_model.json"]
for p in saf:
    if not p.is_file(): err(f"[saf] falta {p.relative_to(ROOT)}")
if all(p.is_file() for p in saf):
    if len({hashlib.sha256(p.read_bytes()).hexdigest() for p in saf}) != 1:
        err("[saf] SAF_model.json no es idéntico en assets, saf/ y system/etc")
    try: json.loads(saf[0].read_text(encoding="utf-8"))
    except Exception as ex: err(f"[saf] SAF_model.json no es JSON válido ({ex})")
    notes.append(f"saf: SAF_model.json {saf[0].stat().st_size // 1024} KB idéntico en 3 sitios")

# 5) exclusiones ---------------------------------------------------------------------------------------
gradle = (ROOT / "app/build.gradle.kts").read_text(encoding="utf-8")
for m in re.finditer(r'ignoreAssetsPatterns\s*\+?=\s*(?:listOf\()?([^\n]+)', gradle):
    if re.search(r'sofa|ihr1|wav|rir|hrtf|saf|json', m.group(1), re.I):
        err(f"[apk] build.gradle.kts excluye datasets de los assets: {m.group(0).strip()}")
for wf in (ROOT / ".github/workflows").glob("*.yml"):
    for i, line in enumerate(wf.read_text(encoding="utf-8").splitlines(), 1):
        if "zip " in line and re.search(r'-x\s+["\']?[^"\'\s]*(sofa|ihr1|\.wav|rir|hrtf|SAF)', line, re.I):
            err(f"[zip] {wf.name}:{i} excluye datasets del zip del módulo")

# 6) cableado ------------------------------------------------------------------------------------------
cust = (ROOT / "magisk_module/customize.sh").read_bytes().decode("utf-8", "replace")
for needle, what in (('cp -rf "$SOFA_SRC"/.', "SOFA recursivo (incluye ari/)"), ("RIR_SRC", "RIR"),
                     ("HRTF_SRC", "HRTF"), ("SAF_model.json", "SAF")):
    if needle not in cust: err(f"[customize.sh] no despliega {what}")
app = (ROOT / "app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt").read_text(encoding="utf-8")
if "IvannaAssetProvider" not in app: err("[app] IVANNAApplication no llama a IvannaAssetProvider (sin despliegue sin root)")
prov = (ROOT / "app/src/main/java/com/ivanna/omega/core/IvannaAssetProvider.kt").read_text(encoding="utf-8")
if "MARKER_PREFIX" not in prov or ".part" not in prov: err("[app] IvannaAssetProvider sin marcador de versión / escritura atómica")

print("=== [0.6] CHECK DATASETS DEPLOY REPORT ===")
for n in notes: print("  -", n)
if errors:
    for e in errors[:40]: print("  [FAIL]", e)
    print(f"\n[FAIL] {len(errors)} hallazgos de despliegue de datasets.")
    sys.exit(1)
print("\n[PASS] SOFA/HRTF/RIR/SAF consistentes entre APK y módulo, íntegros y sin exclusiones.")
