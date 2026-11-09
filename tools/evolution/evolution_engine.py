#!/usr/bin/env python3
"""
Motor de Auto-Evolución en Código de IVANNA — MVP (2026-11-09).

Ciclo completo:
  1. ESTADO     lee el repo (git log/status, nº de tests, cobertura por módulo).
  2. CONOCIMIENTO construye tools/evolution/knowledge/knowledge.json:
     índice del codebase, patrón SAF_GUARD de referencia, candidatos.
  3. DETECTAR   reglas deterministas (sin LLM): funciones SAF sin guardas,
     funciones muertas (definidas, nunca llamadas), módulos sin test.
  4. PROPONER   emite la propuesta (parche + justificación) para el candidato.
  5. VALIDAR    aplica el parche en el working tree, compila el/los tests,
     corre la suite host completa, registra el resultado y REVIERTE el
     working tree. El entregable es el diff + reporte, no el cambio aplicado.
  6. REPORTE    tools/evolution/proposals/<id>/REPORT.md + validation.json.

Dónde vive el motor (decisión de arquitectura, ver README.md):
  HÍBRIDO. El teléfono solo recolecta telemetría/logs y propone vía el
  daemon; el motor que ESCRIBE código vive en la VM/servidor, donde está
  el repo, el compilador host y la CI. La app nunca reescribe su fuente.

Generación de propuestas (honestidad del MVP):
  La detección y la validación son automáticas. La REDACCIÓN del parche
  en este MVP la hace el analista (LLM) a partir del paquete de
  conocimiento; el siguiente paso es conectar IvannaGeminiAgent con la
  API de Gemini para que el LLM redacte el parche directamente a partir
  de knowledge.json (ver README.md § "Qué falta").

Gates de seguridad (nunca auto-modificar):
  app/build.gradle.kts, app/src/main/cpp/CMakeLists.txt,
  app/src/main/java/com/ivanna/omega/security/*,
  .github/workflows/*, tools/security/*, *.keystore, *PAT*, *SECRET*.

Uso:
  python3 tools/evolution/evolution_engine.py --scan
      Solo fases 1-3: estado + conocimiento + detección (no toca el árbol).
  python3 tools/evolution/evolution_engine.py --proposal 0001 --validate
      Ciclo completo para la propuesta 0001 (aplica, valida, revierte).
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
EVO = REPO / "tools" / "evolution"
KNOWLEDGE = EVO / "knowledge" / "knowledge.json"
PROPOSALS = EVO / "proposals"

# Archivos que el motor NUNCA toca (gate de seguridad).
FORBIDDEN = (
    "app/build.gradle.kts",
    "app/src/main/cpp/CMakeLists.txt",
    "app/src/main/java/com/ivanna/omega/security/",
    ".github/workflows/",
    "tools/security/",
)

FORBIDDEN_SUFFIX = (".keystore", ".jks", ".pem", ".key")


def run(cmd, cwd=REPO, check=False):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, shell=isinstance(cmd, str))
    if check and r.returncode != 0:
        raise RuntimeError(f"cmd failed: {cmd}\n{r.stdout}\n{r.stderr}")
    return r


def is_forbidden(rel: str) -> bool:
    return (rel.startswith(FORBIDDEN)
            or rel.endswith(FORBIDDEN_SUFFIX)
            or "SECRET" in rel.upper()
            or "PAT" in rel)


# ── Fase 1: estado ──────────────────────────────────────────────────────────
def repo_state():
    log = run(["git", "log", "--oneline", "-5"], check=True).stdout.strip()
    status = run(["git", "status", "--short"]).stdout.strip()
    n_tests = len(re.findall(r"add_test\(NAME", (REPO / "app/src/main/cpp/tests/CMakeLists.txt").read_text()))
    return {"head": log.splitlines(), "dirty": status.splitlines(), "ctest_targets": n_tests}


# ── Fase 2+3: conocimiento + detección ──────────────────────────────────────
def scan_saf_guards():
    """Detecta funciones SAF sin las guardas del patrón SAF_GUARD (2026-09-17).

    Referencia: SAFUpdate en app/src/main/cpp/include/saf_runtime.h tiene
    guarda NaN (isfinite) + denominador degenerado. Cualquier función del
    mismo módulo sin isfinite es candidata.
    """
    hits = []
    for path in sorted((REPO / "app/src/main/cpp").rglob("saf_*.cpp")):
        src = path.read_text(errors="ignore")
        if "isfinite" not in src:
            funcs = re.findall(r"(?:double|float|void|int)\s+(\w+)\s*\(", src)
            hits.append({"file": str(path.relative_to(REPO)),
                         "functions": funcs, "has_isfinite_guard": False})
    return hits


def scan_dead_functions():
    """Funciones definidas en .cpp sin llamadores en todo el repo."""
    dead = []
    for path in sorted((REPO / "app/src/main/cpp").rglob("*.cpp")):
        src = path.read_text(errors="ignore")
        for m in re.finditer(r"(?:^|\n)(?:double|float|void|int|bool)\s+([A-Z]\w+)\s*\(", src):
            name = m.group(1)
            refs = run(["git", "grep", "-l", name, "--", "app/src/main"]).stdout.strip()
            callers = [l for l in refs.splitlines() if l and Path(l).resolve() != path.resolve()]
            if not callers:
                dead.append({"function": name, "file": str(path.relative_to(REPO))})
    return dead


def scan_untested_modules():
    """Módulos .cpp de primer nivel sin referencia en tests/CMakeLists.txt."""
    cmake = (REPO / "app/src/main/cpp/tests/CMakeLists.txt").read_text()
    out = []
    for path in sorted((REPO / "app/src/main/cpp").glob("*.cpp")):
        if path.name not in cmake:
            out.append(str(path.relative_to(REPO)))
    return out


def build_knowledge():
    state = repo_state()
    knowledge = {
        "generated_by": "tools/evolution/evolution_engine.py --scan",
        "repo_state": state,
        "reference_pattern": {
            "name": "SAF_GUARD (2026-09-17)",
            "where": "app/src/main/cpp/include/saf_runtime.h :: SAFUpdate",
            "rules": [
                "guardar entradas no-finitas con std::isfinite (fail-closed)",
                "denominador degenerado (<=0 o no finito) -> paso cero seguro",
                "sanitizar parámetros ANTES de usarlos en el cálculo",
            ],
        },
        "detectors": {
            "saf_without_guards": scan_saf_guards(),
            "dead_functions": scan_dead_functions(),
            "untested_top_level_modules": scan_untested_modules(),
        },
    }
    KNOWLEDGE.parent.mkdir(parents=True, exist_ok=True)
    KNOWLEDGE.write_text(json.dumps(knowledge, indent=2, ensure_ascii=False))
    return knowledge


# ── Fase 5: validar una propuesta ───────────────────────────────────────────
def validate_proposal(pid: str) -> dict:
    pdir = PROPOSALS / pid
    patch = next(pdir.glob("*.patch"), None)
    if patch is None:
        raise SystemExit(f"sin parche en {pdir}")
    # 1. Gate: el parche no debe tocar archivos prohibidos.
    # "+++ b/<path>" tiene 6 caracteres de prefijo; l[4:] dejaba "b/<path>"
    # y el checkout/revert no tocaba nada (bug encontrado en la corrida 1).
    touched = [l[6:] for l in patch.read_text().splitlines()
               if l.startswith("+++ b/")]
    bad = [t for t in touched if is_forbidden(t)]
    if bad:
        raise SystemExit(f"GATE: la propuesta toca archivos prohibidos: {bad}")
    # 2. El árbol debe estar limpio en los archivos del parche.
    dirty = run(["git", "status", "--porcelain", "--"] + touched).stdout.strip()
    if dirty:
        raise SystemExit(f"árbol sucio en archivos de la propuesta, aborta:\n{dirty}")
    result = {"proposal": pid, "patch": patch.name, "files": touched}
    try:
        r = run(["git", "apply", "--check", str(patch)])
        if r.returncode != 0:
            result.update({"applied": False, "error": r.stderr})
            return result
        run(["git", "apply", str(patch)], check=True)
        result["applied"] = True
        # 3. Configurar + compilar solo el target nuevo (si lo hay).
        cmake = os.environ.get("IVANNA_CMAKE", str(Path.home() / ".local/bin/cmake"))
        run([cmake, "-S", "app/src/main/cpp/tests", "-B", "build-tests"], check=True)
        test_bins = sorted(set(re.findall(r"add_executable\((\w+)", patch.read_text())))
        built, test_runs = [], {}
        for t in test_bins:
            b = run([cmake, "--build", "build-tests", "--target", t, "-j4"])
            built.append({"target": t, "ok": b.returncode == 0,
                          "tail": (b.stdout + b.stderr)[-500:]})
            if b.returncode == 0:
                tr = run([f"./{t}"], cwd=REPO / "build-tests")
                test_runs[t] = {"passed": tr.returncode == 0,
                                "tail": tr.stdout[-500:]}
        result["built"] = built
        result["new_tests"] = test_runs
        # 4. Suite completa.
        run([cmake, "--build", "build-tests", "-j4"], check=True)
        ct = run(["ctest", "--output-on-failure"], cwd=REPO / "build-tests")
        result["full_suite"] = {"passed": ct.returncode == 0,
                                "tail": ct.stdout[-800:]}
    finally:
        # 5. REVERTIR siempre: el entregable es el diff, no el árbol tocado.
        # `git checkout -- <paths>` falla ENTERO si algún pathspec es un
        # archivo nuevo (untracked) del parche: por eso la corrida 2 dejó
        # los 2 archivos modificados sin revertir. Separar: tracked ->
        # checkout; nuevo -> unlink. Y VERIFICAR que quedó limpio.
        for t in touched:
            p = REPO / t
            tracked = run(["git", "ls-files", "--", t]).stdout.strip()
            if tracked:
                run(["git", "checkout", "--", t])
            elif p.exists():
                p.unlink()
        left = run(["git", "status", "--porcelain", "--"] + touched).stdout.strip()
        result["reverted"] = (left == "")
        result["revert_leftover"] = left
    (pdir / "validation.json").write_text(json.dumps(result, indent=2, ensure_ascii=False))
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--proposal")
    ap.add_argument("--validate", action="store_true")
    args = ap.parse_args()
    if args.scan or (not args.proposal and not args.validate):
        k = build_knowledge()
        print(json.dumps(k, indent=2, ensure_ascii=False))
    if args.proposal and args.validate:
        print(json.dumps(validate_proposal(args.proposal), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
