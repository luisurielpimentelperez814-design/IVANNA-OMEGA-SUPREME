#!/system/bin/sh
# ==============================================================================
# IVANNA Autonomous Runtime Platform
# Boot Analysis & Safeboot Shield
# ==============================================================================

MODDIR="${0%/*}"
CORE="$MODDIR/core/ivanna_autonomous_core.sh"

# Inicializar el core de forma tolerante: un módulo incompleto no debe convertir
# un helper ausente en un bootloop ni escribir accidentalmente en /boot_streak.
if [ -f "$CORE" ]; then
    . "$CORE"
    if command -v ivanna_platform_init >/dev/null 2>&1 &&
       ivanna_platform_init "$MODDIR"; then
        CORE_READY=1
    else
        CORE_READY=0
    fi
else
    CORE_READY=0
fi

if [ "${CORE_READY:-0}" -ne 1 ]; then
    mkdir -p /data/adb/ivanna_omega 2>/dev/null
    echo "$(date) BOOT-GUARD: core ausente o no inicializable; se omiten acciones autónomas" \
        >> /data/adb/ivanna_omega/boot_guard.log 2>/dev/null
    exit 0
fi

set_state "BOOT_ANALYSIS"
observatory_log "BOOT" "Iniciando análisis de pre-montaje (post-fs-data)"

# Historial de fallos consecutivos del runtime. service.sh publica el marcador
# solo después de confirmar daemon vivo + socket abstracto listo. No se reinicia
# el contador al terminar post-fs-data: llegar aquí no prueba que IVANNA arrancó.
BOOT_COUNT_FILE="$CORE_DATA/boot_streak"
BOOT_TS_FILE="$CORE_DATA/boot_streak_ts"
BOOT_HEALTH_FILE="/data/adb/ivanna_omega_last_boot_ok"
BOOT_GUARD="$MODDIR/core/ivanna_boot_guard.sh"
NOW=$(date +%s)
COUNT=0

if [ -f "$BOOT_GUARD" ]; then
    . "$BOOT_GUARD"
    if ivanna_boot_guard_update "$BOOT_COUNT_FILE" "$BOOT_TS_FILE" \
        "$BOOT_HEALTH_FILE" "$NOW" 600; then
        COUNT="$IVANNA_BOOT_STREAK"
    else
        observatory_log "BOOT" "WARN: no se pudo actualizar el contador de salud; safe-mode automático omitido"
        memory_core_record "BOOT_GUARD_ERROR" "No se pudo actualizar boot_streak"
    fi
else
    observatory_log "BOOT" "WARN: helper de salud ausente; safe-mode automático omitido"
    memory_core_record "BOOT_GUARD_ERROR" "Falta ivanna_boot_guard.sh"
fi

activate_safe_mode() {
    local reason="$1"
    set_state "SAFE_MODE"
    touch "$MODDIR/.safe_mode" 2>/dev/null
    # Solo se retiran overlays del módulo; nunca se toca la configuración OEM.
    rm -f "$MODDIR/system/vendor/etc/audio_effects.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna.xml" \
          "$MODDIR/system/etc/audio_effects_ivanna_omega.xml" 2>/dev/null
    observatory_log "BOOT" "SAFE_MODE activado: $reason"
    memory_core_record "SAFE_MODE" "$reason"
}

memory_core_record "BOOT_EVENT" "Inicio del sistema (runtime health streak: $COUNT)"
if [ "$COUNT" -ge 3 ] && [ ! -f "$MODDIR/.safe_mode" ]; then
    activate_safe_mode "Tres arranques consecutivos sin alcanzar daemon + socket saludables"
fi

# Configurar propiedades persistentes de la plataforma.
setprop persist.ivanna.magisk_active 1 2>/dev/null
MODULE_VER=$(grep '^version=' "$MODDIR/module.prop" 2>/dev/null | cut -d= -f2)
MODULE_VER="${MODULE_VER:-unknown}"
setprop persist.ivanna.version "$MODULE_VER" 2>/dev/null

mkdir -p /dev/socket 2>/dev/null
chmod 0755 /dev/socket 2>/dev/null

# Validación estructural del ELF del efecto. No es un checksum: comprueba los
# campos de cabecera necesarios para un shared object ELF64 AArch64 little-endian.
ELF_GUARD="$MODDIR/core/ivanna_elf_guard.sh"
if [ -f "$ELF_GUARD" ]; then
    . "$ELF_GUARD"
    for SO in \
        "$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so" \
        "$MODDIR/system/lib64/soundfx/libomega_effect.so"; do
        if [ -f "$SO" ]; then
            if ivanna_elf_validate "$SO"; then
                observatory_log "INTEGRITY" "Cabecera nativa validada: $SO ($IVANNA_ELF_REASON)"
            else
                observatory_log "INTEGRITY" "FATAL: $SO rechazado: $IVANNA_ELF_REASON. Archivo preservado."
                memory_core_record "INTEGRITY_FAIL" "$SO: $IVANNA_ELF_REASON; no se eliminó el binario"
                if [ ! -f "$MODDIR/.safe_mode" ]; then
                    activate_safe_mode "Biblioteca de audio no válida: $SO ($IVANNA_ELF_REASON)"
                fi
            fi
        fi
    done
else
    observatory_log "INTEGRITY" "WARN: ivanna_elf_guard.sh ausente; no se afirma validación ELF"
    memory_core_record "INTEGRITY_GUARD_MISSING" "Falta ivanna_elf_guard.sh"
fi

# Safe-mode recovery: conservar el registro mientras el usuario no intervenga.
if [ -f "$MODDIR/.safe_mode" ]; then
    memory_core_record "SAFE_MODE_HELD" "Modo preservación activo; los overlays IVANNA de audio permanecen retirados"
fi

observatory_log "BOOT" "Análisis de Boot superado; pendiente de confirmación runtime en service.sh."
