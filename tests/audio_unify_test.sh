#!/system/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT="$ROOT/magisk_module/audio_unify.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
MODROOT="$TMP/modules"
STATE="$TMP/state"
mkdir -p "$MODROOT/ivanna_omega_supreme"
for m in ivanna_audio_override ivanna_hal_fix ivanna_audio_probe omega_pal_presence_probe; do
    mkdir -p "$MODROOT/$m"
done
# A module disabled before migration must remain disabled after rollback.
touch "$MODROOT/ivanna_audio_probe/disable"

run() {
    IVANNA_MODROOT="$MODROOT" IVANNA_AUDIO_UNIFY_STATE="$STATE" sh "$SCRIPT" "$1"
}

run apply > "$TMP/apply.out"
[ -f "$MODROOT/ivanna_audio_override/disable" ]
[ -f "$MODROOT/ivanna_hal_fix/disable" ]
[ -f "$MODROOT/ivanna_audio_probe/disable" ]
[ -f "$MODROOT/omega_pal_presence_probe/disable" ]
grep -qx 'ivanna_audio_override' "$STATE/changed_modules.list"
grep -qx 'ivanna_hal_fix' "$STATE/changed_modules.list"
grep -qx 'omega_pal_presence_probe' "$STATE/changed_modules.list"
if grep -qx 'ivanna_audio_probe' "$STATE/changed_modules.list"; then
    echo "FAIL: pre-disabled module was incorrectly recorded"
    exit 1
fi

run rollback > "$TMP/rollback.out"
[ ! -e "$MODROOT/ivanna_audio_override/disable" ]
[ ! -e "$MODROOT/ivanna_hal_fix/disable" ]
[ -f "$MODROOT/ivanna_audio_probe/disable" ]
[ ! -e "$MODROOT/omega_pal_presence_probe/disable" ]
[ ! -e "$STATE/changed_modules.list" ]

run rollback > "$TMP/rollback-again.out"
echo "PASS: apply/rollback is reversible and preserves pre-existing disabled state"
