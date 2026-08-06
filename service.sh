#!/system/bin/sh

MODDIR=${0%/*}

DAEMON_BIN="$MODDIR/system/bin/ivanna_daemon"
LOGFILE="/data/adb/ivanna_omega/daemon.log"

mkdir -p /data/adb/ivanna_omega

echo "[$(date)] IVANNA OMEGA SERVICE START" >> "$LOGFILE"

if [ ! -x "$DAEMON_BIN" ]; then
    echo "[$(date)] ERROR: daemon missing $DAEMON_BIN" >> "$LOGFILE"
    exit 1
fi

chmod 755 "$DAEMON_BIN"

# Evitar instancias duplicadas
PID=$(pidof ivanna_daemon)

if [ -n "$PID" ]; then
    echo "[$(date)] Existing daemon PID=$PID killing" >> "$LOGFILE"
    kill -9 $PID
    sleep 2
fi


while true
do
    echo "[$(date)] Launching daemon" >> "$LOGFILE"

    "$DAEMON_BIN" \
        --socket "@omega_daemon_socket" \
        >> "$LOGFILE" 2>&1

    EXIT=$?

    echo "[$(date)] daemon exited code=$EXIT" >> "$LOGFILE"

    setprop persist.ivanna.daemon_active 0

    sleep 3
done
