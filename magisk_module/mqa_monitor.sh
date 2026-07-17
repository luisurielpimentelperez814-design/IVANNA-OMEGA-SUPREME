#!/system/bin/sh
# IVANNA OMEGA SUPREME — mqa_monitor.sh v1.1
# Monitorea sesiones de audio activas y envia preset optimo al daemon.
# Corre en background desde service.sh.
#
# Modos:
#   mqa_monitor.sh [MODDIR]                → modo normal (loop infinito).
#   mqa_monitor.sh [MODDIR] --dry-run      → misma logica pero NO envia
#                                             presets al daemon; solo loguea
#                                             lo que HARIA. Util para validar
#                                             la deteccion en un dispositivo
#                                             sin tocar el audio.
#   mqa_monitor.sh --selftest              → una sola pasada de deteccion,
#                                             imprime resultado por stdout y
#                                             sale (exit 0 si detecto app,
#                                             exit 1 si no). No requiere root
#                                             para el chequeo de dependencias.

# ── Parseo de flags ──────────────────────────────────────────────────────────
DRY_RUN=0
SELFTEST=0
POSITIONAL=""
for arg in "$@"; do
    case "$arg" in
        --dry-run)  DRY_RUN=1 ;;
        --selftest) SELFTEST=1 ;;
        --help|-h)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        *) POSITIONAL="$POSITIONAL $arg" ;;
    esac
done
# shellcheck disable=SC2086
set -- $POSITIONAL

MODDIR="${1:-$(dirname "$0")}"
CONTROL="$MODDIR/ivanna_control.sh"
LOG=/data/adb/ivanna_mqa.log

# En --selftest usamos un log temporal si /data/adb no es escribible
# (typicamente cuando se corre sin root para validar el script).
if [ "$SELFTEST" -eq 1 ] && [ ! -w "$(dirname "$LOG")" 2>/dev/null ]; then
    LOG="${TMPDIR:-/tmp}/ivanna_mqa_selftest.log"
fi

log() { echo "[$(date '+%H:%M:%S')] mqa: $1" >> "$LOG" 2>/dev/null; }

# ── Validacion de dependencias ───────────────────────────────────────────────
have_dumpsys=1
command -v dumpsys >/dev/null 2>&1 || have_dumpsys=0

if [ "$SELFTEST" -eq 1 ]; then
    echo "[selftest] MODDIR=$MODDIR"
    echo "[selftest] CONTROL=$CONTROL (exists=$([ -x "$CONTROL" ] && echo yes || echo no))"
    echo "[selftest] dumpsys disponible=$have_dumpsys"
    echo "[selftest] LOG=$LOG"
fi

if [ "$have_dumpsys" -eq 0 ]; then
    log "dumpsys no disponible — monitor no puede detectar apps activas. Saliendo."
    [ "$SELFTEST" -eq 1 ] && echo "[selftest] FAIL: dumpsys ausente"
    exit 1
fi

if [ "$DRY_RUN" -eq 0 ] && [ "$SELFTEST" -eq 0 ] && [ ! -x "$CONTROL" ]; then
    log "ivanna_control.sh no encontrado o no ejecutable en $CONTROL. Saliendo."
    exit 1
fi

send_preset() {
    if [ "$DRY_RUN" -eq 1 ]; then
        log "(dry-run) preset -> $1"
        return 0
    fi
    [ -x "$CONTROL" ] && "$CONTROL" preset "$1" 2>/dev/null && log "preset -> $1"
}

# ── Deteccion (funcion reutilizable por selftest y loop) ─────────────────────
detect_preset() {
    ACTIVE_PKG=$(dumpsys media_session 2>/dev/null \
        | grep "package=" \
        | head -1 \
        | sed 's/.*package=//;s/ .*//')

    if [ -z "$ACTIVE_PKG" ]; then
        ACTIVE_PKG=$(dumpsys audio 2>/dev/null \
            | grep "AudioTrack" -A2 \
            | grep "package" \
            | head -1 \
            | awk '{print $2}')
    fi

    TARGET_PRESET=""
    case "$ACTIVE_PKG" in
        com.tidal.android|com.qobuz.music|com.amazon.buick)
            TARGET_PRESET="Flat" ;;
        com.spotify.music|com.apple.android.music|com.google.android.apps.youtube.music)
            TARGET_PRESET="Warm" ;;
        com.google.android.youtube|com.google.android.tv)
            TARGET_PRESET="Spatial" ;;
        "" ) : ;;
        *)
            IS_GAME=$(dumpsys package "$ACTIVE_PKG" 2>/dev/null \
                | grep -c "CATEGORY_GAME" || echo 0)
            [ "$IS_GAME" -gt 0 ] && TARGET_PRESET="Punch"
            ;;
    esac
    echo "$ACTIVE_PKG|$TARGET_PRESET"
}

# ── --selftest: una pasada y salir ───────────────────────────────────────────
if [ "$SELFTEST" -eq 1 ]; then
    result=$(detect_preset)
    pkg=$(echo "$result" | cut -d'|' -f1)
    preset=$(echo "$result" | cut -d'|' -f2)
    echo "[selftest] active_pkg='${pkg}' target_preset='${preset}'"
    if [ -n "$pkg" ]; then
        echo "[selftest] OK: se detecto app activa"
        exit 0
    else
        echo "[selftest] WARN: no se detecto ninguna app con sesion de audio activa"
        exit 1
    fi
fi

# ── Loop principal ───────────────────────────────────────────────────────────
LAST_PRESET=""
log "monitor iniciado (dry_run=$DRY_RUN)"

while true; do
    result=$(detect_preset)
    ACTIVE_PKG=$(echo "$result" | cut -d'|' -f1)
    TARGET_PRESET=$(echo "$result" | cut -d'|' -f2)

    if [ -n "$TARGET_PRESET" ] && [ "$TARGET_PRESET" != "$LAST_PRESET" ]; then
        log "app=$ACTIVE_PKG -> $TARGET_PRESET"
        send_preset "$TARGET_PRESET"
        LAST_PRESET="$TARGET_PRESET"
    fi

    sleep 5
done
