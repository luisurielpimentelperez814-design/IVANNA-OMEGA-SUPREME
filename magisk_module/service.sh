#!/system/bin/sh
# IVANNA OMEGA SUPREME v6.0 - Magisk Realtime Daemon Service
MODDIR=${0%/*}
DAEMON_BIN="$MODDIR/omega_daemon"
LOGFILE="/data/adb/ivanna_omega/daemon.log"

mkdir -p /data/adb/ivanna_omega

echo "[$(date)] Starting IVANNA OMEGA Daemon Service..." >> "$LOGFILE"

# Log rotation > 10MB
if [ -f "$LOGFILE" ] && [ $(stat -c%s "$LOGFILE") -gt 10485760 ]; then
    mv "$LOGFILE" "${LOGFILE}.old"
fi

if [ -f "$DAEMON_BIN" ]; then
    chmod 755 "$DAEMON_BIN"
    while true; do
        echo "[$(date)] Launching $DAEMON_BIN" >> "$LOGFILE"
        "$DAEMON_BIN" >> "$LOGFILE" 2>&1
        echo "[$(date)] Daemon crashed or exited. Restarting in 1s..." >> "$LOGFILE"
        sleep 1
    done
else
    echo "[$(date)] ERROR: $DAEMON_BIN not found!" >> "$LOGFILE"
fi
