#!/system/bin/sh
# IVANNA OMEGA SUPREME v6.0 - Magisk Realtime Daemon Service
MODDIR=${0%/*}
DAEMON_BIN="$MODDIR/omega_daemon"
LOGFILE="/data/adb/ivanna_omega/daemon.log"
SOCKET_PATH="/dev/socket/ivanna_omega"

mkdir -p /data/adb/ivanna_omega

echo "[$(date)] Starting IVANNA OMEGA Daemon Service..." >> "$LOGFILE"

# Log rotation > 10MB
if [ -f "$LOGFILE" ] && [ $(stat -c%s "$LOGFILE") -gt 10485760 ]; then
    mv "$LOGFILE" "${LOGFILE}.old"
fi

# Función para limpiar socket antiguo
cleanup_socket() {
    if [ -e "$SOCKET_PATH" ]; then
        rm -f "$SOCKET_PATH" 2>/dev/null
    fi
}

# Función para esperar a que el socket esté listo
wait_for_socket() {
    local max_wait=10
    local waited=0
    while [ $waited -lt $max_wait ]; do
        if [ -e "$SOCKET_PATH" ]; then
            chmod 0666 "$SOCKET_PATH" 2>/dev/null
            return 0
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    echo "[$(date)] ERROR: Socket no apareció después de ${max_wait}s" >> "$LOGFILE"
    return 1
}

if [ -f "$DAEMON_BIN" ]; then
    chmod 755 "$DAEMON_BIN"
    while true; do
        cleanup_socket
        echo "[$(date)] Launching $DAEMON_BIN" >> "$LOGFILE"
        "$DAEMON_BIN" >> "$LOGFILE" 2>&1 &
        DAEMON_PID=$!
        
        # Esperar a que el socket esté listo
        if wait_for_socket; then
            echo "[$(date)] Socket $SOCKET_PATH ready with perms 0666" >> "$LOGFILE"
            setprop persist.ivanna.daemon_active 1
        else
            echo "[$(date)] Failed to start socket — daemon may not be running" >> "$LOGFILE"
            setprop persist.ivanna.daemon_active 0
        fi
        
        wait $DAEMON_PID
        DAEMON_EXIT=$?
        echo "[$(date)] Daemon crashed or exited (code=$DAEMON_EXIT). Restarting in 1s..." >> "$LOGFILE"
        setprop persist.ivanna.daemon_active 0
        sleep 1
    done
else
    echo "[$(date)] ERROR: $DAEMON_BIN not found!" >> "$LOGFILE"
fi
