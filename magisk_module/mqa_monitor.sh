#!/system/bin/sh
# IVANNA OMEGA SUPREME — mqa_monitor.sh v1.0
# Monitorea sesiones de audio activas y envía preset óptimo al daemon.
# Corre en background desde service.sh.

MODDIR="${1:-$(dirname "$0")}"
CONTROL="$MODDIR/ivanna_control.sh"
LOG=/data/adb/ivanna_mqa.log
log() { echo "[$(date '+%H:%M:%S')] mqa: $1" >> "$LOG"; }

send_preset() {
    [ -x "$CONTROL" ] && "$CONTROL" preset "$1" 2>/dev/null && log "preset → $1"
}

LAST_PRESET=""

log "monitor iniciado"

while true; do
    # ── Detectar app activa con sesión de audio ────────────────────────────
    ACTIVE_PKG=""

    # Método 1: dumpsys media_session (más confiable)
    if command -v dumpsys >/dev/null 2>&1; then
        ACTIVE_PKG=$(dumpsys media_session 2>/dev/null \
            | grep "package=" \
            | head -1 \
            | sed 's/.*package=//;s/ .*//')
    fi

    # Método 2: fallback por actividad en audio (AudioTrack activo)
    if [ -z "$ACTIVE_PKG" ] && command -v dumpsys >/dev/null 2>&1; then
        ACTIVE_PKG=$(dumpsys audio 2>/dev/null \
            | grep "AudioTrack" -A2 \
            | grep "package" \
            | head -1 \
            | awk '{print $2}')
    fi

    # ── Mapeo de app → preset óptimo ──────────────────────────────────────
    TARGET_PRESET=""
    case "$ACTIVE_PKG" in
        # Hi-Res / Lossless: flat reference (no colorear)
        com.tidal.android|com.qobuz.music|com.amazon.buick)
            TARGET_PRESET="Flat"
            ;;
        # Spotify / Apple Music / YouTube Music: Warm (compensar compresión)
        com.spotify.music|com.apple.android.music|com.google.android.apps.youtube.music)
            TARGET_PRESET="Warm"
            ;;
        # YouTube / videos: Spatial (mejor experiencia estéreo)
        com.google.android.youtube|com.google.android.tv)
            TARGET_PRESET="Spatial"
            ;;
        # Juegos: Punch (impacto + presencia)
        *)
            # Detectar si es un juego (categoría GAME en PackageManager)
            IS_GAME=$(dumpsys package "$ACTIVE_PKG" 2>/dev/null \
                | grep -c "CATEGORY_GAME" || echo 0)
            [ "$IS_GAME" -gt 0 ] && TARGET_PRESET="Punch"
            ;;
    esac

    # ── Enviar preset solo si cambió ───────────────────────────────────────
    if [ -n "$TARGET_PRESET" ] && [ "$TARGET_PRESET" != "$LAST_PRESET" ]; then
        log "app=$ACTIVE_PKG → $TARGET_PRESET"
        send_preset "$TARGET_PRESET"
        LAST_PRESET="$TARGET_PRESET"
    fi

    sleep 5
done
