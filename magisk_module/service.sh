#!/system/bin/sh

MODDIR=${0%/*}

DAEMON="$MODDIR/system/bin/ivanna_daemon"
STATE="/data/adb/ivanna_omega"
LOG="$STATE/daemon.log"

mkdir -p "$STATE"

chmod 755 "$DAEMON" 2>/dev/null
chmod 755 "$MODDIR/service.sh" 2>/dev/null

if pidof ivanna_daemon >/dev/null 2>&1; then
    echo "$(date) daemon ya estaba activo" >> "$LOG"
    exit 0
fi

rm -f "$STATE/daemon.pid"

echo "$(date) iniciando ivanna_daemon" >> "$LOG"

"$DAEMON" >> "$LOG" 2>&1 &

PID=$!
echo "$PID" > "$STATE/daemon.pid"

sleep 3

if kill -0 "$PID" 2>/dev/null; then
    echo "$(date) daemon activo PID=$PID" >> "$LOG"
else
    echo "$(date) ERROR daemon murió al iniciar" >> "$LOG"
fi
