#!/system/bin/sh
# IVANNA OMEGA SUPREME — concert_mode.sh
# Activa/desactiva modo Concierto a nivel de sistema.

MODDIR="${1:-$(dirname "$0")}"
CONTROL="$MODDIR/ivanna_control.sh"
LOG=/data/adb/ivanna_concert.log
log() { echo "[$(date '+%H:%M:%S')] concert: $1" >> "$LOG"; }

case "$2" in
    on|"")
        log "activando"
        "$CONTROL" concert on
        setprop ivanna.concert_mode 1
        log "activado"
        echo "✅ Modo Concierto ON"
        ;;
    off)
        log "desactivando"
        "$CONTROL" concert off
        setprop ivanna.concert_mode 0
        log "desactivado"
        echo "✅ Modo Concierto OFF"
        ;;
    *)
        echo "Uso: concert_mode.sh [moddir] on|off"
        ;;
esac
