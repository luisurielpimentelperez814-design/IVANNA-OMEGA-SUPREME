#!/system/bin/sh
# IVANNA OMEGA SUPREME — post-fs-data.sh (Googleplex Perfect Edition)
# Operaciones iniciales ultra-robustas en contexto init. No se permite set -e.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log
COUNTER_FILE=/data/adb/ivanna_omega_boot_counter
LAST_OK=/data/adb/ivanna_omega_last_boot_ok

log() { echo "[$(date '+%H:%M:%S')] post-fs: $1" >> "$LOG" 2>/dev/null; }

mkdir -p /data/adb/ivanna_omega 2>/dev/null
mkdir -p /data/adb 2>/dev/null
chmod 0711 /data/adb/ivanna_omega 2>/dev/null

rm -f "$LAST_OK" 2>/dev/null
log "Iniciando secuencia de arranque de núcleo OMEGA."

# Anti-bootloop
COUNT=0
if [ -f "$COUNTER_FILE" ]; then
    COUNT=$(cat "$COUNTER_FILE" 2>/dev/null || echo 0)
fi
COUNT=$((COUNT + 1))
echo "$COUNT" > "$COUNTER_FILE" 2>/dev/null
log "Conteo de inicio inestable: $COUNT"

if [ "$COUNT" -ge 3 ] && [ ! -f "$MODDIR/.safe_mode" ]; then
    log "CRÍTICO: SAFE MODE ACTIVADO. Mitigando conflictos de sistema de audio."
    touch "$MODDIR/.safe_mode" 2>/dev/null
    rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna_omega.xml" 2>/dev/null
    log "Configuraciones de audio neutralizadas en safe_mode."
fi

# Configurar persist properties
setprop persist.ivanna.magisk_active 1 2>/dev/null || log "WARN: Fallo al asignar persist.ivanna.magisk_active"
MODULE_VER=""
if [ -f "$MODDIR/module.prop" ]; then
    MODULE_VER=$(grep '^version=' "$MODDIR/module.prop" 2>/dev/null | cut -d= -f2)
fi
MODULE_VER="${MODULE_VER:-unknown}"
setprop persist.ivanna.version "$MODULE_VER" 2>/dev/null || log "WARN: Fallo al asignar version"
log "Cargando motor acústico versión $MODULE_VER"

mkdir -p /dev/socket 2>/dev/null
chmod 0755 /dev/socket 2>/dev/null

is_elf() {
    hex=$(head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$hex" = "7f454c46" ]
}
for SO in \
    "$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so" \
    "$MODDIR/system/lib64/soundfx/libomega_effect.so"; do
    if [ -f "$SO" ]; then
        if is_elf "$SO"; then
            log "Librería DSP nativa verificada: $SO"
        else
            log "ERROR: Checksum ELF fallido para $SO — Destruyendo para proteger audioserver."
            rm -f "$SO" 2>/dev/null
        fi
    fi
done

log "Secuencia de arranque init superada."
