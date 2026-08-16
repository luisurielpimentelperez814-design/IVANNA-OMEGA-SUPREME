#!/system/bin/sh
# IVANNA OMEGA SUPREME — post-fs-data.sh v2.1 (PATCH)
# Fixes:
#  - Anti-bootloop: solo dispara safe_mode si service.sh NUNCA logró correr
#    (usa marca .last_boot_ok tocada por service.sh cuando terminó bien).
#  - ELF check: readelf/head -c4 con matching real de "\x7fELF", no dd|od.
#  - Setprop de módulo activo sigue aquí (leído por la app).

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log
COUNTER_FILE=/data/adb/ivanna_omega_boot_counter
LAST_OK=/data/adb/ivanna_omega_last_boot_ok
log() { echo "[$(date '+%H:%M:%S')] post-fs: $1" >> "$LOG" 2>/dev/null; }

# ── 1. Anti-bootloop robusto ─────────────────────────────────────────────────
# FIX: si el boot anterior fue exitoso (LAST_OK existe), resetear el contador.
# Sin este reset, después de 3+ boots normales COUNT≥3, y si LAST_OK se borra
# (desinstalación, reset de datos) el próximo crash dispara safe_mode en 1 boot
# en vez de los 3 consecutivos que se pretenden guardar como umbral.
COUNT=0
if [ -f "$LAST_OK" ]; then
    echo 0 > "$COUNTER_FILE"
    COUNT=0
elif [ -f "$COUNTER_FILE" ]; then
    COUNT=$(cat "$COUNTER_FILE" 2>/dev/null || echo 0)
fi
COUNT=$((COUNT + 1))
echo "$COUNT" > "$COUNTER_FILE"
log "boot #$COUNT (last_ok=$( [ -f $LAST_OK ] && echo yes || echo no ))"

# Solo activar safe_mode si:
#   a) hubo >=3 arranques encadenados, Y
#   b) NUNCA se marcó un service.sh completo (LAST_OK no existe).
# service.sh crea LAST_OK al final; con eso, un boot exitoso limpia el
# criterio y no se dispara falso positivo tras 3 reinicios normales.
if [ "$COUNT" -ge 3 ] && [ ! -f "$LAST_OK" ] && [ ! -f "$MODDIR/.safe_mode" ]; then
    log "SAFE MODE activado (3 boots consecutivos sin service.sh OK)"
    touch "$MODDIR/.safe_mode"
    # FIX (nombres reales, auditoría 2026-08-11): el archivo vendor es
    # audio_effects.xml (correcto), pero bajo system/etc/ los archivos
    # reales se llaman audio_effects_ivanna.xml y audio_effects_ivanna_omega.xml
    # — "audio_effects.xml" a secas en system/etc/ nunca existió en este
    # módulo, así que ese rm era un no-op silencioso que dejaba viva la
    # config de system/etc/ real en safe mode.
    rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna_omega.xml"
fi

# ── 2. Propiedades de sistema (leídas por la app) ────────────────────────────
setprop persist.ivanna.magisk_active 1
setprop persist.ivanna.version "2.1"
log "propiedades de sistema seteadas"

# ── 3. Socket dir ────────────────────────────────────────────────────────────
mkdir -p /dev/socket 2>/dev/null
chmod 0755 /dev/socket 2>/dev/null

# ── 4. Verificación ELF robusta de libomega_effect.so ────────────────────────
is_elf() {
    # Lee los 4 primeros bytes en hex; ELF magic = 7f 45 4c 46
    hex=$(head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$hex" = "7f454c46" ]
}

SO_VENDOR="$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so"
SO_SYSTEM="$MODDIR/system/lib64/soundfx/libomega_effect.so"
for SO in "$SO_VENDOR" "$SO_SYSTEM"; do
    if [ -f "$SO" ]; then
        if is_elf "$SO"; then
            log "libomega_effect.so ELF OK: $SO"
        else
            log "ERROR: $SO no es ELF — retirando para proteger audioserver"
            rm -f "$SO"
        fi
    fi
done

log "post-fs-data.sh v2.1 completado"

# ── 5. SELinux Context para ivanna_daemon (Dominio Aislado) ──────────────────
# Asignamos el tipo ejecutable para que la transición de dominio (domain_auto_trans)
# ocurra automáticamente cuando service.sh lo lance.
chcon u:object_r:omega_daemon_exec:s0 $MODDIR/system/bin/ivanna_daemon 2>/dev/null
log "SELinux context assigned to ivanna_daemon"
