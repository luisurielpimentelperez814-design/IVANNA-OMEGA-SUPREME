#!/system/bin/sh
# Supervisor del daemon: service.sh solo arranca el daemon UNA vez; si muere despues
# (OOM, SIGSEGV, socket perdido) nada lo relanzaba hasta el siguiente reinicio y la app
# quedaba sin plano de control. Este lazo lo detecta (proceso Y socket abstracto, no solo PID)
# y relanza via service.sh, con backoff y tope para no entrar en bucle de caidas.
MODDIR=${0%/*}
STATE="/data/adb/ivanna_omega"
LOG="$STATE/daemon.log"
mkdir -p "$STATE" 2>/dev/null
echo $$ > "$STATE/supervisor.pid"

sleep 60   # deja terminar el arranque normal de service.sh y del sistema

FAILS=0
WIN_START=$(date +%s)
BACKOFF=5
while :; do
    sleep "$BACKOFF"
    if [ -f "$MODDIR/.safe_mode" ] || [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
        echo "$(date) supervisor: modulo en safe_mode/disable/remove — detenido" >> "$LOG"
        rm -f "$STATE/supervisor.pid"
        exit 0
    fi
    if pidof ivanna_daemon >/dev/null 2>&1 && grep -aq omega_daemon_socket /proc/net/unix 2>/dev/null; then
        BACKOFF=5
        continue
    fi
    NOW=$(date +%s)
    if [ $((NOW - WIN_START)) -gt 3600 ]; then FAILS=0; WIN_START=$NOW; fi
    FAILS=$((FAILS + 1))
    if [ "$FAILS" -gt 8 ]; then
        echo "$(date) supervisor: 8 relanzamientos en 1 h — me detengo (revisar daemon.log)" >> "$LOG"
        rm -f "$STATE/supervisor.pid"
        exit 0
    fi
    echo "$(date) supervisor: daemon caido o sin socket — relanzando (intento $FAILS)" >> "$LOG"
    IVANNA_SUPERVISED=1 sh "$MODDIR/service.sh" >> "$LOG" 2>&1
    BACKOFF=$((BACKOFF * 2)); [ "$BACKOFF" -gt 60 ] && BACKOFF=60
done
