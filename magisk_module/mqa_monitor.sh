#!/system/bin/sh
# IVANNA OMEGA SUPREME — mqa_monitor.sh v1.2 (PATCH)
# - No arranca si el daemon no está vivo (evita loop inútil)
# - dumpsys opcional; sale limpio si no está

DRY_RUN=0
SELFTEST=0
POSITIONAL=""
for arg in "$@"; do
    case "$arg" in
        --dry-run)  DRY_RUN=1 ;;
        --selftest) SELFTEST=1 ;;
        --help|-h)  sed -n '2,10p' "$0"; exit 0 ;;
        *) POSITIONAL="$POSITIONAL $arg" ;;
    esac
done
# shellcheck disable=SC2086
set -- $POSITIONAL

MODDIR="${1:-$(dirname "$0")}"
CONTROL="$MODDIR/ivanna_control.sh"
LOG=/data/adb/ivanna_mqa.log
[ "$SELFTEST" -eq 1 ] && [ ! -w "$(dirname "$LOG")" 2>/dev/null ] && LOG="${TMPDIR:-/tmp}/ivanna_mqa_selftest.log"
log() { echo "[$(date '+%H:%M:%S')] mqa: $1" >> "$LOG" 2>/dev/null; }

command -v dumpsys >/dev/null 2>&1 || { log "dumpsys ausente — saliendo"; exit 1; }
[ "$DRY_RUN" -eq 0 ] && [ "$SELFTEST" -eq 0 ] && [ ! -x "$CONTROL" ] && { log "ctrl no ejecutable — saliendo"; exit 1; }

# Espera daemon vivo hasta 30s; si nunca aparece, no arranca loop
if [ "$SELFTEST" -eq 0 ] && [ "$DRY_RUN" -eq 0 ]; then
    tries=0
    while [ $tries -lt 15 ]; do
        [ -e /dev/socket/ivanna_omega ] && break
        sleep 2
        tries=$((tries+1))
    done
    if [ ! -e /dev/socket/ivanna_omega ]; then
        log "socket nunca apareció — modo app-only, saliendo"
        exit 0
    fi
fi

send_preset() {
    if [ "$DRY_RUN" -eq 1 ]; then log "(dry-run) preset -> $1"; return 0; fi
    "$CONTROL" preset "$1" >/dev/null 2>&1 && log "preset -> $1"
}

detect_preset() {
    ACTIVE_PKG=$(dumpsys media_session 2>/dev/null | grep "package=" | head -1 | sed 's/.*package=//;s/ .*//')
    if [ -z "$ACTIVE_PKG" ]; then
        ACTIVE_PKG=$(dumpsys audio 2>/dev/null | grep "AudioTrack" -A2 | grep "package" | head -1 | awk '{print $2}')
    fi
    TARGET_PRESET=""
    case "$ACTIVE_PKG" in
        com.tidal.android|com.qobuz.music|com.amazon.buick)                    TARGET_PRESET="Flat" ;;
        com.spotify.music|com.apple.android.music|com.google.android.apps.youtube.music) TARGET_PRESET="Warm" ;;
        com.google.android.youtube|com.google.android.tv)                       TARGET_PRESET="Spatial" ;;
        "" ) : ;;
        *)  IS_GAME=$(dumpsys package "$ACTIVE_PKG" 2>/dev/null | grep -c "CATEGORY_GAME" || echo 0)
            [ "$IS_GAME" -gt 0 ] && TARGET_PRESET="Punch" ;;
    esac
    echo "$ACTIVE_PKG|$TARGET_PRESET"
}

if [ "$SELFTEST" -eq 1 ]; then
    r=$(detect_preset); pkg=$(echo "$r"|cut -d'|' -f1); pre=$(echo "$r"|cut -d'|' -f2)
    echo "[selftest] pkg='$pkg' preset='$pre'"
    [ -n "$pkg" ] && exit 0 || exit 1
fi

LAST=""
log "monitor iniciado (dry_run=$DRY_RUN)"
while true; do
    r=$(detect_preset); PKG=$(echo "$r"|cut -d'|' -f1); PRE=$(echo "$r"|cut -d'|' -f2)
    if [ -n "$PRE" ] && [ "$PRE" != "$LAST" ]; then
        log "app=$PKG -> $PRE"
        send_preset "$PRE"
        LAST="$PRE"
    fi
    sleep 5
done
