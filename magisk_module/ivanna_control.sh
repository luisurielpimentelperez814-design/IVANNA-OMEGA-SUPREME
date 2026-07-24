#!/system/bin/sh
# IVANNA OMEGA SUPREME — ivanna_control.sh v1.1 (PATCH)
# - Chequeo previo de nc (evita crash si no está)
# - Timeout duro con kill; fallback setprop siempre disponible

SOCKET=/dev/socket/ivanna_omega
TIMEOUT=2
LOG=/data/adb/ivanna_control.log
log() { echo "[$(date '+%H:%M:%S')] ctrl: $1" >> "$LOG" 2>/dev/null; }

have_nc() { command -v nc >/dev/null 2>&1; }

send_command() {
    CMD="$1"
    if [ ! -e "$SOCKET" ]; then
        log "ERROR: socket $SOCKET no existe — daemon no activo"
        setprop ivanna.pending_cmd "$CMD"
        echo "queued (no socket)"
        return 1
    fi
    if ! have_nc; then
        log "AVISO: nc no disponible — usando setprop fallback"
        setprop ivanna.pending_cmd "$CMD"
        echo "queued (no nc)"
        return 0
    fi
    RESP=$(echo "$CMD" | timeout "$TIMEOUT" nc -U "$SOCKET" 2>/dev/null)
    RC=$?
    if [ $RC -eq 0 ] && [ -n "$RESP" ]; then
        log "CMD=$CMD → RESP=$RESP"
        echo "$RESP"
    else
        setprop ivanna.pending_cmd "$CMD"
        log "CMD=$CMD → setprop fallback (rc=$RC)"
        echo "OK (queued)"
    fi
}

COMMAND="$1"; ARG="$2"
case "$COMMAND" in
    status)     send_command "STATUS" ;;
    preset)     [ -z "$ARG" ] && { echo "Uso: $0 preset <nombre>"; exit 1; }; send_command "SET_PRESET:$ARG" ;;
    volume)     [ -z "$ARG" ] && { echo "Uso: $0 volume <0.0-1.0>"; exit 1; }; send_command "SET_PF_MASTER:$ARG" ;;
    bypass)     case "$ARG" in on) send_command "SET_BYPASS:1";; off) send_command "SET_BYPASS:0";; *) echo "Uso: $0 bypass on|off"; exit 1;; esac ;;
    concert)    case "$ARG" in
                    on)  send_command "SET_PRESET:Spatial"; send_command "SET_REVERB:0.7"; echo "Modo Concierto ON" ;;
                    off) send_command "SET_PRESET:Warm";    send_command "SET_REVERB:0.0"; echo "Modo Concierto OFF" ;;
                    *)   echo "Uso: $0 concert on|off"; exit 1 ;;
                esac ;;
    telemetry)  send_command "GET_TELEMETRY" ;;
    reload)     send_command "RELOAD_PARAMS" ;;
    probe)      # Ping barato — devuelve "alive" solo si daemon responde
                if [ -e "$SOCKET" ] && have_nc; then
                    R=$(echo "PING" | timeout 1 nc -U "$SOCKET" 2>/dev/null)
                    [ -n "$R" ] && echo "alive" || echo "socket_open_no_response"
                else
                    echo "no_daemon"
                fi ;;
    *)
        cat <<EOF
IVANNA OMEGA SUPREME — Control CLI v1.1
Uso:
  $0 status               Estado del daemon
  $0 preset <nombre>      Flat|Warm|Bright|Punch|Spatial|Heavy|Vocal|Bass
  $0 volume <0.0-1.0>     Volumen master
  $0 bypass on|off        Bypass DSP
  $0 concert on|off       Modo Concierto (Spatial + reverb)
  $0 telemetry            Métricas
  $0 reload               Releer parámetros
  $0 probe                Ping daemon (alive|no_daemon)
EOF
        exit 0 ;;
esac
