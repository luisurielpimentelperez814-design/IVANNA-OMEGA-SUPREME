#!/usr/bin/env python3
"""
Watcher de auto-reparación de IVANNA (tools/autorepair/watcher.py).

Cada vuelta:
  1. Lee el último snapshot del puente (GET /v1/diagnostics/latest).
  2. Si no hay snapshot nuevo (mismo `sequence` que la última vez), no hace
     nada y sale en silencio (sin spam en el cron).
  3. Si hay snapshot nuevo, lo clasifica con las reglas de rules.py.
  4. Por cada problema clasificado, escribe UN archivo de incidente en
     queue/<timestamp>-<tipo>.json (deduplicado por sequence + tipo: si ya
     existe un incidente pendiente o archivado con esa sequence y ese tipo,
     no duplica).
  5. Guarda el estado (última sequence y contadores) en
     state/watcher_state.json para que las reglas de tasa puedan comparar
     con la muestra anterior.

Configuración por variables de entorno (NUNCA en el código):
  IVANNA_BRIDGE_URL        URL base del puente (por defecto http://127.0.0.1:8787,
                           útil para probar en local; en producción es la URL
                           de Render, p. ej. https://ivanna-diagnostic-bridge.onrender.com)
  IVANNA_DIAGNOSTIC_TOKEN  bearer token del puente (mínimo 32 caracteres).
                           Sin token, el watcher no puede leer el puente y
                           sale en silencio (código 0, sin spam).

Uso:
  python3 watcher.py --once                  # una vuelta (lo que corre el cron)
  python3 watcher.py --loop --interval 5     # bucle cada 5 min (prueba manual)
  python3 watcher.py --simulate examples/snapshot-termico.json
                                             # clasifica un ejemplo sin puente
  python3 watcher.py --self-test            # clasifica todos los ejemplos y
                                             # verifica el tipo esperado
  python3 watcher.py --once --verbose       # una vuelta con log por stdout

El watcher SOLO detecta y encola. No parchea, no publica, no reinicia nada.
El fix lo orquesta tools/autorepair/run_fix.py (ver su README).
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))

from rules import classify  # noqa: E402

STATE_FILE = BASE / "state" / "watcher_state.json"
QUEUE_DIR = BASE / "queue"
PROCESSING_DIR = BASE / "processing"
ARCHIVE_DIR = BASE / "archive"

DEFAULT_BRIDGE_URL = "http://127.0.0.1:8787"
FETCH_TIMEOUT_S = 10


def _ahora_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _ts_compacto(iso=None):
    dt = datetime.now(timezone.utc) if iso is None else datetime.fromisoformat(iso.replace("Z", "+00:00"))
    return dt.strftime("%Y%m%dT%H%M%SZ")


def _cargar_estado():
    try:
        return json.loads(STATE_FILE.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def _guardar_estado(estado):
    STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
    STATE_FILE.write_text(json.dumps(estado, indent=2, ensure_ascii=False))


def _leer_snapshot_puente(base_url, token):
    """GET /v1/diagnostics/latest. Devuelve (snapshot, error). error es None
    si salió bien; si el puente no responde o no hay snapshot, devuelve
    (None, motivo) y el llamador NO hace nada (silencio, no spam)."""
    url = base_url.rstrip("/") + "/v1/diagnostics/latest"
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {token}"})
    try:
        with urllib.request.urlopen(req, timeout=FETCH_TIMEOUT_S) as resp:
            return json.loads(resp.read().decode("utf-8")), None
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None, "puente sin snapshot todavía (404 no_snapshot_yet)"
        return None, f"puente respondió HTTP {e.code}"
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        return None, f"puente no alcanzable: {e}"
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None, "respuesta del puente no es JSON válido"


def _incidente_existe(sequence, tipo):
    """Dedup: ¿ya hay un incidente con esta sequence y este tipo en la cola,
    en proceso o archivado?"""
    for d in (QUEUE_DIR, PROCESSING_DIR, ARCHIVE_DIR):
        if not d.exists():
            continue
        for f in d.glob("*.json"):
            try:
                data = json.loads(f.read_text())
            except (json.JSONDecodeError, OSError):
                continue
            if data.get("sequence") == sequence and data.get("tipo") == tipo:
                return True
    return False


def _encolar(hallazgo, snapshot):
    """Escribe el incidente en queue/. Devuelve la ruta o None si ya existía."""
    sequence = snapshot.get("sequence")
    tipo = hallazgo["tipo"]
    if sequence is not None and _incidente_existe(sequence, tipo):
        return None
    ts = _ts_compacto(snapshot.get("observedAt") or snapshot.get("receivedAt"))
    incident_id = f"{ts}-{tipo}"
    incidente = {
        "incident_id": incident_id,
        "tipo": tipo,
        "severidad": hallazgo["severidad"],
        "razon": hallazgo["razon"],
        "detected_at": _ahora_iso(),
        "sequence": sequence,
        "snapshot": snapshot,
        "context": {
            "bridge_url": os.environ.get("IVANNA_BRIDGE_URL", DEFAULT_BRIDGE_URL),
            "app_version": snapshot.get("appVersion"),
        },
        "fix": None,  # lo rellena el subagente de fix antes de publicar
    }
    QUEUE_DIR.mkdir(parents=True, exist_ok=True)
    ruta = QUEUE_DIR / f"{incident_id}.json"
    ruta.write_text(json.dumps(incidente, indent=2, ensure_ascii=False))
    return ruta


def _actualizar_estado_desde_snapshot(estado, snapshot):
    """Guarda la muestra actual para que la próxima vuelta pueda calcular
    tasas (underruns/min, frameDrops/min)."""
    metrics = snapshot.get("metrics") or {}
    estado["last_sequence"] = snapshot.get("sequence")
    if isinstance(metrics.get("underruns"), (int, float)):
        estado["last_underruns"] = metrics["underruns"]
    if isinstance(metrics.get("frameDrops"), (int, float)):
        estado["last_frame_drops"] = metrics["frameDrops"]
    estado["last_seen_at"] = snapshot.get("observedAt") or snapshot.get("receivedAt") or _ahora_iso()
    estado["last_check_at"] = _ahora_iso()
    return estado


def procesar_snapshot(snapshot, estado, verbose=False):
    """Una vuelta de clasificación sobre un snapshot ya obtenido.
    Devuelve (incidentes_encolados, estado_nuevo)."""
    sequence = snapshot.get("sequence")
    if sequence is not None and estado.get("last_sequence") == sequence:
        if verbose:
            print(f"sin snapshot nuevo (sequence {sequence})")
        return [], estado
    hallazgos = classify(snapshot, estado)
    encolados = []
    for h in hallazgos:
        ruta = _encolar(h, snapshot)
        if ruta is not None:
            encolados.append(str(ruta))
            if verbose:
                print(f"incidente encolado: {ruta} ({h['tipo']}: {h['razon']})")
        elif verbose:
            print(f"duplicado, no se encola: sequence {sequence} tipo {h['tipo']}")
    estado = _actualizar_estado_desde_snapshot(estado, snapshot)
    _guardar_estado(estado)
    if verbose and not hallazgos:
        print(f"snapshot {sequence} sano, sin hallazgos")
    return encolados, estado


def _una_vuelta(verbose=False):
    token = os.environ.get("IVANNA_DIAGNOSTIC_TOKEN", "")
    if len(token) < 32:
        if verbose:
            print("IVANNA_DIAGNOSTIC_TOKEN no configurado o corto: sin puente, no se hace nada",
                  file=sys.stderr)
        return 0
    base_url = os.environ.get("IVANNA_BRIDGE_URL", DEFAULT_BRIDGE_URL)
    snapshot, error = _leer_snapshot_puente(base_url, token)
    if error:
        if verbose:
            print(error, file=sys.stderr)
        return 0  # sin puente / sin snapshot: silencio, no spam
    estado = _cargar_estado()
    procesar_snapshot(snapshot, estado, verbose=verbose)
    return 0


def _self_test():
    """Clasifica cada ejemplo de examples/ y comprueba el tipo esperado
    (campo _expected_tipo; null = sano). No toca el puente ni la cola."""
    ejemplos = sorted((BASE / "examples").glob("snapshot-*.json"))
    if not ejemplos:
        print("sin ejemplos en examples/")
        return 1
    fallos = 0
    for ruta in ejemplos:
        data = json.loads(ruta.read_text())
        esperado = data.pop("_expected_tipo", None)
        estado = data.pop("_state", {}) or {}
        snapshot = {k: v for k, v in data.items() if not k.startswith("_")}
        hallazgos = classify(snapshot, estado)
        tipos = [h["tipo"] for h in hallazgos]
        ok = (esperado is None and not tipos) or (esperado in tipos)
        marca = "OK  " if ok else "FALLO"
        if not ok:
            fallos += 1
        print(f"{marca} {ruta.name}: esperado={esperado} obtenido={tipos or 'sano'}")
        for h in hallazgos:
            print(f"      - {h['tipo']} [{h['severidad']}]: {h['razon']}")
    print(f"\n{len(ejemplos) - fallos}/{len(ejemplos)} ejemplos correctos")
    return 0 if fallos == 0 else 1


def main():
    ap = argparse.ArgumentParser(description="Watcher de auto-reparación de IVANNA")
    ap.add_argument("--once", action="store_true", help="una sola vuelta (lo que corre el cron)")
    ap.add_argument("--loop", action="store_true", help="bucle continuo (prueba manual)")
    ap.add_argument("--interval", type=float, default=5.0, help="minutos entre vueltas en --loop (def. 5)")
    ap.add_argument("--simulate", metavar="JSON", help="clasifica un snapshot de archivo, sin puente")
    ap.add_argument("--self-test", action="store_true", help="clasifica los ejemplos y verifica el tipo esperado")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return _self_test()
    if args.simulate:
        data = json.loads(Path(args.simulate).read_text())
        data.pop("_expected_tipo", None)
        estado = data.pop("_state", {}) or {}
        snapshot = {k: v for k, v in data.items() if not k.startswith("_")}
        estado_previo = _cargar_estado()
        estado_usado = estado if estado else estado_previo
        encolados, _ = procesar_snapshot(snapshot, estado_usado, verbose=True)
        print(f"encolados: {encolados or 'ninguno'}")
        return 0
    if args.loop:
        while True:
            _una_vuelta(verbose=args.verbose)
            time.sleep(args.interval * 60)
    return _una_vuelta(verbose=args.verbose)


if __name__ == "__main__":
    raise SystemExit(main())
