#!/system/bin/sh
# IVANNA OMEGA SUPREME — service.sh (Googleplex Perfect Edition)
# Supremacía Acústica Absoluta con latencia ultra-baja y zero-drop frames.

MODDIR="${0%/*}"
DAEMON_BIN="$MODDIR/system/bin/ivanna_daemon"
PID_FILE=/data/adb/ivanna_daemon.pid
MQA_PID_FILE=/data/adb/ivanna_mqa.pid
LOGFILE=/data/adb/ivanna_omega.log
LAST_OK=/data/adb/ivanna_omega_last_boot_ok
BACKOFF=1
BACKOFF_MAX=60
SESSION_CRASH_COUNT=0
SESSION_CRASH_MAX=5
MQA_PID=""
DAEMON_PID=""

# ── Configuraciones de Rendimiento del Kernel ─────────────────────────────────
optimize_kernel_parameters() {
    # Evitar swap y page cache limits
    echo 100 > /proc/sys/vm/swappiness 2>/dev/null
    # OOM Score Adjustment para el daemon (inmortalidad)
    # se aplicará al PID después.
}

mqa_kill_previous() {
    if [ -f "$MQA_PID_FILE" ]; then
        OLD_MQA=$(cat "$MQA_PID_FILE" 2>/dev/null)
        if [ -n "$OLD_MQA" ] && kill -0 "$OLD_MQA" 2>/dev/null; then
            if ps -p "$OLD_MQA" -o comm= 2>/dev/null | grep -q "mqa_monitor\|sh"; then
                kill -9 "$OLD_MQA" 2>/dev/null
                echo "[$(date)] mqa_monitor previo PID=$OLD_MQA eliminado." >> "$LOGFILE"
            fi
        fi
        rm -f "$MQA_PID_FILE"
    fi
    if [ -n "$MQA_PID" ] && kill -0 "$MQA_PID" 2>/dev/null; then
        kill -9 "$MQA_PID" 2>/dev/null
    fi
    MQA_PID=""
}

cleanup() {
    setprop persist.ivanna.daemon_active 0
    [ -n "$DAEMON_PID" ] && kill -9 "$DAEMON_PID" 2>/dev/null
    mqa_kill_previous
    rm -f "$PID_FILE" "$MQA_PID_FILE"
    echo "[$(date)] service.sh exit — daemon_active=0" >> "$LOGFILE"
}
trap cleanup EXIT HUP INT TERM

daemon_kill_orphans() {
    if [ -f "$PID_FILE" ]; then
        OLD_PID=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
            kill "$OLD_PID" 2>/dev/null
            sleep 0.3
            kill -0 "$OLD_PID" 2>/dev/null && kill -9 "$OLD_PID" 2>/dev/null
            echo "[$(date)] Daemon huérfano PID=$OLD_PID neutralizado." >> "$LOGFILE"
        fi
        rm -f "$PID_FILE"
    fi
    ORPHANS=$(pidof ivanna_daemon 2>/dev/null)
    if [ -n "$ORPHANS" ]; then
        kill $ORPHANS 2>/dev/null
        sleep 0.3
        ORPHANS_LEFT=$(pidof ivanna_daemon 2>/dev/null)
        [ -n "$ORPHANS_LEFT" ] && kill -9 $ORPHANS_LEFT 2>/dev/null
        echo "[$(date)] Daemon huérfanos (pidof) eliminados: $ORPHANS" >> "$LOGFILE"
    fi
}

optimize_kernel_parameters

while true; do
    daemon_kill_orphans
    
    echo "[$(date)] Iniciando $DAEMON_BIN en modo Supremacía Acústica..." >> "$LOGFILE"
    START_TS=$(date +%s)
    
    # Lanzar el daemon
    "$DAEMON_BIN" --socket "@omega_daemon_socket" --realtime >> "$LOGFILE" 2>&1 &
    DAEMON_PID=$!
    echo "$DAEMON_PID" > "$PID_FILE"
    
    # Elevación a tiempo real (SCHED_FIFO / Inmortalidad OOM)
    echo -1000 > /proc/$DAEMON_PID/oom_score_adj 2>/dev/null || true
    chrt -f -p 99 $DAEMON_PID 2>/dev/null || true

    SOCK_READY=0
    SOCK_TRIES=0
    while [ $SOCK_TRIES -lt 15 ]; do
        sleep 0.5
        kill -0 "$DAEMON_PID" 2>/dev/null || break
        if grep -q " @omega_daemon_socket$" /proc/net/unix 2>/dev/null; then 
            SOCK_READY=1
            break
        fi
        SOCK_TRIES=$((SOCK_TRIES + 1))
    done

    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        if [ "$SOCK_READY" -eq 1 ]; then
            echo "[$(date)] Daemon PID=$DAEMON_PID operacional — abstract socket bindeado." >> "$LOGFILE"
        else
            echo "[$(date)] WARN: Daemon PID=$DAEMON_PID vivo pero socket lento." >> "$LOGFILE"
        fi
        
        setprop persist.ivanna.daemon_active 1
        touch "$LAST_OK"
        
        if [ -f "$MODDIR/mqa_monitor.sh" ] && [ -x "$MODDIR/mqa_monitor.sh" ]; then
            mqa_kill_previous
            "$MODDIR/mqa_monitor.sh" "$MODDIR" >> "$LOGFILE" 2>&1 &
            MQA_PID=$!
            echo "$MQA_PID" > "$MQA_PID_FILE"
            echo -1000 > /proc/$MQA_PID/oom_score_adj 2>/dev/null || true
            echo "[$(date)] MQA Monitor PID=$MQA_PID online." >> "$LOGFILE"
        fi
    else
        echo "[$(date)] ERROR: Daemon colapsó instantáneamente. Reintento en ${BACKOFF}s" >> "$LOGFILE"
        setprop persist.ivanna.daemon_active 0
        rm -f "$PID_FILE"
        sleep "$BACKOFF"
        BACKOFF=$(( BACKOFF * 2 )); [ "$BACKOFF" -gt "$BACKOFF_MAX" ] && BACKOFF=$BACKOFF_MAX
        continue
    fi

    wait "$DAEMON_PID"
    EXIT_CODE=$?
    UPTIME=$(( $(date +%s) - START_TS ))
    
    setprop persist.ivanna.daemon_active 0
    rm -f "$PID_FILE"
    mqa_kill_previous

    if [ "$UPTIME" -ge 30 ]; then
        BACKOFF=2
        SESSION_CRASH_COUNT=0
    else
        BACKOFF=$(( BACKOFF * 2 )); [ "$BACKOFF" -gt "$BACKOFF_MAX" ] && BACKOFF=$BACKOFF_MAX
        SESSION_CRASH_COUNT=$((SESSION_CRASH_COUNT + 1))
        if [ "$SESSION_CRASH_COUNT" -ge "$SESSION_CRASH_MAX" ]; then
            echo "[$(date)] CRÍTICO: Múltiples colapsos de daemon en ciclo corto. Entrando en enfriamiento (600s)." >> "$LOGFILE"
            SESSION_CRASH_COUNT=0
            BACKOFF=600
        fi
    fi
    echo "[$(date)] Daemon terminó (Código: $EXIT_CODE, Uptime: ${UPTIME}s). Reinicio en ${BACKOFF}s." >> "$LOGFILE"
    sleep "$BACKOFF"
done
