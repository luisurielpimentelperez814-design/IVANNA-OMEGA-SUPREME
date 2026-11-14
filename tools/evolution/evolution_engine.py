#!/usr/bin/env python3
"""
Motor de Auto-Evolución en Código de IVANNA — MVP (2026-11-09).

Ciclo completo:
  1. ESTADO     lee el repo (git log/status, nº de tests, cobertura por módulo).
  2. CONOCIMIENTO construye tools/evolution/knowledge/knowledge.json:
     índice del codebase, patrón SAF_GUARD de referencia, candidatos.
  3. DETECTAR   reglas deterministas (sin LLM): funciones SAF sin guardas,
     funciones muertas (definidas, nunca llamadas), módulos sin test,
     chequeos host de Fase 0 en rojo (scripts/check_*.py, 0003) y
     TODO/FIXME en el C++ nativo (0003; --scan ~30 s con los 6 chequeos).
  4. PROPONER   emite la propuesta (parche + justificación) para el candidato.
  5. VALIDAR    valida el parche en un git worktree desechable (scratch):
     compila el/los tests, corre la suite host completa (o las auto-pruebas
     del motor si el parche no toca C++), registra el resultado y ELIMINA
     el scratch. El árbol real nunca se toca. El entregable es el diff +
     validation.json + REPORT.md, no el cambio aplicado.
  6. REPORTE    tools/evolution/proposals/<id>/REPORT.md + validation.json.

Dónde vive el motor (decisión de arquitectura, ver README.md):
  HÍBRIDO. El teléfono solo recolecta telemetría/logs y propone vía el
  daemon; el motor que ESCRIBE código vive en la VM/servidor, donde está
  el repo, el compilador host y la CI. La app nunca reescribe su fuente.

Generación de propuestas (2026-11-14, motor completo):
  La detección es determinista y la REDACCIÓN del parche la hace el LLM
  vía OmniRoute (tools/evolution/llm_client.py, modelo "ivanna-free",
  key solo en ~/.omniroute/ivanna-app-key). El LLM recibe solo código
  relevante + diagnóstico; nunca credenciales ni secretos. La validación
  ocurre en un git worktree desechable (scratch): el árbol real nunca se
  toca. Ver tools/evolution/PIPELINE.md.

Uso:
  python3 tools/evolution/evolution_engine.py --scan
      Solo fases 1-3: estado + conocimiento + detección (no toca el árbol).
  python3 tools/evolution/evolution_engine.py --proposal 0001 --validate
      Ciclo completo para la propuesta 0001 (valida en scratch, no aplica).
  python3 tools/evolution/evolution_engine.py --draft 0002 --finding '{...}'
      El LLM redacta el parche para el hallazgo JSON (sin validar).
  python3 tools/evolution/evolution_engine.py --evolve 0002 --finding '{...}'
      Detectar → reescribir (LLM) → verificar en scratch → REPORT.md.
      No aplica al árbol, no commitea, no publica.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
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

# Archivos que el LLM NUNCA redacta en un draft (además de FORBIDDEN).
# - Los archivos de versión los mueve SOLO el publish de
#   tools/autorepair/run_fix.py (bump + CHANGELOG); un draft no los toca.
# - app/src/main/java/com/ivanna/omega/audio/ es el área del doctor runtime
#   que construye el agente principal (2026-11-14): el motor no la toca.
DRAFT_FORBIDDEN = FORBIDDEN + (
    "app/src/main/java/com/ivanna/omega/audio/",
    "version.properties",
    "magisk_module/module.prop",
    "magisk_module/update.json",
    "CHANGELOG.md",
    "README.md",
)

FORBIDDEN_SUFFIX = (".keystore", ".jks", ".pem", ".key")


def run(cmd, cwd=REPO, check=False, timeout=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                       shell=isinstance(cmd, str), timeout=timeout)
    if check and r.returncode != 0:
        raise RuntimeError(f"cmd failed: {cmd}\n{r.stdout}\n{r.stderr}")
    return r


def is_forbidden(rel: str) -> bool:
    return (rel.startswith(FORBIDDEN)
            or rel.endswith(FORBIDDEN_SUFFIX)
            or "SECRET" in rel.upper()
            or "PAT" in rel)


def _strip_cmake_comments(text: str) -> str:
    """Quita comentarios `#` de CMake sin tocar el contenido entre comillas.

    Basta para los escaneos por regex: un `#` dentro de comillas no es
    comentario y un `#` fuera corta la línea.
    """
    out = []
    for line in text.splitlines():
        buf = []
        in_quote = False
        escaped = False
        for ch in line:
            if escaped:
                buf.append(ch)
                escaped = False
                continue
            if ch == "\\":
                buf.append(ch)
                escaped = True
                continue
            if ch == '"':
                in_quote = not in_quote
                buf.append(ch)
                continue
            if ch == "#" and not in_quote:
                break
            buf.append(ch)
        out.append("".join(buf))
    return "\n".join(out)


def _count_ctest_targets(cmake_text: str) -> int:
    """Cuenta los targets CTest registrados en tests/CMakeLists.txt.

    El conteo viejo (`add_test\\(NAME`) daba 36 cuando el total real es
    ~104: los tests se registran sobre todo con el helper
    `ivanna_add_test(name)` (que usa `gtest_discover_tests`), no con
    `add_test(NAME ...)` directo. Se cuentan los nombres únicos de:
      - invocaciones `ivanna_add_test(<nombre>)` (sin la definición de la
        función: la línea `function(ivanna_add_test name)` casa el regex
        con el nombre `name` y hay que excluirla explícitamente);
      - `add_test(NAME <nombre> ...)` directos.
    Los comentarios se quitan antes (una línea `# ivanna_add_test(fake)`
    no registra nada).
    """
    text = _strip_cmake_comments(cmake_text)
    names = set()
    for m in re.finditer(r"\bivanna_add_test\s*\(\s*([A-Za-z0-9_./:-]+)", text):
        line_start = text.rfind("\n", 0, m.start()) + 1
        line = text[line_start:text.find("\n", m.start())]
        if line.strip().startswith("function("):
            continue  # la definición del helper, no un registro
        names.add(m.group(1))
    for m in re.finditer(r"\badd_test\s*\(\s*NAME\s+([A-Za-z0-9_./:-]+)",
                         text, flags=re.IGNORECASE):
        names.add(m.group(1))
    return len(names)


# ── Fase 1: estado ──────────────────────────────────────────────────────────
def repo_state():
    log = run(["git", "log", "--oneline", "-5"], check=True).stdout.strip()
    status = run(["git", "status", "--short"]).stdout.strip()
    cmake_text = (REPO / "app/src/main/cpp/tests/CMakeLists.txt").read_text(errors="ignore")
    n_tests = _count_ctest_targets(cmake_text)
    return {"head": log.splitlines(), "dirty": status.splitlines(), "ctest_targets": n_tests}


# ── Fase 2+3: conocimiento + detección ──────────────────────────────────────
def scan_saf_guards():
    """Detecta módulos SAF sin guarda isfinite con trazabilidad de origen.

    Reglas corregidas tras falsos positivos medidos el 2026-11-14
    (propuesta 0002, redactada por el LLM vía OmniRoute):
    1) Un archivo sin funciones detectadas no es hit, aunque tampoco tenga
       guarda (saf_stimulus_jni.cpp: solo JNIEXPORT jboolean, el regex no
       casa ninguna función; antes se listaba con "functions": []).
    2) Los nombres de función se deduplican preservando el orden de aparición
       (ivanna_saf_apply_latent salía dos veces: declaración + definición).
    3) Para cada saf_*.cpp se revisa el header hermano
       app/src/main/cpp/include/<base>.h y, junto al .cpp, <base>.h / <base>.hpp.
       Si el .cpp o cualquiera de esos headers contiene "isfinite", el módulo
       se considera guardado y no es hit (saf_runtime.cpp: su guarda vive en
       include/saf_runtime.h, donde está SAFUpdate, el patrón de referencia).
    4) Cada hit incluye "guard_source" ("cpp" | "header" | "none") y
       "header_checked" (ruta relativa del header donde se encontró la
       guarda, o del header existente revisado; null si no existe ningún
       header hermano), además de conservar "file", "functions" y
       "has_isfinite_guard".
    """
    hits = []
    cpp_root = REPO / "app/src/main/cpp"
    func_re = re.compile(r"(?:double|float|void|int)\s+(\w+)\s*\(")
    for path in sorted(cpp_root.rglob("saf_*.cpp")):
        src = path.read_text(errors="ignore")
        funcs = []
        seen = set()
        for name in func_re.findall(src):
            if name not in seen:
                seen.add(name)
                funcs.append(name)
        if not funcs:
            continue

        base = path.stem
        header_candidates = [
            cpp_root / "include" / f"{base}.h",
            path.with_suffix(".h"),
            path.with_suffix(".hpp"),
        ]
        header_checked = None
        guard_source = "none"
        has_isfinite_guard = False
        if "isfinite" in src:
            has_isfinite_guard = True
            guard_source = "cpp"
        else:
            for header in header_candidates:
                if not header.exists():
                    continue
                header_checked = str(header.relative_to(REPO))
                if "isfinite" in header.read_text(errors="ignore"):
                    has_isfinite_guard = True
                    guard_source = "header"
                    break

        if has_isfinite_guard:
            continue

        hits.append({
            "file": str(path.relative_to(REPO)),
            "functions": funcs,
            "has_isfinite_guard": False,
            "guard_source": guard_source,
            "header_checked": header_checked,
        })
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


def scan_host_checks_red():
    """Ejecuta los 6 chequeos de Fase 0 (scripts/check_*.py) y resume cuáles
    están en rojo. Evidencia medida 2026-11-14 (HEAD 8ac65b9d): los 6 fallan
    con exit 1 (~27 s en total: jni_wiring 12 hallazgos, kotlin_wrapper_wiring,
    header_wiring, rt_safety, docs_claims con README 978/978 vs telemetry
    974/974, datasets_deploy con 239 hallazgos SOFA). Antes de este detector
    el --scan era ciego a todos ellos.

    DETECTAR no es ARREGLAR: los fixes de scripts/ quedan fuera del ámbito
    del motor (los propone, no los aplica). Ningún chequeo puede tumbar el
    scan: timeout (120 s), script ausente o error se registran y se sigue.
    """
    checks = (
        "check_jni_wiring",
        "check_kotlin_wrapper_wiring",
        "check_header_wiring",
        "check_rt_safety",
        "check_docs_claims",
        "check_datasets_deploy",
    )
    results = []
    for name in checks:
        script = REPO / "scripts" / f"{name}.py"
        finding_lines = []
        exit_code = None
        ok = False
        error = None
        try:
            if not script.exists():
                raise FileNotFoundError(f"no existe: {script.name}")
            proc = run(["python3", str(script)], cwd=REPO, timeout=120)
            exit_code = proc.returncode
            ok = exit_code == 0
            for line in (proc.stdout or "").splitlines() + (proc.stderr or "").splitlines():
                # Hallazgos: "  - ..." en casi todos; datasets_deploy usa
                # "  [FAIL] ..." (si solo se contaran los "  - ", sus 239
                # hallazgos contarían como 0).
                if line.startswith("  -") or line.startswith("  [FAIL]"):
                    finding_lines.append(line)
            if not ok and not finding_lines:
                error = ((proc.stderr or proc.stdout or f"exit {exit_code}")
                         .strip().splitlines()[0][:120])
        except Exception as exc:  # noqa: BLE001 - ningún chequeo tumba el scan
            error = str(exc).strip().splitlines()[0][:120] if str(exc).strip() else "error"
            exit_code = None
            ok = False
        results.append({
            "check": name,
            "ok": ok,
            "exit_code": exit_code,
            "findings": len(finding_lines),
            "sample": finding_lines[:3],
            "error": error,
        })
    return results


def scan_todo_fixme():
    """Inventario TODO/FIXME en el C++ nativo (git grep, tope 50 entradas).

    El detector reporta; el analista decide (no filtra por antigüedad:
    un TODO viejo puede seguir siendo deuda real). Si git grep no encuentra
    nada (exit 1 de grep) el total es 0, no un error.
    """
    # third_party (googletest vendoreado) excluido: sus TODOs no son deuda
    # de IVANNA y ahogaban el inventario (medido: varios de los 41).
    proc = run(["git", "grep", "-n", "-E", "TODO|FIXME", "--",
                "app/src/main/cpp", ":!app/src/main/cpp/tests/third_party"])
    pattern = re.compile(r"^(?P<file>.*?):(?P<line>\d+):(.*?\b(?P<tag>TODO|FIXME)\b.*)$")
    items = []
    for line in (proc.stdout or "").splitlines():
        m = pattern.match(line)
        if not m:
            continue
        items.append({
            "file": m.group("file"),
            "line": int(m.group("line")),
            "tag": m.group("tag"),
            "text": m.group(3).strip()[:120],
        })
    total = len(items)
    truncated = total > 50
    if truncated:
        items = items[:50]
    return {"items": items, "total": total, "truncated": truncated}


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
            "host_checks_red": scan_host_checks_red(),
            "todo_fixme": scan_todo_fixme(),
        },
    }
    KNOWLEDGE.parent.mkdir(parents=True, exist_ok=True)
    KNOWLEDGE.write_text(json.dumps(knowledge, indent=2, ensure_ascii=False))
    return knowledge


# ── Fase 5: validar una propuesta (MODO SCRATCH, 2026-11-14) ──────────────
# Reescritura redactada por el LLM vía OmniRoute (propuesta de motor):
# el validador anterior aplicaba el parche DIRECTO sobre el árbol de trabajo
# y luego revertía con `git checkout` / `unlink` — ese revert ya falló dos
# veces (ver comentarios de la versión anterior) y compilaba en el
# `build-tests` compartido del repo. Ahora NUNCA se toca el árbol real:
# todo ocurre en un `git worktree` desechable que se elimina al terminar.
# El entregable sigue siendo el diff + validation.json, no el cambio aplicado.
def validate_proposal(pid: str) -> dict:
    pdir = PROPOSALS / pid
    patch = next(pdir.glob("*.patch"), None)
    if patch is None:
        raise SystemExit(f"sin parche en {pdir}")
    patch_text = patch.read_text()
    # Gate 1: el parche no debe tocar archivos prohibidos.
    touched = [l[6:] for l in patch_text.splitlines()
               if l.startswith("+++ b/")]
    bad = [t for t in touched if is_forbidden(t)]
    if bad:
        raise SystemExit(f"GATE: la propuesta toca archivos prohibidos: {bad}")
    # Gate 2: secretos en el texto del parche (misma lista que
    # tools/autorepair/run_fix.py PATRONES_SECRETOS, sin el token del
    # puente, que no aplica a un diff). La key de OmniRoute nunca pasa
    # por aquí: el LLM solo recibe código + diagnóstico.
    secret_patterns = [
        re.compile(r"ghp_[A-Za-z0-9]{20,}"),
        re.compile(r"github_pat_[A-Za-z0-9_]{20,}"),
        re.compile(r"(?i)password\s*=\s*['\"][^'\"]{4,}"),
        re.compile(r"(?i)token\s*=\s*['\"][^'\"]{8,}"),
    ]
    secrets = [pat.pattern for pat in secret_patterns if pat.search(patch_text)]
    if secrets:
        raise SystemExit(f"GATE: la propuesta contiene secretos potenciales: {secrets}")
    # Foto del árbol antes: al final debe ser idéntica (nada tocado).
    repo_status_before = run(["git", "status", "--porcelain"]).stdout.strip()
    result = {"proposal": pid, "patch": patch.name, "files": touched}
    scratch = {"mode": "worktree", "removed": False, "worktree": None}
    result["scratch"] = scratch
    tmpdir = None
    wt = None
    try:
        tmpdir = tempfile.mkdtemp(prefix="ivanna-evo-", dir=str(REPO.parent))
        wt = Path(tmpdir) / "wt"
        scratch["worktree"] = str(wt)
        try:
            run(["git", "worktree", "add", "--detach", str(wt), "HEAD"], check=True)
            # Base pre-parche = el ÁRBOL ACTUAL, no HEAD pelado: el motor
            # tiene trabajo sin commitear (sus propios archivos están
            # modificados respecto a HEAD) y el parche se redactó contra
            # ese árbol. Se vuelca `git diff HEAD` encima del worktree.
            overlay = run(["git", "diff", "HEAD", "--binary"]).stdout
            if overlay.strip():
                overlay_patch = Path(tmpdir) / "dirty.patch"
                overlay_patch.write_text(overlay)
                run(["git", "apply", str(overlay_patch)], check=True, cwd=wt)
        except RuntimeError as e:
            result.update({"applied": False,
                           "error": f"no se pudo preparar el scratch: {e}"})
            return result
        # Archivos sin seguir del motor (tests nuevos, propuestas, estado):
        # el worktree no los tiene y `git diff HEAD` tampoco los cubre.
        # Se copian con `git status -uall` (son ~25 archivos, ~300 KB).
        # Sin esto, la validación en scratch no ve tools/evolution/tests/
        # y las auto-pruebas Python se saltan (medido en la 0003, 1ª pasada).
        # Archivos que el parche CREA (--- /dev/null): no se copian, el
        # parche los crea; copiarlos antes haría fallar el apply ("el
        # archivo ya existe"). Medido en la 0004. OJO: la línea '---'
        # lleva TAB + timestamp, así que NO se compara con string exacto.
        new_in_patch = set()
        _plines = patch_text.splitlines()
        for _i, _l in enumerate(_plines):
            if (_l.startswith("--- /dev/null") and _i + 1 < len(_plines)
                    and _plines[_i + 1].startswith("+++ b/")):
                new_in_patch.add(_plines[_i + 1][6:])
        untracked = run(["git", "status", "--porcelain", "-uall", "--",
                         "tools/evolution", "tools/autorepair"]).stdout
        for line in untracked.splitlines():
            if not line.startswith("??"):
                continue
            rel = line[3:].strip()
            if rel in new_in_patch:
                continue
            src = REPO / rel
            dst = wt / rel
            if src.is_dir():
                shutil.copytree(src, dst,
                                ignore=shutil.ignore_patterns("__pycache__"),
                                dirs_exist_ok=True)
            elif src.exists():
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
        # Base pre-parche para archivos NO seguidos: tools/evolution/ y
        # tools/autorepair/ son untracked en HEAD, así que el worktree no
        # los contiene; se copian del árbol real (contenido actual = base).
        for t in touched:
            if t in new_in_patch:
                continue  # lo crea el parche; copiar la versión del árbol
                # haría fallar el apply ("already exists in working
                # directory"). Medido en la 0004, 3ª pasada.
            wt_path = wt / t
            tracked = run(["git", "ls-files", "--", t]).stdout.strip()
            if tracked:
                continue
            src = REPO / t
            if src.exists():
                if src.is_dir():
                    result.update({"applied": False,
                                   "error": f"base desconocida: {t} es un directorio"})
                    return result
                wt_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, wt_path)
            elif t not in new_in_patch:
                # No está en HEAD, no está en el árbol y el parche no lo
                # crea: no hay base conocida contra la que aplicar.
                result.update({"applied": False,
                               "error": f"base desconocida: {t} no existe ni en HEAD ni en el árbol"})
                return result
        # Gate 3: el parche aplica limpio DENTRO del scratch, no en el repo.
        r = run(["git", "apply", "--check", str(patch)], cwd=wt)
        if r.returncode != 0:
            result.update({"applied": False, "error": r.stderr})
            return result
        run(["git", "apply", str(patch)], check=True, cwd=wt)
        result["applied"] = True
        cmake = os.environ.get("IVANNA_CMAKE", str(Path.home() / ".local/bin/cmake"))
        test_bins = sorted(set(re.findall(r"add_executable\((\w+)", patch_text)))
        built, test_runs = [], {}
        cpp_touched = any(
            t.endswith((".cpp", ".c", ".h", ".hpp"))
            or t == "app/src/main/cpp/tests/CMakeLists.txt"
            for t in touched)
        if cpp_touched:
            # Parche C++: configurar + compilar + suite completa en scratch.
            run([cmake, "-S", "app/src/main/cpp/tests", "-B", "build-tests"],
                check=True, cwd=wt)
            for t in test_bins:
                b = run([cmake, "--build", "build-tests", "--target", t, "-j4"], cwd=wt)
                built.append({"target": t, "ok": b.returncode == 0,
                              "tail": (b.stdout + b.stderr)[-500:]})
                if b.returncode == 0:
                    tr = run([f"./{t}"], cwd=wt / "build-tests")
                    test_runs[t] = {"passed": tr.returncode == 0,
                                    "tail": tr.stdout[-500:]}
            run([cmake, "--build", "build-tests", "-j4"], check=True, cwd=wt)
            ct = run(["ctest", "--output-on-failure"], cwd=wt / "build-tests")
            result["full_suite"] = {"passed": ct.returncode == 0,
                                    "tail": ct.stdout[-800:]}
        else:
            # Parche sin C++: CTest no tiene nada que compilar; se verifican
            # las auto-pruebas del motor dentro del scratch y se deja
            # constancia explícita de que la suite C++ se saltó (y por qué).
            result["full_suite"] = {"skipped": True,
                                    "reason": "el parche no toca C++"}
            python_checks = {}
            tests_dir = wt / "tools" / "evolution" / "tests"
            if tests_dir.exists():
                ut = run(["python3", "-m", "unittest", "discover",
                          "-s", "tools/evolution/tests", "-v"], cwd=wt)
                python_checks["unittest"] = {
                    "passed": ut.returncode == 0,
                    "tail": (ut.stdout + ut.stderr)[-800:]}
            else:
                python_checks["unittest"] = {
                    "skipped": True, "reason": "no existe tools/evolution/tests"}
            scan = run(["python3", "tools/evolution/evolution_engine.py", "--scan"],
                       cwd=wt, timeout=300)
            python_checks["scan"] = {
                "passed": scan.returncode == 0,
                "tail": (scan.stdout + scan.stderr)[-800:],
                "knowledge_json": (wt / "tools" / "evolution" / "knowledge"
                                   / "knowledge.json").exists(),
            }
            result["python_checks"] = python_checks
        result["built"] = built
        result["new_tests"] = test_runs
    finally:
        # Eliminar el scratch SIEMPRE y verificar que el árbol quedó intacto.
        if wt is not None and wt.exists():
            try:
                run(["git", "worktree", "remove", "--force", str(wt)], check=True)
                scratch["removed"] = True
            except Exception:
                if tmpdir is not None:
                    shutil.rmtree(tmpdir, ignore_errors=True)
                scratch["removed"] = False
            finally:
                run(["git", "worktree", "prune"], check=False)
        # El padre tmpdir (con dirty.patch) no lo borra `worktree remove`:
        # sin esto cada validación deja un directorio huérfano junto al repo.
        if tmpdir is not None:
            shutil.rmtree(tmpdir, ignore_errors=True)
        repo_status_after = run(["git", "status", "--porcelain"]).stdout.strip()
        result["tree_untouched"] = (repo_status_before == repo_status_after)
        # El registro se escribe SIEMPRE (también en los retornos tempranos
        # de base desconocida o apply --check fallido): sin él, un fallo
        # de validación no dejaría evidencia.
        (pdir / "validation.json").write_text(
            json.dumps(result, indent=2, ensure_ascii=False))
    return result


# ── Fase 4: redacción por LLM (OmniRoute) ───────────────────────────────────
# El LLM recibe SOLO código fuente relevante + diagnóstico (autorizado por
# Esse 2026-11-14). NUNCA credenciales ni secretos. La redacción no aplica
# nada: devuelve un diff que luego se valida en scratch (Fase 5).
import difflib


def _llm():
    """Import perezoso de llm_client (mismo directorio). Así --scan funciona
    aunque el gateway esté caído: solo --draft/--evolve necesitan el LLM."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "ivanna_llm_client", EVO / "llm_client.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def is_draft_forbidden(rel: str) -> bool:
    return (rel.startswith(DRAFT_FORBIDDEN)
            or rel.endswith(FORBIDDEN_SUFFIX)
            or "SECRET" in rel.upper()
            or "PAT" in rel)


def _code_blocks_to_patch(text: str):
    """Plan B si el LLM no devuelve un ```diff: bloques ```<lang> cuya
    primera línea es `# FILE: <ruta>` con el contenido COMPLETO del archivo.
    Reconstruye el unified diff con difflib contra el árbol actual.
    Devuelve el patch o None si no hay bloques con FILE."""
    import re as _re
    blocks = _re.findall(r"```[^\n]*\n(.*?)```", text, _re.S)
    files = {}
    for b in blocks:
        lines = b.strip("\n").splitlines()
        if not lines or not lines[0].startswith("# FILE:"):
            continue
        files[lines[0][len("# FILE:"):].strip()] = "\n".join(lines[1:]) + "\n"
    if not files:
        return None
    out = []
    for path, new in sorted(files.items()):
        old_path = REPO / path
        old = old_path.read_text(errors="ignore") if old_path.exists() else ""
        if old == new:
            continue
        fromfile = f"a/{path}" if old else "/dev/null"
        out.extend(difflib.unified_diff(
            old.splitlines(keepends=True), new.splitlines(keepends=True),
            fromfile=fromfile, tofile=f"b/{path}"))
    return "".join(out) or None


def draft_patch(finding: dict) -> dict:
    """Redacta una reescritura magistral con el LLM para un hallazgo.

    finding: {"kind", "title", "evidence", "files": [rutas relativas]}.
    Devuelve {"patch", "raw_text", "model", "latency_s", "files"}.
    Lanza SystemExit si el hallazgo toca algo prohibido para drafts o si
    el LLM no devuelve un diff utilizable tras los dos intentos.
    """
    files = finding.get("files") or []
    bad = [f for f in files if is_draft_forbidden(f)]
    if bad:
        raise SystemExit(f"GATE draft: el hallazgo toca archivos prohibidos "
                         f"para drafts: {bad}")
    llm = _llm()
    ok, health_ms, _models = llm.health()
    if not ok:
        raise SystemExit(f"gateway OmniRoute no disponible (health {health_ms:.0f} ms)")
    # Solo el código relevante, recortado: un archivo gigante no se manda entero.
    context_parts = []
    for f in files:
        p = REPO / f
        if not p.exists() or p.is_dir():
            context_parts.append(f"### {f}\n(no existe en el árbol: se creará)\n")
            continue
        src = p.read_text(errors="ignore")
        if len(src) > 12000:
            src = src[:12000] + "\n[... recortado: el archivo supera 12000 caracteres ...]\n"
        context_parts.append(f"### {f}\n```\n{src}\n```\n")
    system = (
        "Eres el redactor de reescrituras del motor de auto-evolución de IVANNA "
        "(C++, Kotlin, Python). Estándar REFINADA MAGISTRAL: reescritura completa "
        "y refinada, nunca un parche mínimo a medias, nunca básica. Fail-closed: "
        "ante entradas no válidas, el código nuevo debe degradar a un estado "
        "seguro sin tocar el estado. No toques archivos fuera de los indicados. "
        "No inventes APIs, métricas ni resultados de tests. Devuelve SOLO un "
        "bloque ```diff con el unified diff completo (rutas a/<ruta> y b/<ruta>), "
        "sin prosa fuera del bloque.")
    user = (
        f"HALLAZGO ({finding.get('kind', '?')}): {finding.get('title', '')}\n"
        f"Evidencia medida:\n{finding.get('evidence', '')}\n\n"
        f"CÓDIGO RELEVANTE:\n{''.join(context_parts)}\n"
        "REESCRITURA EXIGIDA: corrige la causa raíz de forma completa y refinada "
        "(no un parche mínimo), con test de regresión si el cambio toca código "
        "con tests (C++: añadir/actualizar el test en app/src/main/cpp/tests; "
        "Python del motor: test en tools/evolution/tests). Si el hallazgo es "
        "solo de detección (falso positivo del detector), corrige el detector "
        "y añade su test unitario. Devuelve SOLO el bloque ```diff.")
    import time
    t0 = time.monotonic()
    text, patch = llm.draft_rewrite(system, user, base_dir=REPO)
    latency = time.monotonic() - t0
    if patch is None:
        # Plan B: pedir el contenido completo por archivo y reconstruir el
        # diff con difflib (el formato ```diff falló dos veces con el pool
        # 'ivanna-free' devolviendo un parche vacío, 2026-11-14).
        t1 = time.monotonic()
        text2 = llm.chat(
            [{"role": "system", "content":
              "Eres un programador senior. Devuelve SOLO bloques de código, "
              "uno por archivo modificado, en formato ```python (o cpp) cuya "
              "primera línea sea `# FILE: <ruta relativa>` seguida del "
              "contenido COMPLETO del archivo. Sin prosa fuera de los bloques."},
             {"role": "user", "content":
              "El formato diff falló. Devuelve los archivos completos con la "
              "misma reescritura magistral pedida antes:\n" + user}],
            max_tokens=8192, read_timeout_s=240)
        latency += time.monotonic() - t1
        patch = _code_blocks_to_patch(text2)
        text = text + "\n\n[plan B: bloques de código]\n" + text2
    if patch is None:
        raise SystemExit("el LLM no devolvió un diff utilizable (ni ```diff "
                         "ni bloques # FILE:); no se inventa un parche")
    touched = [l[6:] for l in patch.splitlines() if l.startswith("+++ b/")]
    bad = [t for t in touched if is_draft_forbidden(t)]
    if bad:
        raise SystemExit(f"GATE draft: el parche del LLM toca archivos "
                         f"prohibidos para drafts: {bad}")
    return {"patch": patch, "raw_text": text, "model": llm._model(),
            "latency_s": round(latency, 1), "files": touched}


def _escribir_propuesta(pid: str, patch: str, draft: dict, finding: dict) -> Path:
    pdir = PROPOSALS / pid
    pdir.mkdir(parents=True, exist_ok=True)
    (pdir / f"{pid}.patch").write_text(patch)
    (pdir / "DRAFT.md").write_text(
        f"# Draft {pid} (LLM vía OmniRoute)\n\n"
        f"- **Modelo:** {draft['model']} (gateway http://localhost:20128/v1)\n"
        f"- **Latencia de redacción:** {draft['latency_s']} s\n"
        f"- **Hallazgo:** {finding.get('kind', '?')}: {finding.get('title', '')}\n"
        f"- **Archivos del parche:** {', '.join(draft['files'])}\n\n"
        "## Respuesta completa del LLM\n\n"
        "```\n" + draft["raw_text"][:12000] + "\n```\n")
    return pdir


def cmd_evolve(pid: str, finding: dict) -> dict:
    """Ciclo detectar → reescribir → verificar. NUNCA aplica al árbol ni
    publica: deja la propuesta + validation.json + REPORT.md listos."""
    draft = draft_patch(finding)
    pdir = _escribir_propuesta(pid, draft["patch"], draft, finding)
    result = validate_proposal(pid)
    passed = bool(result.get("applied"))
    if "full_suite" in result:
        fs = result["full_suite"]
        passed = passed and (fs.get("passed") is True or fs.get("skipped") is True)
    if "python_checks" in result:
        pc = result["python_checks"]
        passed = passed and all(
            v.get("passed") is True or v.get("skipped") is True
            for v in pc.values())
    verdict = "VERDE — lista para revisión/publicación" if passed else \
        "ROJO — NO se publica"
    (pdir / "REPORT.md").write_text(
        f"# Propuesta {pid} — {finding.get('title', '')}\n\n"
        f"- **Hallazgo:** {finding.get('kind', '?')}\n"
        f"- **Evidencia:** {finding.get('evidence', '')}\n"
        f"- **Draft LLM:** modelo {draft['model']}, {draft['latency_s']} s\n"
        f"- **Archivos:** {', '.join(draft['files'])}\n"
        f"- **Veredicto:** {verdict}\n"
        f"- **Árbol intacto:** {result.get('tree_untouched')} "
        f"(scratch {result.get('scratch', {}).get('mode')}, "
        f"eliminado: {result.get('scratch', {}).get('removed')})\n\n"
        "## Validación (ver validation.json)\n\n"
        f"```json\n{json.dumps(result, indent=2, ensure_ascii=False)[:4000]}\n```\n")
    print(f"propuesta {pid}: {verdict}")
    print(f"  parche: {pdir / (pid + '.patch')}")
    print(f"  validación: {pdir / 'validation.json'}")
    print(f"  reporte: {pdir / 'REPORT.md'}")
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--proposal")
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--draft")
    ap.add_argument("--evolve")
    ap.add_argument("--finding", help="hallazgo JSON: {kind, title, evidence, files[]}")
    args = ap.parse_args()
    if args.scan or (not args.proposal and not args.validate
                     and not args.draft and not args.evolve):
        k = build_knowledge()
        print(json.dumps(k, indent=2, ensure_ascii=False))
    if args.proposal and args.validate:
        print(json.dumps(validate_proposal(args.proposal), indent=2, ensure_ascii=False))
    if args.draft or args.evolve:
        if not args.finding:
            raise SystemExit("--draft/--evolve requieren --finding '<json>'")
        finding = json.loads(args.finding)
        pid = args.draft or args.evolve
        if args.draft and not args.evolve:
            draft = draft_patch(finding)
            pdir = _escribir_propuesta(pid, draft["patch"], draft, finding)
            print(f"draft {pid} escrito (SIN validar): {pdir / (pid + '.patch')}")
            print("valídalo con: --proposal " + pid + " --validate")
        else:
            cmd_evolve(pid, finding)


if __name__ == "__main__":
    main()
