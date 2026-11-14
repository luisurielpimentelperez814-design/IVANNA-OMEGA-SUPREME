#!/usr/bin/env python3
"""
Reglas de clasificación de incidentes de IVANNA (tools/autorepair/rules.py).

Cada regla es una función `rule_<nombre>(snapshot, estado)` que devuelve la
razón (texto en español) si el problema está presente, o None si no lo está.
Ninguna regla rompe ante campos ausentes: todo se lee con .get() y un campo
que falta significa "no se puede afirmar el problema" (fail-closed).

El snapshot es el JSON que devuelve GET /v1/diagnostics/latest del puente
(esquema v1, ver diagnostic-bridge/server.mjs):
  metrics: dspProcessingUs, frameDrops, underruns, thermalThrottling,
           memoryPressure ("normal" | "moderate" | "high" | "unknown")
  agent:   running, daemonConnected, adaptiveEngineRunning, clipping,
           latencyDegraded, clipCount, roundTripLatencyUs
  sequence, uptimeMs, observedAt, receivedAt, snapshotId

El `estado` es el dict que guarda el watcher en state/watcher_state.json:
  last_sequence, last_underruns, last_frame_drops, last_seen_at (ISO 8601).
Las reglas de TASA (underruns, frameDrops) necesitan dos muestras: si no hay
estado previo, no disparan (devuelven None) y el watcher guarda la muestra
para la próxima vuelta.

Umbrales (todos documentados junto a su regla; se cambian aquí, no en el
watcher):
"""
from datetime import datetime, timezone

# --- Umbrales ---------------------------------------------------------------

# ASRC / deriva de reloj: más de 10 underruns por minuto significa que el
# reloj deriva más rápido de lo que el ASRC corrige, o que el ASRC no está
# corriendo. Referencia: en vivo se midieron 199 underruns / 523 overruns
# antes del ASRC; con la v5 el dump marca sat=0 y drift -6.25 ppm.
ASRC_DERIVA_UNDERRUNS_POR_MIN = 10

# DSP sobrecargado: más de 60 frameDrops por minuto (0.27 % de los bloques
# de 128 muestras a 48 kHz) ya se oye como clics. O un bloque que tarda más
# de 4000 µs en procesarse: el presupuesto por bloque de 128 muestras a
# 48 kHz es 2667 µs; 4000 deja margen y evita falsos positivos por un pico.
DSP_SOBRECARGA_FRAMEDROPS_POR_MIN = 60
DSP_SOBRECARGA_TIEMPO_US = 4000

# Ruta B: el beacon es el ÚNICO testigo válido de procesado. Si la Ruta B
# está activa y el beacon sigue en 0 frames con enabled_count 0, el efecto
# no está acoplado (o no está habilitado). NOTA: el puente v1 NO reenvía el
# campo routeB (su sanitize() solo permite metrics + agent), así que esta
# regla está DORMIDA hasta que la app envíe esquema v2 con routeB. Cuando
# el campo exista, la regla dispara sola; mientras tanto no da falsos
# positivos porque exige que el campo esté presente.
ROUTE_B_BEACON_FRAMES_MINIMOS = 0

# Severidad por tipo (la usa el orquestador para decidir si intenta el fix
# automático o solo reporta).
SEVERIDAD = {
    "route-b-no-acopla": "alta",
    "asrc-deriva": "media",
    "dsp-sobrecarga": "media",
    "termico": "alta",
    "daemon-desconectado": "alta",
    "memoria-alta": "media",
    "latencia-degradada": "media",
}

# Orden de evaluación: las de causa raíz primero (si el daemon está caído,
# todo lo demás puede ser consecuencia).
ORDEN_REGLAS = (
    "daemon-desconectado",
    "termico",
    "route-b-no-acopla",
    "asrc-deriva",
    "dsp-sobrecarga",
    "memoria-alta",
    "latencia-degradada",
)


# --- Utilidades -------------------------------------------------------------

def _num(d, *keys):
    """Lee un número anidado (dict.get en cadena) o None si falta/no es nº."""
    cur = d
    for k in keys:
        if not isinstance(cur, dict):
            return None
        cur = cur.get(k)
    return cur if isinstance(cur, (int, float)) and not isinstance(cur, bool) else None


def _tasa_por_min(actual, previo, ahora_iso, antes_iso):
    """Tasa de crecimiento por minuto entre dos muestras. None si no se
    puede calcular (sin muestra previa, tiempo <= 0, o el contador se
    reseteó, p. ej. por reinicio del teléfono)."""
    if actual is None or previo is None or not ahora_iso or not antes_iso:
        return None
    try:
        t1 = datetime.fromisoformat(antes_iso.replace("Z", "+00:00"))
        t2 = datetime.fromisoformat(ahora_iso.replace("Z", "+00:00"))
    except ValueError:
        return None
    minutos = (t2 - t1).total_seconds() / 60.0
    if minutos <= 0 or actual < previo:
        return None
    return (actual - previo) / minutos


# --- Reglas -----------------------------------------------------------------

def rule_daemon_desconectado(snapshot, estado):
    """El agente corre pero el daemon no está conectado: sin daemon no hay
    descubrimiento de sesión (Ruta B) ni control del motor."""
    agent = snapshot.get("agent") or {}
    if agent.get("running") is True and agent.get("daemonConnected") is False:
        return "el agente corre pero el daemon no está conectado"
    return None


def rule_termico(snapshot, estado):
    """Throttling térmico activo: el SoC baja frecuencia y el DSP no llega."""
    metrics = snapshot.get("metrics") or {}
    if metrics.get("thermalThrottling") is True:
        return "throttling térmico activo en el dispositivo"
    return None


def rule_route_b_no_acopla(snapshot, estado):
    """Ruta B activa pero el beacon no crece: el efecto omega no está
    acoplado a la sesión real (o no está habilitado). Requiere el campo
    routeB del esquema v2; en esquema v1 no dispara (ver nota arriba)."""
    route_b = snapshot.get("routeB")
    if not isinstance(route_b, dict):
        return None
    if route_b.get("active") is not True:
        return None
    frames = route_b.get("beaconFrames")
    enabled = route_b.get("enabledCount")
    if frames is not None and enabled is not None:
        if frames <= ROUTE_B_BEACON_FRAMES_MINIMOS and enabled == 0:
            return ("Ruta B activa pero beacon en 0 frames y enabled_count 0: "
                    "omega no acoplado a la sesión real")
    return None


def rule_asrc_deriva(snapshot, estado):
    """Underruns creciendo rápido: la deriva de reloj supera la corrección
    del ASRC (o el ASRC no corre). Necesita dos muestras para la tasa."""
    underruns = _num(snapshot, "metrics", "underruns")
    ahora = snapshot.get("observedAt") or snapshot.get("receivedAt")
    tasa = _tasa_por_min(underruns, (estado or {}).get("last_underruns"),
                         ahora, (estado or {}).get("last_seen_at"))
    if tasa is not None and tasa > ASRC_DERIVA_UNDERRUNS_POR_MIN:
        return f"underruns creciendo a {tasa:.1f}/min (umbral {ASRC_DERIVA_UNDERRUNS_POR_MIN}/min)"
    return None


def rule_dsp_sobrecarga(snapshot, estado):
    """FrameDrops creciendo rápido o un bloque que tarda demasiado en
    procesarse: el DSP no llega en tiempo real."""
    drops = _num(snapshot, "metrics", "frameDrops")
    ahora = snapshot.get("observedAt") or snapshot.get("receivedAt")
    tasa = _tasa_por_min(drops, (estado or {}).get("last_frame_drops"),
                         ahora, (estado or {}).get("last_seen_at"))
    if tasa is not None and tasa > DSP_SOBRECARGA_FRAMEDROPS_POR_MIN:
        return (f"frameDrops creciendo a {tasa:.1f}/min "
                f"(umbral {DSP_SOBRECARGA_FRAMEDROPS_POR_MIN}/min)")
    us = _num(snapshot, "metrics", "dspProcessingUs")
    if us is not None and us > DSP_SOBRECARGA_TIEMPO_US:
        return (f"procesado DSP de {us:.0f} µs por bloque "
                f"(umbral {DSP_SOBRECARGA_TIEMPO_US} µs)")
    return None


def rule_memoria_alta(snapshot, estado):
    """Presión de memoria alta: riesgo de que el sistema mate el proceso
    de audio o el daemon."""
    metrics = snapshot.get("metrics") or {}
    if metrics.get("memoryPressure") == "high":
        return "presión de memoria alta (memoryPressure=high)"
    return None


def rule_latencia_degradada(snapshot, estado):
    """Latencia de ida y vuelta degradada según el propio agente."""
    agent = snapshot.get("agent") or {}
    if agent.get("latencyDegraded") is True:
        rtt = _num(snapshot, "agent", "roundTripLatencyUs")
        extra = f" (RTT {rtt:.0f} µs)" if rtt is not None else ""
        return f"latencia degradada según el agente{extra}"
    return None


REGLAS = {
    "daemon-desconectado": rule_daemon_desconectado,
    "termico": rule_termico,
    "route-b-no-acopla": rule_route_b_no_acopla,
    "asrc-deriva": rule_asrc_deriva,
    "dsp-sobrecarga": rule_dsp_sobrecarga,
    "memoria-alta": rule_memoria_alta,
    "latencia-degradada": rule_latencia_degradada,
}


def classify(snapshot, estado=None):
    """Clasifica un snapshot. Devuelve una lista de dicts
    {tipo, severidad, razon} (vacía si está sano). Un snapshot puede
    disparar varias reglas a la vez; cada una genera su propio incidente."""
    estado = estado or {}
    hallazgos = []
    for tipo in ORDEN_REGLAS:
        razon = REGLAS[tipo](snapshot, estado)
        if razon:
            hallazgos.append({
                "tipo": tipo,
                "severidad": SEVERIDAD[tipo],
                "razon": razon,
            })
    return hallazgos
