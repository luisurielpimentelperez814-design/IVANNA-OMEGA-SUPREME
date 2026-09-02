#!/system/bin/sh
# ivanna_diag.sh — Telemetría profesional del runtime (daemon/CPU/mem/DSP/socket).
# Salida: /data/adb/ivanna_omega/diag.log (append, rotación 256KB).
MODDIR="${0%/*}/.."; DATA=/data/adb/ivanna_omega; LOG=$DATA/diag.log
mkdir -p "$DATA" 2>/dev/null
[ -f "$LOG" ] && [ "$(stat -c%s "$LOG" 2>/dev/null || echo 0)" -gt 262144 ] && mv "$LOG" "$LOG.old"
TS=$(date '+%Y-%m-%d %H:%M:%S')
PID=$(pidof ivanna_daemon 2>/dev/null)
if [ -n "$PID" ]; then
  CPU=$(ps -o %CPU= -p "$PID" 2>/dev/null | tr -d ' '); RSS=$(ps -o RSS= -p "$PID" 2>/dev/null | tr -d ' ')
  SCHED=$(chrt -p "$PID" 2>/dev/null | grep -o 'SCHED_[A-Z]*' | head -1)
  STATE="alive pid=$PID cpu=${CPU:-?}% rss=${RSS:-?}KB sched=${SCHED:-?}"
else STATE="DOWN"; fi
SOCK=$(grep -c 'omega_daemon_socket\|omega_command_socket' /proc/net/unix 2>/dev/null || echo 0)
# Disponibilidad cDSP/FastRPC en tiempo real (ruta híbrida Hexagon/NEON)
CDSP="absent"; [ -e /dev/adsprpc-smd ] || [ -e /dev/fastrpc-cdsp ] && CDSP="present"
AF=$(dumpsys audio 2>/dev/null | grep -c "stream type:" 2>/dev/null || echo 0)
echo "$TS daemon=[$STATE] sockets=$SOCK cdsp=$CDSP audio_streams=$AF" >> "$LOG"
