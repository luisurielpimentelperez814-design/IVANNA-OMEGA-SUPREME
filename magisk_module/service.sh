#!/system/bin/sh
MODDIR=${0%/*}

# Log Rotation (Max 10 MB limit)
LOG_FILE="/data/adb/ivanna_omega/daemon.log"
if [ -f "$LOG_FILE" ]; then
    LOG_SIZE=$(stat -c%s "$LOG_FILE" 2>/dev/null || echo 0)
    if [ "$LOG_SIZE" -gt 10485760 ]; then
        mv "$LOG_FILE" "${LOG_FILE}.old"
    fi
fi

echo "[MAGISK] Starting IVANNA OMEGA SUPREME v6.0 Service..." >> "$LOG_FILE"

mkdir -p /data/adb/ivanna_omega/profile
mkdir -p /dev/socket
chmod 0777 /dev/socket

# Respawn Loop
while true; do
    if ! pgrep omega_daemon > /dev/null; then
        echo "[MAGISK] Launching omega_daemon..." >> "$LOG_FILE"
        "$MODDIR/omega_daemon" >> "$LOG_FILE" 2>&1 &
    fi
    sleep 5
done
