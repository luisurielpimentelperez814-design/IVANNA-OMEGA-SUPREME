#!/system/bin/sh
# IVANNA OMEGA SUPREME — post-fs-data.sh
# Se ejecuta ANTES de que arranque audioserver. Momento correcto para
# preparar el overlay /vendor/etc/audio_effects.xml.
#
# Estrategia: si detectamos que audioserver está en crash-loop
# (>= 3 reinicios en el último minuto), quitamos el overlay
# automáticamente para no dejar el teléfono sin audio.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log

log() { echo "[$(date '+%H:%M:%S')] $1" >> "$LOG"; }

log "post-fs-data: iniciando"

# Anti-bootloop: contador de reinicios
COUNTER_FILE=/data/adb/ivanna_omega_boot_counter
COUNT=0
[ -f "$COUNTER_FILE" ] && COUNT=$(cat "$COUNTER_FILE")
COUNT=$((COUNT + 1))
echo "$COUNT" > "$COUNTER_FILE"
log "post-fs-data: boot #$COUNT desde última instalación"

if [ "$COUNT" -ge 3 ] && [ ! -f "$MODDIR/.safe_mode" ]; then
  log "post-fs-data: 3+ boots seguidos → activando MODO SEGURO"
  touch "$MODDIR/.safe_mode"
  rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" \
        "$MODDIR/system/etc/audio_effects.xml"
fi

log "post-fs-data: OK"
