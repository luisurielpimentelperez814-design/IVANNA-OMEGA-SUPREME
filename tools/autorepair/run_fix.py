#!/usr/bin/env python3
"""
Orquestador de fix de IVANNA (tools/autorepair/run_fix.py).

Toma un incidente de la cola (creado por watcher.py) y lo lleva por el flujo:
  next      toma el incidente más antiguo de queue/, lo mueve a processing/
            y genera el prompt quirúrgico en prompts/<id>.md para el
            subagente de fix (el parche lo escribe un subagente de Muse,
            no este script: el script no puede lanzar subagentes).
  draft     el LLM (vía el motor de evolución + OmniRoute) redacta el
            parche del incidente en processing/<id>.patch. NO aplica nada:
            el diff se revisa y se aplica a mano o por subagente; después
            validate + gates como siempre. Es la vía sin subagente.
  validate  compila y corre la suite CTest completa en build-tests.
            Escribe state/last_validation.json. El gate es 100% de tests.
  gates     verifica que el cambio del working tree sea publicable:
            sin archivos prohibidos, cambio mínimo, sin secretos.
  publish   SOLO si validate pasó y gates pasa: bump de versión,
            CHANGELOG, commit y push con [publish-release] (mecanismo PAT
            vía stdin, URL remota limpia después). Sin --execute es dry-run.
  report    escribe el diagnóstico en reports/<id>.md (no se publica) y
            archiva el incidente. Para problemas nuevos, riesgosos o que
            el fix no pudo resolver.
  pending   lista incidentes pendientes, en proceso y reportes sin entregar.
  ack       marca un reporte como entregado a Esse.

Flujo completo (documentado, cada paso es un comando independiente):
  1. python3 run_fix.py next
  2. (el agente principal lanza un subagente con prompts/<id>.md; el
     subagente parchea, corre CTest y rellena el campo "fix" del JSON
     de processing/ con summary/files/ctest)
  3. python3 run_fix.py validate
  4. python3 run_fix.py gates
  5. python3 run_fix.py publish --execute   (o --dry-run para ver el plan)
     -- o, si no se puede publicar:
  5b. python3 run_fix.py report --incident processing/<id>.json --reason "..."

Límites de seguridad (DecisionAuthority, NO negociables):
  - Nunca se publica sin CTest al 100%.
  - Nunca se tocan los archivos prohibidos (ver FORBIDDEN, importado del
    motor de evolución tools/evolution/evolution_engine.py).
  - Nunca se desactiva IntegrityGate, nunca se tocan credenciales/PAT
    (el PAT solo transita por stdin/memoria, nunca se escribe en disco
    ni se imprime), nunca se compra nada.
  - El cambio debe ser mínimo (<= 6 archivos, <= 300 líneas).
  - Sin IVANNA_ALLOW_PUBLISH=1, publish es dry-run aunque se pida --execute.
  - Nunca force-push; si el push es rechazado (el bot publicó update.json),
    se detiene y se imprimen los pasos de rebase.
"""
import argparse
import getpass
import importlib.util
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

BASE = Path(__file__).resolve().parent
REPO = BASE.parents[1]
QUEUE_DIR = BASE / "queue"
PROCESSING_DIR = BASE / "processing"
ARCHIVE_DIR = BASE / "archive"
REPORTS_DIR = BASE / "reports"
PROMPTS_DIR = BASE / "prompts"
STATE_DIR = BASE / "state"
VALIDATION_FILE = STATE_DIR / "last_validation.json"

MAX_ARCHIVOS_CAMBIO = 6
MAX_LINEAS_CAMBIO = 300
VALIDATION_FRESCO_MIN = 60

PATRONES_SECRETOS = (
    re.compile(r"ghp_[A-Za-z0-9]{20,}"),
    re.compile(r"github_pat_[A-Za-z0-9_]{20,}"),
    re.compile(r"IVANNA_DIAGNOSTIC_TOKEN\s*=\s*\S+"),
    re.compile(r"(?i)password\s*=\s*['\"][^'\"]{4,}"),
    re.compile(r"(?i)token\s*=\s*['\"][^'\"]{8,}"),
)

# --- Archivos prohibidos: fuente de verdad en el motor de evolución --------
try:
    _spec = importlib.util.spec_from_file_location(
        "evolution_engine", REPO / "tools" / "evolution" / "evolution_engine.py")
    _evo = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_evo)
    FORBIDDEN = _evo.FORBIDDEN
    FORBIDDEN_SUFFIX = _evo.FORBIDDEN_SUFFIX
    _is_forbidden = _evo.is_forbidden
except Exception:
    # Fallback si el motor no está disponible: misma lista, duplicada aquí
    # con el aviso de que la fuente de verdad es evolution_engine.py.
    FORBIDDEN = (
        "app/build.gradle.kts",
        "app/src/main/cpp/CMakeLists.txt",
        "app/src/main/java/com/ivanna/omega/security/",
        ".github/workflows/",
        "tools/security/",
    )
    FORBIDDEN_SUFFIX = (".keystore", ".jks", ".pem", ".key")

    def _is_forbidden(rel):
        return (rel.startswith(FORBIDDEN)
                or rel.endswith(FORBIDDEN_SUFFIX)
                or "SECRET" in rel.upper()
                or "PAT" in rel)


def _es_prohibido(rel):
    return _is_forbidden(rel)


# --- Conocimiento por tipo de incidente -------------------------------------
# auto_fixable=False significa: NO se intenta parche automático; se escribe
# reporte para Esse (el fix vive fuera del repo, o requiere decisión humana).
CONOCIMIENTO = {
    "route-b-no-acopla": {
        "auto_fixable": True,
        "archivos": [
            "app/src/main/java/com/ivanna/omega/audio/IvannaGlobalEffectManager.kt",
            "app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt",
            "app/src/main/cpp/omega_effect.cpp",
            "app/src/main/cpp/daemon/command_server.cpp",
        ],
        "pistas": [
            "El beacon /data/local/tmp/omega_effect_beacon es el ÚNICO testigo válido "
            "(frames u64 LE bytes 8-15, enabled_count u32 bytes 4-7, magic 'OEBF').",
            "El daemon descubre la sesión real (dumpsys cada 3 s) y la publica en "
            "snapshot ABI v11 + GET_STATUS; la app acopla con tryAttachDiscoveredSession().",
            "La 380 eliminó el fallback a omega global en sesión 0: NO recrearlo.",
            "La app NO puede leer /data/adb/ivanna_omega (solo-root): no implementar "
            "lectura del snapshot desde la app.",
            "Si el beacon sigue en 0 tras el fix, el camino alternativo es el XML "
            "(postprocess en audio_effects.xml), que requiere cambiar customize.sh "
            "y reiniciar audioserver con confirmación de Esse.",
        ],
        "prohibido": ["No tocar Ruta A (el problema es Ruta B).",
                      "No crear omega en sesión 0.",
                      "No leer el snapshot desde la app."],
    },
    "asrc-deriva": {
        "auto_fixable": False,
        "por_que": ("El fix vive en hal_integration/moto_g85/pal_preload_probe/ "
                    "(write-stage ASRC por LD_PRELOAD, untracked, de otro agente). "
                    "La automatización NO toca ese directorio."),
        "archivos": ["hal_integration/moto_g85/pal_preload_probe/ (NO TOCAR)"],
        "pistas": [
            "El dump /data/vendor/ivanna_audio/pal_asrc_stage.txt es la autoridad "
            "(drift_ppm, sat, kalman_drift, fir_taps).",
            "Si sat>0 o el drift se dispara, el rollback es la v4 o el kill switch "
            "pal_asrc.off; eso lo decide Esse, no el automatismo.",
        ],
        "prohibido": ["No tocar hal_integration/."],
    },
    "dsp-sobrecarga": {
        "auto_fixable": True,
        "archivos": [
            "app/src/main/cpp/IvannaFusionCore.cpp",
            "app/src/main/cpp/omni_controls_bridge.cpp",
        ],
        "pistas": [
            "El shed térmico / gobernador por deuda existe: bajar amounts antes "
            "que tocar la cadena.",
            "Cada etapa apagada es bit-exacta; un bypass no cambia el sonido.",
            "Medir CPU por bloque antes y después; el G85 tiene 1.5 CPUs "
            "compartidas en la VM de build, pero el teléfono es un A78 real.",
        ],
        "prohibido": ["No apagar ORBE/HOLOS/Phi por defecto sin orden de Esse."],
    },
    "termico": {
        "auto_fixable": False,
        "por_que": ("El throttling es físico (temperatura del SoC). No hay parche "
                    "de código seguro que lo resuelva; requiere bajar la carga "
                    "o enfriar el teléfono. Se reporta a Esse."),
        "archivos": [],
        "pistas": ["El shed térmico ya baja amounts automáticamente; si sigue "
                   "throttleando, la carga es demasiada para el G85."],
        "prohibido": ["No desactivar el shed térmico."],
    },
    "daemon-desconectado": {
        "auto_fixable": True,
        "archivos": [
            "magisk_module/service.sh",
            "magisk_module/supervisor.sh",
            "app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt",
        ],
        "pistas": [
            "El supervisor relanza el daemon si muere; se detiene tras 8 "
            "relanzamientos en 1 h o con .safe_mode / disable / remove.",
            "5 caídas seguidas crean .safe_mode: revisar daemon.log antes de "
            "tocar el supervisor.",
            "El socket es @omega_daemon_socket (abstracto); la autorización "
            "usa el uid de /data/user/*/com.ivanna.omega.",
        ],
        "prohibido": ["No tocar el binario del daemon sin rebuild por CI."],
    },
    "memoria-alta": {
        "auto_fixable": False,
        "por_que": ("Requiere investigación (qué proceso crece, si es fuga o "
                    "pico legítimo). No hay fix mínimo obvio; se reporta."),
        "archivos": [],
        "pistas": ["dumpsys meminfo + el log del daemon antes de proponer nada."],
        "prohibido": [],
    },
    "latencia-degradada": {
        "auto_fixable": True,
        "archivos": [
            "app/src/main/cpp/IvannaFusionCore.cpp",
        ],
        "pistas": [
            "Revisar tamaño de anillo / bloques y el decimado de NPE analyze "
            "bajo carga antes de tocar la cadena.",
        ],
        "prohibido": ["No cambiar el tamaño de bloque de 128 muestras."],
    },
}

REGLAS_ESSE = """Reglas de Esse (NO negociables):
- Cambios mínimos y fail-closed. No se refactoriza nada más.
- No tocar: Ruta A si el problema es Ruta B (y viceversa), hal_integration/
  (ASRC, de otro agente), app/src/main/java/com/ivanna/omega/security/
  (IntegrityGate / anti-piratería), app/build.gradle.kts,
  app/src/main/cpp/CMakeLists.txt, .github/workflows/, tools/security/,
  ni ningún archivo con credenciales, claves o secretos.
- No afirmar "funciona en el teléfono" sin logcat + beacon + escucha.
- NO hacer commit ni push (eso lo hace el orquestador run_fix.py).
- El gate es CTest al 100%: compila con ~/.local/bin/cmake en build-tests
  y corre la suite completa. Si un test falla, el fix NO se publica.
- Al terminar, actualiza el campo "fix" del JSON de processing/ con:
  {"summary": "<una línea, estilo commit>", "files": ["<archivos tocados>"],
   "ctest": "<N/N tests>"} y avisa al orquestador."""


# --- Utilidades -------------------------------------------------------------

def _ahora_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _run(cmd, cwd=REPO, check=False, input_text=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, input=input_text)
    if check and r.returncode != 0:
        raise RuntimeError(f"falló: {' '.join(map(str, cmd))}\n{r.stdout}\n{r.stderr}")
    return r


def _cargar_json(ruta):
    return json.loads(Path(ruta).read_text())


def _cambiar_estado_repo():
    return _run(["git", "status", "--porcelain"]).stdout.strip()


def _archivos_cambiados(paths=None):
    """Archivos con cambios. Si se pasa una lista de paths, SOLO se evalúan
    esos (el fix que se publica); sin lista, todo el árbol excepto
    build-tests. Esto importa porque el árbol suele tener trabajo ajeno sin
    commitear (p. ej. hal_integration/, untracked y de otro agente) que NO
    forma parte del fix y no debe tumbar el gate."""
    if paths is not None:
        cambiados = set()
        for p in paths:
            status = _run(["git", "status", "--porcelain", "--", p]).stdout.strip()
            if status:
                cambiados.add(p)
        return sorted(cambiados)
    cambiados = set()
    for args in (["git", "diff", "--name-only"],
                 ["git", "diff", "--cached", "--name-only"]):
        out = _run(args).stdout.strip()
        cambiados.update(l for l in out.splitlines() if l)
    status = _run(["git", "status", "--porcelain", "-uall"]).stdout
    for linea in status.splitlines():
        if linea.startswith("??"):
            cambiados.add(linea[3:].strip())
    return sorted(p for p in cambiados if not p.startswith("build-tests"))


def _numstat(paths=None):
    """(n_archivos, n_lineas) del cambio. Con paths, solo esos archivos."""
    total_archivos, total_lineas = 0, 0
    cmd = ["git", "diff", "--numstat"]
    if paths is not None:
        cmd += ["--"] + list(paths)
    out = _run(cmd).stdout
    for linea in out.splitlines():
        partes = linea.split()
        if len(partes) >= 3 and partes[0].isdigit() and partes[1].isdigit():
            total_archivos += 1
            total_lineas += int(partes[0]) + int(partes[1])
    # Archivos nuevos (untracked) no salen en numstat: contar sus líneas.
    status = _run(["git", "status", "--porcelain", "-uall"]).stdout
    for linea in status.splitlines():
        if linea.startswith("??"):
            p = linea[3:].strip()
            if paths is not None and p not in paths:
                continue
            fp = REPO / p
            if fp.is_file():
                total_archivos += 1
                try:
                    total_lineas += len(fp.read_text(errors="ignore").splitlines())
                except OSError:
                    pass
    return total_archivos, total_lineas


# --- Subcomando: next -------------------------------------------------------

def _generar_prompt(incidente):
    tipo = incidente["tipo"]
    con = CONOCIMIENTO.get(tipo, {})
    head = _run(["git", "log", "--oneline", "-3"]).stdout.strip()
    status = _cambiar_estado_repo() or "(árbol limpio)"
    auto = con.get("auto_fixable")
    if auto is False:
        nota = (f"⚠️ Este tipo NO es auto-arreglable: {con.get('por_que', '')}\n"
                "No escribas parche. Escribe el diagnóstico en el reporte y "
                "devuelve el incidente sin tocar código.\n")
    else:
        nota = ""
    archivos = "\n".join(f"- {a}" for a in con.get("archivos", [])) or "- (por determinar por el diagnóstico)"
    pistas = "\n".join(f"- {p}" for p in con.get("pistas", []))
    prohibido = "\n".join(f"- {p}" for p in con.get("prohibido", []))
    return f"""# PROMPT QUIRÚRGICO — Auto-fix IVANNA (generado por tools/autorepair/run_fix.py)

## Incidente
- ID: {incidente["incident_id"]}
- Tipo: {tipo} (severidad {incidente["severidad"]})
- Razón: {incidente["razon"]}
- Detectado: {incidente["detected_at"]} (snapshot sequence {incidente.get("sequence")})
- Snapshot (recortado a métricas/agente):
```json
{json.dumps({"metrics": incidente["snapshot"].get("metrics"),
             "agent": incidente["snapshot"].get("agent"),
             "routeB": incidente["snapshot"].get("routeB")}, indent=2, ensure_ascii=False)}
```

{nota}## Contexto del repo
- Repo: ~/workspace/ivanna, rama main. HEAD:
```
{head}
```
- Estado del árbol al generar el prompt:
```
{status}
```

## Archivos probables
{archivos}

## Pistas (verificadas en sesiones anteriores)
{pistas}

## Prohibido en este fix
{prohibido}

{REGLAS_ESSE}

## Objetivo
Corregir la causa raíz del incidente con el cambio más pequeño posible,
fail-closed. Si el diagnóstico muestra que el problema NO es auto-arreglable
o que el fix sería mayor que un cambio mínimo, NO parchees: escribe el
diagnóstico en el JSON de processing/ (campo "fix": null y "no_fix_reason")
y devuelve el control.
"""


def cmd_next(args):
    pendientes = sorted(QUEUE_DIR.glob("*.json"))
    if not pendientes:
        print("cola vacía: no hay incidentes pendientes")
        return 0
    ruta = pendientes[0]
    incidente = _cargar_json(ruta)
    tipo = incidente["tipo"]
    if not CONOCIMIENTO.get(tipo, {}).get("auto_fixable", False):
        print(f"incidente {incidente['incident_id']} tipo '{tipo}': NO auto-arreglable")
        print(f"motivo: {CONOCIMIENTO[tipo].get('por_que', '')}")
        print("usa: run_fix.py report --incident " + str(ruta) + " --reason <motivo>")
        return 2
    PROCESSING_DIR.mkdir(parents=True, exist_ok=True)
    destino = PROCESSING_DIR / ruta.name
    ruta.rename(destino)  # atómico: otro proceso no lo vuelve a tomar
    PROMPTS_DIR.mkdir(parents=True, exist_ok=True)
    prompt_ruta = PROMPTS_DIR / f"{incidente['incident_id']}.md"
    prompt_ruta.write_text(_generar_prompt(incidente))
    print(f"incidente en proceso: {destino}")
    print(f"prompt quirúrgico: {prompt_ruta}")
    print("SIGUIENTE PASO: el agente principal lanza un subagente con ese prompt.")
    print("Cuando el subagente termine, ejecuta: run_fix.py validate && run_fix.py gates")
    return 0


# --- Subcomando: validate ---------------------------------------------------

def _parsear_ctest(salida):
    m = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", salida)
    if not m:
        return None
    pct, failed, total = int(m.group(1)), int(m.group(2)), int(m.group(3))
    return {"passed_pct": pct, "failed": failed, "total": total,
            "passed": pct == 100 and failed == 0 and total > 0}


def cmd_validate(args):
    cmake = os.environ.get("IVANNA_CMAKE", str(Path.home() / ".local/bin/cmake"))
    build_dir = REPO / "build-tests"
    lock = build_dir / ".autorepair-ctest.lock"
    try:
        lock.parent.mkdir(parents=True, exist_ok=True)
        lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        print("otro CTest de autorepair ya está corriendo (lock presente); aborta")
        return 2
    try:
        os.write(lock_fd, str(os.getpid()).encode())
        print("configurando cmake...")
        r = _run([cmake, "-S", "app/src/main/cpp/tests", "-B", "build-tests"])
        if r.returncode != 0:
            print("cmake configure falló:\n" + (r.stdout + r.stderr)[-2000:])
            return 1
        print("compilando...")
        r = _run([cmake, "--build", "build-tests", "-j4"])
        if r.returncode != 0:
            print("compilación falló:\n" + (r.stdout + r.stderr)[-2000:])
            return 1
        print("corriendo CTest (suite completa)...")
        r = _run(["ctest", "--output-on-failure"], cwd=build_dir)
        resumen = _parsear_ctest(r.stdout)
        alcance = None
        if getattr(args, "incident", None):
            try:
                alcance = _alcance_fix(_cargar_json(args.incident))
            except (FileNotFoundError, json.JSONDecodeError):
                alcance = None
        registro = {
            "at": _ahora_iso(),
            "head": _run(["git", "rev-parse", "HEAD"]).stdout.strip(),
            "diff_stat": _diff_stat(alcance),
            "ctest": resumen,
            "passed": bool(resumen and resumen["passed"]),
            "tail": (r.stdout + r.stderr)[-1500:],
        }
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        VALIDATION_FILE.write_text(json.dumps(registro, indent=2, ensure_ascii=False))
        if resumen:
            print(f"CTest: {resumen['passed_pct']}% pasados, {resumen['failed']} fallidos "
                  f"de {resumen['total']}")
        else:
            print("no se pudo parsear la salida de CTest")
        return 0 if registro["passed"] else 1
    finally:
        os.close(lock_fd)
        try:
            lock.unlink()
        except FileNotFoundError:
            pass


def _alcance_fix(incidente):
    """Archivos que forman el fix: fix.files + archivos de versión."""
    fix = (incidente or {}).get("fix") or {}
    return sorted(set(fix.get("files", [])) | {
        "version.properties",
        "magisk_module/module.prop",
        "magisk_module/update.json",
        "CHANGELOG.md",
    })


def _diff_stat(alcance=None):
    cmd = ["git", "diff", "--stat"]
    if alcance:
        cmd += ["--"] + list(alcance)
    return _run(cmd).stdout.strip()


def _validacion_fresca(alcance=None):
    """True si hay un registro de validación pasado, de menos de 60 min,
    y el diff (del alcance del fix, si se da) no cambió desde entonces."""
    try:
        reg = _cargar_json(VALIDATION_FILE)
    except (FileNotFoundError, json.JSONDecodeError):
        return False
    if not reg.get("passed"):
        return False
    try:
        at = datetime.fromisoformat(reg["at"].replace("Z", "+00:00"))
    except (KeyError, ValueError):
        return False
    minutos = (datetime.now(timezone.utc) - at).total_seconds() / 60.0
    if minutos > VALIDATION_FRESCO_MIN:
        return False
    if reg.get("diff_stat") != _diff_stat(alcance):
        return False
    return True


# --- Subcomando: gates ------------------------------------------------------

def cmd_gates(args):
    """Verifica que el cambio sea publicable. Con --incident, el gate se
    evalúa SOLO sobre los archivos del fix (fix.files) + los archivos de
    versión; sin --incident, sobre todo el árbol (y el trabajo ajeno sin
    commitear puede tumbarlo: ese es el comportamiento conservador)."""
    alcance = None
    if getattr(args, "incident", None):
        incidente = _cargar_json(args.incident)
        fix = incidente.get("fix") or {}
        alcance = sorted(set(fix.get("files", [])) | {
            "version.properties",
            "magisk_module/module.prop",
            "magisk_module/update.json",
            "CHANGELOG.md",
        })
    cambiados = _archivos_cambiados(alcance)
    n_arch, n_lin = _numstat(alcance)
    problemas = []
    if not cambiados:
        problemas.append("no hay cambios en el alcance evaluado: no hay nada que publicar")
    prohibidos = [f for f in cambiados if _es_prohibido(f)]
    if prohibidos:
        problemas.append(f"archivos prohibidos en el cambio: {prohibidos}")
    if n_arch > MAX_ARCHIVOS_CAMBIO:
        problemas.append(f"cambio demasiado grande: {n_arch} archivos (máx {MAX_ARCHIVOS_CAMBIO})")
    if n_lin > MAX_LINEAS_CAMBIO:
        problemas.append(f"cambio demasiado grande: {n_lin} líneas (máx {MAX_LINEAS_CAMBIO})")
    diff = _run(["git", "diff"]).stdout
    if alcance:
        diff = _run(["git", "diff", "--"] + alcance).stdout
    for patron in PATRONES_SECRETOS:
        if patron.search(diff):
            problemas.append(f"posible secreto en el diff (patrón {patron.pattern[:40]}...)")
            break
    verdict = {
        "alcance": alcance or "árbol completo",
        "archivos": cambiados,
        "n_archivos": n_arch,
        "n_lineas": n_lin,
        "problemas": problemas,
        "aprobado": not problemas,
    }
    print(json.dumps(verdict, indent=2, ensure_ascii=False))
    return 0 if verdict["aprobado"] else 1


# --- Subcomando: publish ----------------------------------------------------

def _leer_version():
    props = (REPO / "version.properties").read_text()
    m_name = re.search(r"^versionName=(\d+)\.(\d+)\.(\d+)\s*$", props, re.M)
    m_code = re.search(r"^versionCode=(\d+)\s*$", props, re.M)
    if not m_name or not m_code:
        raise RuntimeError("version.properties sin versionName/versionCode")
    return (int(m_name.group(1)), int(m_name.group(2)), int(m_name.group(3))), int(m_code.group(1))


def _bump_version(resumen):
    """Sube patch+1 y code+1 en version.properties, module.prop, update.json
    y CHANGELOG. Devuelve (nueva_version, nuevo_code). En update.json el
    zipUrl se DEJA apuntando al release anterior: el bot lo actualiza tras
    publicar (así lo hizo la 379: el commit dejó zipUrl en 378 y el bot lo
    subió a 379 después)."""
    (mayor, menor, patch), code = _leer_version()
    nueva = f"{mayor}.{menor}.{patch + 1}"
    nuevo_code = code + 1
    hoy = datetime.now(timezone.utc).strftime("%Y-%m-%d")

    props_p = REPO / "version.properties"
    props = props_p.read_text()
    props = re.sub(r"^versionName=.*$", f"versionName={nueva}", props, flags=re.M)
    props = re.sub(r"^versionCode=.*$", f"versionCode={nuevo_code}", props, flags=re.M)
    # Comentario de la versión nueva justo encima de versionName.
    props = re.sub(r"^(versionName=.*)$",
                   f"# v{nueva}: {resumen}\n\\1", props, count=1, flags=re.M)
    props_p.write_text(props)

    mod_p = REPO / "magisk_module" / "module.prop"
    mod = mod_p.read_text()
    mod = re.sub(r"^version=.*$", f"version=v{nueva}", mod, flags=re.M)
    mod = re.sub(r"^versionCode=.*$", f"versionCode={nuevo_code}", mod, flags=re.M)
    mod = re.sub(r"^description=",
                 f"description=v{nueva}: {resumen} ", mod, count=1, flags=re.M)
    mod_p.write_text(mod)

    upd_p = REPO / "magisk_module" / "update.json"
    upd = json.loads(upd_p.read_text())
    upd["version"] = f"v{nueva}"
    upd["versionCode"] = nuevo_code
    # zipUrl NO se toca aquí: lo actualiza el bot tras la publicación.
    upd_p.write_text(json.dumps(upd, indent=2) + "\n")

    cl_p = REPO / "CHANGELOG.md"
    cl = cl_p.read_text()
    entrada = (f"# v{nueva} ({nuevo_code}) - {hoy}\n\n"
               f"- **Auto-fix ({resumen})**\n"
               f"- **Verificado:** CTest host al 100% (ver state/last_validation.json). "
               f"Kotlin sin compilar en VM; lo valida la CI (paso 17).\n\n")
    cl_p.write_text(entrada + cl)
    return nueva, nuevo_code


def _obtener_pat():
    """El PAT solo transita por stdin/memoria: variable de entorno
    IVANNA_GH_TOKEN o prompt interactivo (getpass, no se muestra).
    NUNCA se escribe en disco ni se imprime."""
    token = os.environ.get("IVANNA_GH_TOKEN", "").strip()
    if token:
        return token
    return getpass.getpass("PAT de GitHub (stdin, no se guarda ni se muestra): ").strip()


def _push_con_pat(token, dry_run):
    repo_url = ("https://github.com/luisurielpimentelperez814-design/"
                "IVANNA-OMEGA-SUPREME.git")
    url_con_token = repo_url.replace("https://", f"https://{token}@")
    cmds = [
        ["git", "fetch", "origin"],
        ["git", "remote", "set-url", "origin", url_con_token],
        ["git", "push", "origin", "main"],
        ["git", "remote", "set-url", "origin", repo_url],
    ]
    if dry_run:
        print("DRY-RUN: estos serían los comandos (el token NO se muestra):")
        for c in cmds:
            seguro = ["<PAT>" if token in p else p for p in c]
            print("  " + " ".join(seguro))
        return True
    try:
        # 1. ¿Estamos al día con origin/main? Si el bot publicó update.json
        #    después de nuestro último fetch, el push sería rechazado.
        _run(["git", "fetch", "origin"], check=True)
        atras = _run(["git", "rev-list", "--count", "HEAD..origin/main"]).stdout.strip()
        if atras != "0":
            print(f"origin/main va {atras} commits por delante (probablemente el "
                  "bot de update.json). NO se pushea; haz:\n"
                  "  git rebase origin/main   (conserva MI versión + el zipUrl del bot)\n"
                  "  GIT_EDITOR=true git rebase --continue\n"
                  "y vuelve a intentar el publish.")
            return False
        # 2. Push con el PAT y restauración inmediata de la URL limpia.
        _run(["git", "remote", "set-url", "origin", url_con_token], check=True)
        try:
            _run(["git", "push", "origin", "main"], check=True)
        finally:
            _run(["git", "remote", "set-url", "origin", repo_url], check=True)
        # 3. Verificar que la URL quedó limpia (sin token).
        remoto = _run(["git", "remote", "-v"]).stdout
        if token in remoto:
            raise RuntimeError("la URL remota quedó con el token: REVISA git remote -v")
        print("push OK; URL remota limpia verificada")
        return True
    except RuntimeError as e:
        print(f"push falló: {e}")
        # Restaurar la URL limpia aunque algo fallara a mitad.
        _run(["git", "remote", "set-url", "origin", repo_url])
        return False


def cmd_publish(args):
    # Incidente: el indicado o el más reciente en processing/.
    if args.incident:
        inc_path = Path(args.incident)
    else:
        proc = sorted(PROCESSING_DIR.glob("*.json"))
        if not proc:
            print("no hay incidentes en processing/; ejecuta run_fix.py next primero")
            return 2
        inc_path = proc[-1]
    incidente = _cargar_json(inc_path)
    fix = incidente.get("fix") or {}
    resumen = fix.get("summary", "")
    if not resumen:
        print("el incidente no tiene fix.summary: el subagente no rellenó el "
              "resumen del fix. No se publica.")
        print("si el fix no es posible, usa: run_fix.py report --incident "
              + str(inc_path))
        return 2
    # Gates: validación fresca + cambio publicable (con el alcance del fix).
    alcance = _alcance_fix(incidente)
    if not _validacion_fresca(alcance):
        print("sin validación fresca aprobada; corriendo validate...")
        if cmd_validate(args) != 0:
            print("CTest NO pasó al 100%: no se publica")
            return 1
    args.incident = str(inc_path)
    rc_gates = cmd_gates(args)
    if rc_gates != 0:
        print("gates NO pasaron: no se publica")
        return 1
    ejecutar = args.execute and os.environ.get("IVANNA_ALLOW_PUBLISH") == "1"
    if args.execute and not ejecutar:
        print("publish --execute requiere IVANNA_ALLOW_PUBLISH=1; se muestra el "
              "plan (dry-run) y no se ejecuta nada")
    if args.dry_run or not ejecutar:
        # En dry-run no se tocan los archivos: solo se muestra el plan.
        (mayor, menor, patch), code = _leer_version()
        nueva = f"{mayor}.{menor}.{patch + 1}"
        nuevo_code = code + 1
        print(f"PLAN de publicación v{nueva} ({nuevo_code}):")
        print("  1. bump: version.properties + module.prop + update.json + CHANGELOG")
        print(f"  2. git add -A (archivos del fix: {fix.get('files', [])})")
        print(f"  3. git commit -m 'v{nueva}: {resumen} [publish-release]'")
        print("  4. push con PAT vía stdin (URL remota limpia después)")
        print("  5. archivar el incidente en archive/")
        print("Para ejecutar: IVANNA_ALLOW_PUBLISH=1 python3 run_fix.py publish "
              "--incident " + str(inc_path) + " --execute")
        print("El PAT lo pide por stdin (getpass) o por IVANNA_GH_TOKEN; nunca "
              "se escribe en disco ni se imprime.")
        return 0
    # --- Ejecución real (solo con IVANNA_ALLOW_PUBLISH=1 y --execute) ---
    nueva, nuevo_code = _bump_version(resumen)
    archivos_fix = fix.get("files", [])
    for f in archivos_fix + ["version.properties", "magisk_module/module.prop",
                             "magisk_module/update.json", "CHANGELOG.md"]:
        _run(["git", "add", "--", f])
    mensaje = (f"v{nueva}: {resumen} [publish-release]\n\n"
               f"Auto-fix del incidente {incidente['incident_id']} "
               f"({incidente['tipo']}: {incidente['razon']}).\n"
               f"CTest host: {fix.get('ctest', 'ver state/last_validation.json')}. "
               "Kotlin sin compilar en VM; lo valida la CI (paso 17).")
    _run(["git", "commit", "-m", mensaje], check=True)
    print(f"commit v{nueva} creado")
    token = _obtener_pat()
    if not token:
        print("sin PAT: el commit quedó local, no se pusheó. El PAT se pide "
              "por stdin cuando se vuelva a intentar el publish.")
        return 2
    try:
        if not _push_con_pat(token, dry_run=False):
            return 1
    finally:
        token = None  # no retener el secreto en memoria más de lo necesario
    ARCHIVE_DIR.mkdir(parents=True, exist_ok=True)
    inc_path.rename(ARCHIVE_DIR / inc_path.name)
    print(f"publicado v{nueva} ({nuevo_code}); incidente archivado")
    print(f"release: https://github.com/luisurielpimentelperez814-design/"
          f"IVANNA-OMEGA-SUPREME/releases/tag/v{nueva}")
    return 0


def cmd_draft(args):
    """Redacta con el LLM (vía el motor de evolución) el parche para el
    incidente en processing/. NO aplica nada: escribe processing/<id>.patch
    y deja el incidente listo para validate + gates. El fix lo valida el
    CTest; sin CTest al 100% no se publica (ver cmd_validate/cmd_gates)."""
    import importlib.util as _ilu
    spec = _ilu.spec_from_file_location(
        "evolution_engine", REPO / "tools" / "evolution" / "evolution_engine.py")
    evo = _ilu.module_from_spec(spec)
    spec.loader.exec_module(evo)
    if args.incident:
        inc_path = Path(args.incident)
    else:
        proc = sorted(PROCESSING_DIR.glob("*.json"))
        if not proc:
            print("no hay incidentes en processing/; ejecuta run_fix.py next primero")
            return 2
        inc_path = proc[-1]
    incidente = _cargar_json(inc_path)
    con = CONOCIMIENTO.get(incidente["tipo"], {})
    finding = {
        "kind": f"autorepair:{incidente['tipo']}",
        "title": f"{incidente['tipo']}: {incidente['razon']}",
        "evidence": (
            f"Incidente {incidente['incident_id']} detectado "
            f"{incidente['detected_at']} (snapshot sequence "
            f"{incidente.get('sequence')}). Severidad {incidente['severidad']}.\n"
            f"Archivos probables: {', '.join(con.get('archivos', [])) or 'por determinar'}.\n"
            f"Pistas: {'; '.join(con.get('pistas', []))}\n"
            f"Prohibido: {'; '.join(con.get('prohibido', []))}"),
        "files": con.get("archivos", []),
    }
    try:
        draft = evo.draft_patch(finding)
    except SystemExit as e:
        print(f"draft rechazado: {e}")
        return 2
    patch_path = PROCESSING_DIR / f"{incidente['incident_id']}.patch"
    patch_path.write_text(draft["patch"])
    print(f"draft LLM ({draft['model']}, {draft['latency_s']} s): {patch_path}")
    print(f"archivos del parche: {', '.join(draft['files'])}")
    print("SIN APLICAR: revisa el diff y aplícalo a mano, o deja que el "
          "subagente lo aplique; después: run_fix.py validate && run_fix.py gates")
    return 0


# --- Subcomando: report -----------------------------------------------------

def cmd_report(args):
    inc_path = Path(args.incident)
    incidente = _cargar_json(inc_path)
    motivo = args.reason or "sin motivo registrado"
    REPORTS_DIR.mkdir(parents=True, exist_ok=True)
    ruta = REPORTS_DIR / f"{incidente['incident_id']}.md"
    contenido = f"""# Reporte de auto-reparación — {incidente['incident_id']}

- **Tipo:** {incidente['tipo']} (severidad {incidente['severidad']})
- **Razón:** {incidente['razon']}
- **Detectado:** {incidente['detected_at']} (snapshot sequence {incidente.get('sequence')})
- **Estado:** NO publicado (reporte para Esse)

## Por qué no se publicó
{motivo}

## Snapshot (métricas/agente)
```json
{json.dumps({"metrics": incidente["snapshot"].get("metrics"),
             "agent": incidente["snapshot"].get("agent"),
             "routeB": incidente["snapshot"].get("routeB")}, indent=2, ensure_ascii=False)}
```

## Qué se revisó
- Incidente clasificado por watcher.py con las reglas de rules.py.
- Tipo auto-arreglable: {CONOCIMIENTO.get(incidente['tipo'], {}).get('auto_fixable', 'desconocido')}

## Siguiente paso sugerido
Que el agente principal entregue este reporte a Esse y, si él lo aprueba,
se lance el fix manual (o se ajuste el umbral de la regla en rules.py si
fue un falso positivo).
"""
    ruta.write_text(contenido)
    ARCHIVE_DIR.mkdir(parents=True, exist_ok=True)
    inc_path.rename(ARCHIVE_DIR / inc_path.name)
    print(f"reporte escrito: {ruta}")
    print("NOTIFICACIÓN: este reporte queda listo para que el agente principal "
          "se lo entregue a Esse en el chat (el watcher no manda mensajes solo).")
    return 0


# --- Subcomandos: pending / ack ---------------------------------------------

def cmd_pending(args):
    print("== cola (pendientes) ==")
    for f in sorted(QUEUE_DIR.glob("*.json")):
        try:
            d = _cargar_json(f)
            print(f"  {f.name}  [{d.get('severidad')}] {d.get('razon')}")
        except json.JSONDecodeError:
            print(f"  {f.name}  (JSON inválido)")
    print("== en proceso ==")
    for f in sorted(PROCESSING_DIR.glob("*.json")):
        print(f"  {f.name}")
    print("== reportes ==")
    for f in sorted(REPORTS_DIR.glob("*.md")):
        ack = (REPORTS_DIR / (f.stem + ".ack")).exists()
        print(f"  {f.name}  {'(entregado)' if ack else '(PENDIENTE DE ENTREGAR)'}")
    return 0


def cmd_ack(args):
    reporte = REPORTS_DIR / args.report
    if not reporte.exists():
        print(f"no existe el reporte {reporte}")
        return 2
    (REPORTS_DIR / (reporte.stem + ".ack")).write_text(
        f"entregado a Esse el {_ahora_iso()}\n")
    print(f"reporte {reporte.name} marcado como entregado")
    return 0


# --- main -------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Orquestador de fix de IVANNA")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("next", help="toma el incidente más antiguo y genera el prompt")
    p_val = sub.add_parser("validate", help="compila y corre CTest (el gate es que pasen todos los tests)")
    p_val.add_argument("--incident", help="JSON del incidente en processing/ "
                       "(la validación se ancla a sus archivos)")
    p_gates = sub.add_parser("gates", help="verifica que el cambio sea publicable")
    p_gates.add_argument("--incident", help="JSON del incidente en processing/ "
                         "(el gate se evalúa solo sobre sus archivos)")

    p_pub = sub.add_parser("publish", help="bump + commit + push (con gates)")
    p_pub.add_argument("--incident", help="JSON del incidente en processing/")
    p_pub.add_argument("--execute", action="store_true",
                       help="ejecutar de verdad (requiere IVANNA_ALLOW_PUBLISH=1)")
    p_pub.add_argument("--dry-run", action="store_true",
                       help="solo mostrar el plan, sin tocar nada")

    p_draft = sub.add_parser("draft", help="el LLM redacta el parche del "
                             "incidente (no aplica nada, no publica)")
    p_draft.add_argument("--incident", help="JSON del incidente en processing/ "
                         "(por defecto, el más reciente)")

    p_rep = sub.add_parser("report", help="escribe el reporte de no-publicación")
    p_rep.add_argument("--incident", required=True, help="JSON del incidente")
    p_rep.add_argument("--reason", help="por qué no se publicó")

    sub.add_parser("pending", help="lista cola, proceso y reportes")
    p_ack = sub.add_parser("ack", help="marca un reporte como entregado a Esse")
    p_ack.add_argument("report", help="nombre del .md en reports/")

    args = ap.parse_args()
    return {
        "next": cmd_next,
        "validate": cmd_validate,
        "gates": cmd_gates,
        "publish": cmd_publish,
        "draft": cmd_draft,
        "report": cmd_report,
        "pending": cmd_pending,
        "ack": cmd_ack,
    }[args.cmd](args)


if __name__ == "__main__":
    raise SystemExit(main())
