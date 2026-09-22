#!/system/bin/sh
# ==============================================================================
# IVANNA Autonomous Runtime Platform
# Boot Analysis & Safeboot Shield
# ==============================================================================

MODDIR="${0%/*}"

# Inicializar plataforma core temprano
source "$MODDIR/core/ivanna_autonomous_core.sh"
ivanna_platform_init "$MODDIR"

set_state "BOOT_ANALYSIS"
observatory_log "BOOT" "Iniciando análisis profundo de pre-montaje (post-fs-data)"

# 4. MEMORY CORE: Historial de Arranques
BOOT_COUNT_FILE="$CORE_DATA/boot_streak"
BOOT_TS_FILE="$CORE_DATA/boot_streak_ts"
COUNT=$(cat "$BOOT_COUNT_FILE" 2>/dev/null || echo 0)
LAST_TS=$(cat "$BOOT_TS_FILE" 2>/dev/null || echo 0)
NOW=$(date +%s)
# Ventana de 10 min: solo cuentan boots cercanos (crash-loop real). Un reboot
# manual del usuario días después NO debe tripear el safe-mode.
if [ $((NOW - LAST_TS)) -gt 600 ]; then COUNT=0; fi
COUNT=$((COUNT + 1))
echo "$COUNT" > "$BOOT_COUNT_FILE"
echo "$NOW" > "$BOOT_TS_FILE"

memory_core_record "BOOT_EVENT" "Inicio del sistema (Boot count actual: $COUNT)"

if [ "$COUNT" -ge 3 ] && [ ! -f "$MODDIR/.safe_mode" ]; then
    set_state "SAFE_MODE"
    observatory_log "BOOT" "CRÍTICO: 3 Inicios inestables consecutivos. IVANNA activa modo preservación."
    touch "$MODDIR/.safe_mode" 2>/dev/null
    
    # Reparación / Shield: Retirar assets peligrosos temporalmente
    rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna_omega.xml" 2>/dev/null
    memory_core_record "SAFE_MODE" "Desactivación forzada de efectos de audio para proteger Host OS"
fi

# Configurar persist properties de la plataforma
setprop persist.ivanna.magisk_active 1 2>/dev/null
MODULE_VER=$(grep '^version=' "$MODDIR/module.prop" 2>/dev/null | cut -d= -f2)
MODULE_VER="${MODULE_VER:-unknown}"
setprop persist.ivanna.version "$MODULE_VER" 2>/dev/null

mkdir -p /dev/socket 2>/dev/null
chmod 0755 /dev/socket 2>/dev/null

is_elf() {
    local hex=$(head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$hex" = "7f454c46" ]
}

for SO in \
    "$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so" \
    "$MODDIR/system/lib64/soundfx/libomega_effect.so"; do
    if [ -f "$SO" ]; then
        if is_elf "$SO"; then
            observatory_log "INTEGRITY" "Librería nativa verificada en frío: $SO"
        else
            observatory_log "INTEGRITY" "FATAL: Checksum ELF corrupto para $SO. Neutralizando componente."
            rm -f "$SO" 2>/dev/null
            memory_core_record "INTEGRITY_FAIL" "Librería $SO fue destruida por corrupción ELF."
        fi
    fi
done

# Safe-mode recovery: si el boot anterior entró en .safe_mode y este boot
# llegó hasta aquí sin el flag, el shield funcionó — registrar para telemetría.
if [ -f "$MODDIR/.safe_mode" ]; then
    memory_core_record "SAFE_MODE_HELD" "Modo preservación activo — audio_effects retirados hasta intervención del usuario"
fi
# Resetear el counter porque post-fs-data no crasheó
echo "0" > "$BOOT_COUNT_FILE"

observatory_log "BOOT" "Análisis de Boot superado. Plataforma lista para elevación de entorno."
