#!/system/bin/sh
# IVANNA OMEGA SUPREME — post-fs-data.sh v3.0
# FIX CRÍTICO v3.0: ELIMINAR set -e
# post-fs-data.sh corre en el contexto de init antes de que el data filesystem
# esté completamente disponible. set -e en este entorno es letal:
#   - cat "$COUNTER_FILE" puede fallar si /data/adb aún no está montado
#   - stat, od, head pueden fallar en early boot por falta de tools
#   - El primer fallo mata el script ANTES de setear persist.ivanna.magisk_active
#     → la app nunca detecta el módulo como activo aunque esté instalado
#   - SELinux puede bloquear operations → todos los setprop fallan silenciosamente
# La filosofía correcta en post-fs: continuar siempre, registrar fallos, nunca abortar.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log
COUNTER_FILE=/data/adb/ivanna_omega_boot_counter
# FIX v3.0: LAST_OK se BORRA al inicio de post-fs-data para forzar
# que service.sh lo re-cree en cada boot exitoso. Antes se conservaba
# entre boots, lo que permitía que la anti-bootloop NO se disparase
# aunque el módulo estuviera en crash-loop (LAST_OK del boot anterior
# seguía presente, COUNT se reseteaba, y 3 crashes consecutivos pasaban
# sin protección). Ahora la invariante es: LAST_OK existe ↔ service.sh
# completó su primer loop exitoso EN ESTE BOOT.
LAST_OK=/data/adb/ivanna_omega_last_boot_ok

log() { echo "[$(date '+%H:%M:%S')] post-fs: $1" >> "$LOG" 2>/dev/null; }

mkdir -p /data/adb/ivanna_omega 2>/dev/null
mkdir -p /data/adb 2>/dev/null

# ── 1. Borrar LAST_OK al inicio — fuerza re-creación por service.sh ──────────
rm -f "$LAST_OK" 2>/dev/null
log "LAST_OK borrado — service.sh debe recrearlo tras boot estable"

# ── 2. Anti-bootloop robusto ──────────────────────────────────────────────────
# Contar boots consecutivos sin LAST_OK. Si COUNT≥3, activar safe_mode.
# FIX v3.0: sin set -e, todos los pasos son independientes.
COUNT=0
if [ -f "$COUNTER_FILE" ]; then
    COUNT=$(cat "$COUNTER_FILE" 2>/dev/null || echo 0)
fi
COUNT=$((COUNT + 1))
echo "$COUNT" > "$COUNTER_FILE" 2>/dev/null
log "boot #$COUNT sin LAST_OK"

if [ "$COUNT" -ge 3 ] && [ ! -f "$MODDIR/.safe_mode" ]; then
    log "SAFE MODE activado ($COUNT boots consecutivos sin service.sh estable)"
    touch "$MODDIR/.safe_mode" 2>/dev/null
    # Retirar ambos archivos de audio_effects para no alterar audioserver
    rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna_omega.xml" 2>/dev/null
    log "audio_effects desactivados en safe_mode"
fi

# ── 3. Propiedades de sistema ─────────────────────────────────────────────────
setprop persist.ivanna.magisk_active 1 2>/dev/null || log "WARN: setprop magisk_active falló"

# FIX v3.0: versión dinámica desde module.prop (no hardcodeada "2.1")
# En v2.1 era literalmente: setprop persist.ivanna.version "2.1"
# Ahora se lee del module.prop real para que coincida con el zip instalado.
MODULE_VER=""
if [ -f "$MODDIR/module.prop" ]; then
    MODULE_VER=$(grep '^version=' "$MODDIR/module.prop" 2>/dev/null | cut -d= -f2)
fi
MODULE_VER="${MODULE_VER:-unknown}"
setprop persist.ivanna.version "$MODULE_VER" 2>/dev/null || log "WARN: setprop version falló"
log "módulo v$MODULE_VER — props seteadas"

# ── 4. Socket dir ─────────────────────────────────────────────────────────────
mkdir -p /dev/socket 2>/dev/null
chmod 0755 /dev/socket 2>/dev/null

# ── 5. Verificación ELF de libomega_effect.so ─────────────────────────────────
is_elf() {
    hex=$(head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$hex" = "7f454c46" ]
}

for SO in \
    "$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so" \
    "$MODDIR/system/lib64/soundfx/libomega_effect.so"; do
    if [ -f "$SO" ]; then
        if is_elf "$SO"; then
            log "ELF OK: $SO ($(stat -c%s "$SO" 2>/dev/null || echo ?) bytes)"
        else
            log "ERROR: $SO no es ELF — retirando para proteger audioserver"
            rm -f "$SO" 2>/dev/null
        fi
    fi
done

# ── 6. Directorio runtime con permisos correctos ──────────────────────────────
# audioserver necesita leer /data/adb/ivanna_omega/ para el SHM del Control Bus.
# 0711: raíz ejecutable por todos (traversal), archivos protegidos por DAC+SELinux.
chmod 0711 /data/adb/ivanna_omega 2>/dev/null

log "post-fs-data.sh v3.0 completado (safe_mode=$( [ -f "$MODDIR/.safe_mode" ] && echo yes || echo no ))"
