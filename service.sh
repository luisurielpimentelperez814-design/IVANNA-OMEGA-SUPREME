#!/system/bin/sh
# IVANNA FUSION DSP — service.sh
# Se ejecuta después de que el sistema arranque completamente.

MODDIR="${0%/*}"
LOG="/data/local/tmp/ivanna_fusion.log"

echo "[IVANNA FUSION] $(date) — service.sh start" >> "$LOG"

# Esperar a que audioserver esté listo
sleep 5

# Verificar que el .so está cargado por audioserver
LOADED=$(cat /proc/$(pidof audioserver)/maps 2>/dev/null | grep libivanna_fusion)
if [ -n "$LOADED" ]; then
  echo "[IVANNA FUSION] DSP cargado correctamente por audioserver ✓" >> "$LOG"
else
  echo "[IVANNA FUSION] WARNING: .so no detectado en audioserver. Restarting..." >> "$LOG"
  # En algunos dispositivos, audioserver necesita restart tras instalar efectos nuevos
  # Esto es seguro con Magisk (no root shell persists)
  setprop sys.audio.effects.restart 1 2>/dev/null
  echo "[IVANNA FUSION] Señal de restart enviada." >> "$LOG"
fi

echo "[IVANNA FUSION] service.sh complete" >> "$LOG"
