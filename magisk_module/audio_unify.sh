#!/system/bin/sh
# IVANNA OMEGA SUPREME — safe Moto G85 audio-module consolidation
# This changes only Magisk module disable markers for the NEXT boot.
# It does not edit vendor/init files, set LD_PRELOAD, restart the HAL,
# change SELinux, or claim physical PAL/ASRC activation.

MODROOT="${IVANNA_MODROOT:-/data/adb/modules}"
STATE="${IVANNA_AUDIO_UNIFY_STATE:-/data/adb/ivanna_omega/audio_unification}"
LOG="$STATE/migration.log"
LEGACY_MODULES="ivanna_audio_override ivanna_hal_fix ivanna_audio_probe omega_pal_presence_probe"
MAIN_MODULE="ivanna_omega_supreme"

say() { echo "$*"; }
log() { mkdir -p "$STATE" 2>/dev/null; echo "$(date '+%F %T') $*" >> "$LOG" 2>/dev/null; }

status() {
    say "IVANNA audio consolidation status"
    [ -d "$MODROOT/$MAIN_MODULE" ] || say "WARNING: $MAIN_MODULE is not installed"
    for m in $LEGACY_MODULES; do
        if [ ! -d "$MODROOT/$m" ]; then
            say "$m: absent"
        elif [ -f "$MODROOT/$m/disable" ]; then
            say "$m: disabled"
        else
            say "$m: ENABLED (legacy module; disable before next boot)"
        fi
    done
    say "Current running HAL is not changed by this command."
}

apply() {
    [ -d "$MODROOT/$MAIN_MODULE" ] || {
        say "ERROR: $MAIN_MODULE is not installed; refusing migration"
        return 1
    }
    mkdir -p "$STATE" || return 1
    if [ -e "$STATE/changed_modules.list" ]; then
        say "Migration record already exists. Run '$0 status' or '$0 rollback' first."
        return 1
    fi
    : > "$STATE/changed_modules.list" || return 1
    for m in $LEGACY_MODULES; do
        d="$MODROOT/$m"
        [ -d "$d" ] || continue
        if [ -f "$d/disable" ]; then
            say "$m: already disabled; leaving its prior state unchanged"
            continue
        fi
        if touch "$d/disable"; then
            echo "$m" >> "$STATE/changed_modules.list"
            log "disabled $m for next boot"
            say "$m: disabled for next boot"
        else
            say "ERROR: could not disable $m; rolling back changes made in this run"
            rollback
            return 1
        fi
    done
    say "Done. Reboot is required for Magisk to unmount legacy modules."
    say "No HAL restart or LD_PRELOAD injection was attempted."
    say "Rollback: su -c sh $MODROOT/$MAIN_MODULE/audio_unify.sh rollback"
}

rollback() {
    list="$STATE/changed_modules.list"
    if [ ! -f "$list" ]; then
        say "No migration record; nothing changed by this script."
        return 0
    fi
    rc=0
    while IFS= read -r m; do
        case " $LEGACY_MODULES " in *" $m "*) ;; *) say "Ignoring unknown record: $m"; continue ;; esac
        d="$MODROOT/$m"
        if [ -d "$d" ] && [ -f "$d/disable" ]; then
            rm -f "$d/disable" || rc=1
            if [ "$rc" -eq 0 ]; then
                log "re-enabled $m (rollback)"
                say "$m: re-enabled for next boot"
            fi
        else
            say "$m: no disable marker to remove"
        fi
    done < "$list"
    if [ "$rc" -eq 0 ]; then
        rm -f "$list"
        say "Rollback markers restored. Reboot is required for Magisk to remount modules."
    else
        say "ERROR: one or more markers could not be restored; inspect $list"
    fi
    return "$rc"
}

case "${1:-status}" in
    status) status ;;
    apply) apply ;;
    rollback) rollback ;;
    *) say "Usage: $0 {status|apply|rollback}"; exit 2 ;;
esac
