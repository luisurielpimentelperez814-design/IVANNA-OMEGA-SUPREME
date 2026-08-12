#!/system/bin/sh
# IVANNA OMEGA SUPREME — ivanna_control.sh v2.0 (FIX cableado socket)
#
# CAMBIO CLAVE respecto v1.1:
#   v1.1 usaba SOCKET=/dev/socket/ivanna_omega — una ruta del filesystem
#   que NUNCA existe: ivanna_daemon publica en abstract namespace
#   (@omega_daemon_socket, ver app/src/main/cpp/daemon/ivanna_daemon.cpp:36
#   y service.sh:36 "--socket @omega_daemon_socket"). Consecuencia:
#   TODO comando caía en el primer `[ -e $SOCKET ]` y terminaba en el
#   fallback setprop ivanna.pending_cmd que ningún consumidor lee →
#   ningún comando llegaba al daemon.
#
#   v2.0:
#     - Probe real via /proc/net/unix (tabla de sockets del kernel).
#       Ahí sí aparece el abstract socket como "@omega_daemon_socket".
#     - Send via `nc -U` en abstract SI busybox nc lo soporta; se
#       detecta por probe corto contra el propio socket. Si nc no soporta
#       abstract o no existe, se marca claramente "no_transport" en vez
#       de fingir OK con setprop fantasma.
#     - Sin dependencia de `su`.

SOCKET_NAME="omega_daemon_socket"        # sin @, para grep en /proc/net/unix
SOCKET_ABS="@${SOCKET_NAME}"             # forma que acepta nc -U compatible
TIMEOUT=2
LOG=/data/adb/ivanna_control.log
log() { echo "[$(date '+%H:%M:%S')] ctrl: $1" >> "$LOG" 2>/dev/null; }

# Devuelve 0 si el socket abstract está listo (kernel lo tiene bindeado).
# Lee /proc/net/unix; los abstract aparecen con prefijo "@" en la
# columna Path.
socket_alive() {
    [ -r /proc/net/unix ] || return 1
    grep -q " @${SOCKET_NAME}$" /proc/net/unix 2>/dev/null
}

have_nc() { command -v nc >/dev/null 2>&1; }

# Cache del resultado de nc_supports_abstract() — se calcula una sola
# vez por invocación del script (evita la doble conexión al daemon que
# antes ocurría en cada send_command: 1x probe + 1x comando real).
_NC_ABS_TESTED=0
_NC_ABS_OK=0

# Detecta si el nc disponible soporta AF_UNIX abstract namespace.
# Usa PING (comando texto válido desde v2.1) al socket ya conocido vivo.
# Resultado cacheado: el probe solo conecta UNA vez por invocación del script.
nc_supports_abstract() {
    if [ "$_NC_ABS_TESTED" -eq 1 ]; then
        [ "$_NC_ABS_OK" -eq 1 ]; return
    fi
    _NC_ABS_TESTED=1
    have_nc || { _NC_ABS_OK=0; return 1; }
    if echo "PING" | timeout 1 nc -U "$SOCKET_ABS" >/dev/null 2>&1; then
        _NC_ABS_OK=1; return 0
    fi
    _NC_ABS_OK=0; return 1
}

send_command() {
    CMD="$1"

    if ! socket_alive; then
        log "ERROR: @$SOCKET_NAME no publicado — daemon no arrancó (ver service.sh log)"
        echo "no_daemon"
        return 1
    fi

    if ! nc_supports_abstract; then
        log "AVISO: nc no soporta AF_UNIX abstract en este device — CLI shell no puede escribir. Use la app o adb-forward + LocalSocket."
        echo "no_transport"
        return 2
    fi

    RESP=$(echo "$CMD" | timeout "$TIMEOUT" nc -U "$SOCKET_ABS" 2>/dev/null)
    RC=$?
    if [ $RC -eq 0 ] && [ -n "$RESP" ]; then
        log "CMD=$CMD → RESP=$RESP"
        echo "$RESP"
        return 0
    else
        log "CMD=$CMD → sin respuesta (rc=$RC)"
        echo "no_response"
        return 3
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
    probe)      # Estado del transport, sin efecto lateral
                if socket_alive; then
                    if nc_supports_abstract; then
                        R=$(echo "PING" | timeout 1 nc -U "$SOCKET_ABS" 2>/dev/null)
                        [ -n "$R" ] && echo "alive" || echo "socket_open_no_response"
                    else
                        echo "socket_open_no_nc"
                    fi
                else
                    echo "no_daemon"
                fi ;;
    *)
        cat <<HELP
IVANNA OMEGA SUPREME — Control CLI v2.0
Socket: @${SOCKET_NAME} (abstract namespace)
Uso:
  $0 status               Estado del daemon
  $0 preset <nombre>      Flat|Warm|Bright|Punch|Spatial|Heavy|Vocal|Bass
  $0 volume <0.0-1.0>     Volumen master
  $0 bypass on|off        Bypass DSP
  $0 concert on|off       Modo Concierto (Spatial + reverb)
  $0 telemetry            Métricas
  $0 reload               Releer parámetros
  $0 probe                Estado transport (alive|socket_open_no_nc|no_daemon)
HELP
        exit 0 ;;
esac
