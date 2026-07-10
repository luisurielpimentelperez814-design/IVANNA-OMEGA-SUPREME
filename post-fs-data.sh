#!/system/bin/sh
# IVANNA OMEGA SUPREME — post-fs-data.sh
# Anti-bootloop temprano: si hay 3 arranques seguidos sin completar, retira overlays.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log
COUNTER_FILE=/data/adb/ivanna_omega_boot_counter
log() { echo "[$(date '+%H:%M:%S')] $1" >> "$LOG"; }

COUNT=0
[ -f "$COUNTER_FILE" ] && COUNT=$(cat "$COUNTER_FILE" 2>/dev/null || echo 0)
COUNT=$((COUNT + 1))
echo "$COUNT" > "$COUNTER_FILE"
log "post-fs-data: boot #$COUNT"

if [ "$COUNT" -ge 3 ] && [ ! -f "$MODDIR/.safe_mode" ]; then
  log "post-fs-data: activando modo seguro"
  touch "$MODDIR/.safe_mode"
  rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" "$MODDIR/system/etc/audio_effects.xml"
fi
