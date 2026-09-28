#!/system/bin/sh
# IVANNA OMEGA SUPREME — service.sh
# Late-start service: registra salud de audio y resetea contador anti-bootloop.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log

until [ "$(getprop sys.boot_completed)" = "1" ]; do
  sleep 2
done

echo "[$(date '+%H:%M:%S')] service: boot_completed; módulo=$MODDIR" >> "$LOG"
echo "0" > /data/adb/ivanna_omega_boot_counter

AUDIO_PID=$(pidof audioserver 2>/dev/null | awk '{print $1}')
if [ -n "$AUDIO_PID" ]; then
  echo "[$(date '+%H:%M:%S')] service: audioserver activo pid=$AUDIO_PID" >> "$LOG"
else
  echo "[$(date '+%H:%M:%S')] service: ADVERTENCIA audioserver no activo" >> "$LOG"
fi

if command -v dumpsys >/dev/null 2>&1; then
  dumpsys media.audio_policy 2>/dev/null | grep -A2 -i "omega\|dolby" >> "$LOG" 2>&1
fi
