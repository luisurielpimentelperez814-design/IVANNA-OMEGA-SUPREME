#!/system/bin/sh
# IVANNA OMEGA SUPREME — concert_mode.sh v1.1
MODDIR="${1:-$(dirname "$0")}"
CONTROL="$MODDIR/ivanna_control.sh"
LOG=/data/adb/ivanna_concert.log
log() { echo "[$(date '+%H:%M:%S')] concert: $1" >> "$LOG" 2>/dev/null; }

case "$2" in
    on|"")
        log "activando"; "$CONTROL" concert on; setprop ivanna.concert_mode 1
        echo "✅ Modo Concierto ON" ;;
    off)
        log "desactivando"; "$CONTROL" concert off; setprop ivanna.concert_mode 0
        echo "✅ Modo Concierto OFF" ;;
    *) echo "Uso: concert_mode.sh [moddir] on|off" ;;
esac
