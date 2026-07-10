#!/system/bin/sh
# IVANNA OMEGA SUPREME — post-fs-data.sh v2.0
# Anti-bootloop + propiedad de sistema para que AudioEffect global
# reconozca que el módulo está activo.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log
COUNTER_FILE=/data/adb/ivanna_omega_boot_counter
log() { echo "[$(date '+%H:%M:%S')] post-fs: $1" >> "$LOG"; }

# ── 1. Anti-bootloop ──────────────────────────────────────────────────────────
COUNT=0
[ -f "$COUNTER_FILE" ] && COUNT=$(cat "$COUNTER_FILE" 2>/dev/null || echo 0)
COUNT=$((COUNT + 1))
echo "$COUNT" > "$COUNTER_FILE"
log "boot #$COUNT"

if [ "$COUNT" -ge 3 ] && [ ! -f "$MODDIR/.safe_mode" ]; then
    log "SAFE MODE activado (3 boots sin completar)"
    touch "$MODDIR/.safe_mode"
    # Retirar overlays de audio_effects para evitar crash de audioserver
    rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" \
          "$MODDIR/system/etc/audio_effects.xml"
fi

# ── 2. Propiedad de sistema: módulo activo ────────────────────────────────────
# Permite que la app y el daemon sepan que el módulo Magisk está montado.
# La app lee: getprop persist.ivanna.magisk_active
setprop persist.ivanna.magisk_active 1
setprop persist.ivanna.version "2.0"
log "propiedades de sistema seteadas"

# ── 3. Socket de comunicación daemon ↔ app ───────────────────────────────────
# Crear directorio del socket con permisos correctos
mkdir -p /dev/socket
chmod 777 /dev/socket 2>/dev/null
log "socket dir listo"

# ── 4. Verificar libomega_effect.so antes de que audioserver la cargue ───────
SO_VENDOR="$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so"
SO_SYSTEM="$MODDIR/system/lib64/soundfx/libomega_effect.so"
for SO in "$SO_VENDOR" "$SO_SYSTEM"; do
    if [ -f "$SO" ]; then
        MAGIC=$(dd if="$SO" bs=1 count=4 2>/dev/null | od -An -c | tr -d ' \n')
        case "$MAGIC" in
            *ELF*)
                log "libomega_effect.so ELF OK: $SO"
                ;;
            *)
                log "ERROR: $SO no es ELF válido — retirando para proteger audioserver"
                rm -f "$SO"
                ;;
        esac
    fi
done

log "post-fs-data.sh v2.0 completado"
