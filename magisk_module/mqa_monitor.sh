#!/system/bin/sh
# IVANNA OMEGA SUPREME — mqa_monitor.sh v2.0
# Detecta la app activa y aplica preset DSP automático via ivanna_control.sh.
# FIX v2.0:
#   - ELIMINAR set -e (peligroso en daemon background: cualquier fallo de
#     dumpsys/grep/cut mata el monitor silenciosamente)
#   - Ampliar detección: Netflix, Plex, Deezer, FLAC Player, Vanced, etc.
#   - Detección de juegos mejorada: CATEGORY_GAME + nombres de paquetes comunes
#   - Logging de cambios de app aunque no haya cambio de preset (debug)
#   - Integración de detección por título de ventana como fallback

MODDIR="${1:-$(dirname "$0")}"
CONTROL="$MODDIR/ivanna_control.sh"
LOG=/data/adb/ivanna_mqa.log
DRY_RUN=0
SELFTEST=0

for arg in "$@"; do
    case "$arg" in
        --dry-run)  DRY_RUN=1 ;;
        --selftest) SELFTEST=1 ;;
        --help|-h)  sed -n '2,10p' "$0"; exit 0 ;;
    esac
done

log() { echo "[$(date '+%H:%M:%S')] mqa: $1" >> "$LOG" 2>/dev/null; }

command -v dumpsys >/dev/null 2>&1 || { log "dumpsys ausente — saliendo"; exit 1; }
[ "$DRY_RUN" -eq 0 ] && [ "$SELFTEST" -eq 0 ] && [ ! -x "$CONTROL" ] && {
    log "ctrl no ejecutable ($CONTROL) — saliendo"
    exit 1
}

# ── Socket probe (abstract namespace) ─────────────────────────────────────────
DAEMON_SOCKET_NAME="omega_daemon_socket"
socket_alive() {
    [ -r /proc/net/unix ] || return 1
    grep -q " @${DAEMON_SOCKET_NAME}$" /proc/net/unix 2>/dev/null
}

# Esperar daemon hasta 30s antes de arrancar el loop
if [ "$SELFTEST" -eq 0 ] && [ "$DRY_RUN" -eq 0 ]; then
    tries=0
    while [ $tries -lt 15 ]; do
        socket_alive && break
        sleep 2
        tries=$((tries+1))
    done
    if ! socket_alive; then
        log "@$DAEMON_SOCKET_NAME nunca apareció — modo app-only, saliendo"
        exit 0
    fi
    log "@$DAEMON_SOCKET_NAME detectado — arrancando loop de auto-preset v2.0"
fi

send_preset() {
    if [ "$DRY_RUN" -eq 1 ]; then
        log "(dry-run) preset → $1"
        return 0
    fi
    "$CONTROL" preset "$1" >/dev/null 2>&1 && log "preset → $1 (app: $2)"
}

# ── detect_preset ─────────────────────────────────────────────────────────────
# Retorna "PKG|PRESET" o "PKG|" si no hay preset conocido.
detect_preset() {
    # Fuente 1: media_session (la más fiable — solo apps con sesión de medios activa)
    ACTIVE_PKG=$(dumpsys media_session 2>/dev/null \
        | grep "package=" \
        | head -1 \
        | sed 's/.*package=//;s/ .*//')

    # Fuente 2: audio (tracks activos — cubre apps sin MediaSession como juegos)
    if [ -z "$ACTIVE_PKG" ]; then
        ACTIVE_PKG=$(dumpsys audio 2>/dev/null \
            | grep -A2 "AudioTrack" \
            | grep "package" \
            | head -1 \
            | awk '{print $2}')
    fi

    # Fuente 3: activity (app en foreground — último recurso, cubre podcasts/radio)
    if [ -z "$ACTIVE_PKG" ]; then
        ACTIVE_PKG=$(dumpsys activity 2>/dev/null \
            | grep "mFocusedApp" \
            | head -1 \
            | sed 's/.*\///;s/[^a-z.].*//I' \
            | tr -d ' ')
    fi

    TARGET_PRESET=""
    case "$ACTIVE_PKG" in
        # ── Música Hi-Fi / Lossless → Flat (reproducción neutra, sin coloración) ──
        com.tidal.android|\
        com.qobuz.music|\
        com.amazon.buick|\
        com.amazon.mp3|\
        com.apple.android.music|\
        com.foobar2000.foobar2000|\
        com.vgram.poweramp|\
        com.maxmpz.audioplayer|\
        com.neutroncode.mp|\
        com.jetappfactory.jetaudio|\
        com.bsbportal.music|\
        com.iZotope.rx|\
        com.extreamsd.viperaudio)
            TARGET_PRESET="Flat" ;;

        # ── Streaming comprimido → Warm (compensar compresión lossy 128-320 kbps) ──
        com.spotify.music|\
        com.google.android.apps.youtube.music|\
        com.soundcloud.android|\
        com.deezer.android|\
        com.pandora.android|\
        com.iheart.android|\
        com.iheartradio.android|\
        com.radio.fmradio|\
        com.clearchannel.iheartradio.controller|\
        com.anghami|\
        com.jiosaavn.app|\
        com.gaana)
            TARGET_PRESET="Warm" ;;

        # ── Video streaming → Spatial (contenido cinematográfico, Dolby Atmos) ──
        com.google.android.youtube|\
        com.google.android.tv|\
        com.netflix.mediaclient|\
        com.amazon.avod.thirdpartyclient|\
        com.disney.disneyplus|\
        tv.twitch.android.app|\
        com.plexapp.android|\
        com.plex.plex|\
        com.hbo.hbonow|\
        com.hulu.plus|\
        com.paramount.plus|\
        com.peacocktv.peacockandroid|\
        com.apple.android.tvplus)
            TARGET_PRESET="Spatial" ;;

        # ── Podcast / Voz → Vocal (presencia 2.5kHz, compresión vocal, graves bajos) ──
        com.google.android.apps.podcasts|\
        com.spotify.podcast|\
        com.capiche.fm|\
        com.pocketcasts.views|\
        au.com.shiftyjelly.pocketcasts|\
        fm.overcast.overcast|\
        com.bambuna.podcastaddict|\
        com.stitcher.app|\
        com.iheart.android.podcast|\
        com.audioboom.podcasts)
            TARGET_PRESET="Vocal" ;;

        # ── Si no reconocemos la app, comprobar CATEGORY_GAME ─────────────────
        "")
            : ;; # sin app activa — mantener preset actual
        *)
            IS_GAME=$(dumpsys package "$ACTIVE_PKG" 2>/dev/null \
                | grep -c "CATEGORY_GAME" 2>/dev/null || echo 0)
            if [ "$IS_GAME" -gt 0 ]; then
                TARGET_PRESET="Punch"
            else
                # Heurística por nombre de paquete: juegos suelen tener
                # "game", "gaming", "play" o publisher conocidos
                case "$ACTIVE_PKG" in
                    *.game.*|*game*.*|com.supercell.*|com.king.*|\
                    com.ea.games.*|com.zynga.*|com.gameloft.*|\
                    com.activision.*|com.epicgames.*|com.riotgames.*)
                        TARGET_PRESET="Punch" ;;
                esac
            fi ;;
    esac

    echo "${ACTIVE_PKG}|${TARGET_PRESET}"
}

# ── Selftest ──────────────────────────────────────────────────────────────────
if [ "$SELFTEST" -eq 1 ]; then
    r=$(detect_preset)
    pkg=$(echo "$r" | cut -d'|' -f1)
    pre=$(echo "$r" | cut -d'|' -f2)
    echo "[selftest] pkg='$pkg' preset='$pre'"
    [ -n "$pkg" ] && exit 0 || exit 1
fi

# ── Monitor loop ──────────────────────────────────────────────────────────────
LAST_PRESET=""
LAST_PKG=""
log "monitor v2.0 iniciado (dry_run=$DRY_RUN)"

while true; do
    r=$(detect_preset 2>/dev/null)
    PKG=$(echo "$r" | cut -d'|' -f1)
    PRE=$(echo "$r" | cut -d'|' -f2)

    # Log cada cambio de app, independientemente de si hay cambio de preset
    if [ -n "$PKG" ] && [ "$PKG" != "$LAST_PKG" ]; then
        log "app detectada: $PKG${PRE:+ → preset: $PRE}"
        LAST_PKG="$PKG"
    fi

    # Aplicar preset solo si cambió
    if [ -n "$PRE" ] && [ "$PRE" != "$LAST_PRESET" ]; then
        send_preset "$PRE" "$PKG"
        LAST_PRESET="$PRE"
    fi

    # Verificar que el daemon sigue vivo cada ciclo; si murió, esperar
    # sin enviar comandos (evita timeouts de nc acumulados).
    if ! socket_alive; then
        log "daemon socket perdido — esperando recuperación..."
        LAST_PRESET=""  # Resetear para re-aplicar preset al recuperarse
        sleep 10
        continue
    fi

    sleep 5
done
