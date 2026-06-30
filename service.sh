#!/system/bin/sh
# IVANNA-OMEGA-SUPREME v1.5 — service.sh
# Se ejecuta después de que el sistema arranque completamente.
# FIX v1.5: corrige grep libivanna_fusion -> libomega_effect.so
# FIX v1.5: añade verificación de carga y rollback automático

MODDIR="${0%/*}"
LOG="/data/local/tmp/ivanna_omega.log"
LIB_NAME="libomega_effect.so"
LIB_PATH="$MODDIR/system/lib64/soundfx/$LIB_NAME"

echo "[IVANNA-OMEGA] $(date) — service.sh start v1.5" >> "$LOG"

# Esperar a que audioserver esté listo
sleep 3

# Verificación 1: ¿el .so existe en el módulo?
if [ ! -f "$LIB_PATH" ]; then
    echo "[IVANNA-OMEGA] ERROR: $LIB_NAME no encontrado en módulo" >> "$LOG"
    setprop ivanna.omega.status "error:so_missing"
    exit 1
fi

# Verificación 2: ¿audioserver lo ha mapeado?
# FIX v1.5: busca libomega_effect.so, NO libivanna_fusion
LOADED=$(cat /proc/$(pidof audioserver)/maps 2>/dev/null | grep "$LIB_NAME")
if [ -n "$LOADED" ]; then
    echo "[IVANNA-OMEGA] DSP cargado correctamente por audioserver ✓" >> "$LOG"
    setprop ivanna.omega.status "active"
else
    echo "[IVANNA-OMEGA] WARNING: $LIB_NAME no detectado en audioserver. Reintentando..." >> "$LOG"
    sleep 2

    # Segundo intento
    LOADED2=$(cat /proc/$(pidof audioserver)/maps 2>/dev/null | grep "$LIB_NAME")
    if [ -n "$LOADED2" ]; then
        echo "[IVANNA-OMEGA] DSP cargado en segundo intento ✓" >> "$LOG"
        setprop ivanna.omega.status "active"
    else
        echo "[IVANNA-OMEGA] ERROR: $LIB_NAME no cargado tras 2 intentos. ROLLBACK." >> "$LOG"

        # ROLLBACK: restaurar backup si existe
        if [ -f "$LIB_PATH.bak" ]; then
            echo "[IVANNA-OMEGA] Restaurando backup..." >> "$LOG"
            cp "$LIB_PATH.bak" "$LIB_PATH"
            chmod 644 "$LIB_PATH"
            setprop ivanna.omega.status "rollback"
            # Reiniciar audioserver para cargar versión anterior
            setprop sys.audio.effects.restart 1 2>/dev/null
        else
            echo "[IVANNA-OMEGA] Sin backup disponible. Deshabilitando módulo." >> "$LOG"
            # Crear flag de disable para Magisk
            touch /data/adb/modules/ivanna_omega/disable 2>/dev/null
            setprop ivanna.omega.status "safe_mode"
        fi
    fi
fi

echo "[IVANNA-OMEGA] service.sh complete — status=$(getprop ivanna.omega.status)" >> "$LOG"
