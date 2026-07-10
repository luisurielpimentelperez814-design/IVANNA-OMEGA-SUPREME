#!/system/bin/sh
# IVANNA OMEGA SUPREME — ivanna_control.sh v1.0
# CLI para enviar comandos al daemon IVANNA via socket Unix.
# Uso desde ADB o desde scripts del módulo:
#   ivanna_control.sh status
#   ivanna_control.sh preset Warm
#   ivanna_control.sh volume 0.8
#   ivanna_control.sh bypass on|off
#   ivanna_control.sh concert on|off

SOCKET=/dev/socket/ivanna_omega
TIMEOUT=2
LOG=/data/adb/ivanna_control.log
log() { echo "[$(date '+%H:%M:%S')] ctrl: $1" >> "$LOG"; }

send_command() {
    CMD="$1"
    # Verificar que el socket existe
    if [ ! -S "$SOCKET" ] && [ ! -e "$SOCKET" ]; then
        log "ERROR: socket $SOCKET no encontrado — daemon no activo"
        echo "ERROR: daemon no activo"
        return 1
    fi
    # Enviar comando y esperar respuesta (timeout 2s)
    RESP=$(echo "$CMD" | timeout "$TIMEOUT" nc -U "$SOCKET" 2>/dev/null)
    RESULT=$?
    if [ $RESULT -eq 0 ] && [ -n "$RESP" ]; then
        log "CMD=$CMD → RESP=$RESP"
        echo "$RESP"
    else
        # Fallback: setprop para que la app lo recoja al sincronizar
        setprop ivanna.pending_cmd "$CMD"
        log "CMD=$CMD → setprop fallback (daemon no respondió)"
        echo "OK (queued)"
    fi
}

COMMAND="$1"
ARG="$2"

case "$COMMAND" in
    status)
        send_command "STATUS"
        ;;
    preset)
        [ -z "$ARG" ] && { echo "Uso: ivanna_control.sh preset <nombre>"; exit 1; }
        send_command "SET_PRESET:$ARG"
        ;;
    volume)
        [ -z "$ARG" ] && { echo "Uso: ivanna_control.sh volume <0.0-1.0>"; exit 1; }
        send_command "SET_PF_MASTER:$ARG"
        ;;
    bypass)
        case "$ARG" in
            on)  send_command "SET_BYPASS:1" ;;
            off) send_command "SET_BYPASS:0" ;;
            *)   echo "Uso: ivanna_control.sh bypass on|off"; exit 1 ;;
        esac
        ;;
    concert)
        case "$ARG" in
            on)
                send_command "SET_PRESET:Spatial"
                send_command "SET_REVERB:0.7"
                log "Modo Concierto ON"
                echo "Modo Concierto activado"
                ;;
            off)
                send_command "SET_PRESET:Warm"
                send_command "SET_REVERB:0.0"
                log "Modo Concierto OFF"
                echo "Modo Concierto desactivado"
                ;;
            *)  echo "Uso: ivanna_control.sh concert on|off"; exit 1 ;;
        esac
        ;;
    telemetry)
        send_command "GET_TELEMETRY"
        ;;
    reload)
        send_command "RELOAD_PARAMS"
        ;;
    *)
        echo "IVANNA OMEGA SUPREME — Control CLI v1.0"
        echo "Uso:"
        echo "  $0 status              — Estado del daemon"
        echo "  $0 preset <nombre>     — Cambiar preset (Flat/Warm/Bright/Punch/Spatial/Heavy/Vocal/Bass)"
        echo "  $0 volume <0.0-1.0>    — Volumen master"
        echo "  $0 bypass on|off       — Bypass del DSP"
        echo "  $0 concert on|off      — Modo Concierto (Spatial + reverb)"
        echo "  $0 telemetry           — Métricas en tiempo real"
        echo "  $0 reload              — Releer parámetros desde la app"
        exit 0
        ;;
esac
