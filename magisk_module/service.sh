#!/system/bin/sh
# IVANNA OMEGA SUPREME — service.sh
# Se ejecuta en late_start service (post-boot). Reseteamos el contador
# de bootloop porque significa que el sistema arrancó correctamente.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log

# Esperar a que el sistema esté completamente arrancado
until [ "$(getprop sys.boot_completed)" = "1" ]; do
  sleep 2
done

echo "[$(date '+%H:%M:%S')] service: boot_completed → reset contador" >> "$LOG"
echo "0" > /data/adb/ivanna_omega_boot_counter

# Verificar que audioserver está vivo (proceso u:r:audioserver:s0)
if pgrep -f audioserver >/dev/null 2>&1; then
  echo "[$(date '+%H:%M:%S')] service: audioserver activo ✓" >> "$LOG"
else
  echo "[$(date '+%H:%M:%S')] service: ⚠ audioserver NO activo — posible conflicto con overlay" >> "$LOG"
fi

# Log de efectos cargados (útil para debugging)
if command -v dumpsys >/dev/null 2>&1; then
  dumpsys media.audio_policy 2>/dev/null | grep -A2 -i "omega\|dolby" \
    >> "$LOG" 2>&1
fi
