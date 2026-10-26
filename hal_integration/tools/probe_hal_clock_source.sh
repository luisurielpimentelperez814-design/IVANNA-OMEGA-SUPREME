#!/system/bin/sh
# Read-only probe for HAL/PAL clock source selection on Moto G85.
# Does not open PCM devices, restart services, change permissions, or write mixers.
# Output: /sdcard/Download/ivanna_hal_clock_probe_<timestamp>/report.txt

STAMP="$(date +%Y%m%d_%H%M%S 2>/dev/null)"
[ -n "$STAMP" ] || STAMP=run
OUT="${1:-/sdcard/Download/ivanna_hal_clock_probe_$STAMP}"
mkdir -p "$OUT" 2>/dev/null || { echo "No se puede crear $OUT"; exit 1; }
R="$OUT/report.txt"
: > "$R" || exit 1
say() { printf '%s\n' "$*" >> "$R"; }
section() { say ""; say "===== $* ====="; }

section "Identidad"
for p in ro.product.model ro.product.device ro.board.platform ro.hardware ro.build.version.release ro.build.version.sdk ro.build.fingerprint; do
  say "$p=$(getprop "$p" 2>/dev/null)"
done
say "uid=$(id 2>/dev/null)"
say "selinux=$(getenforce 2>/dev/null || echo unavailable)"
say "timestamp=$(date 2>/dev/null)"

section "ALSA playback nodes: metadata only; NO open/ioctl"
for f in /proc/asound/cards /proc/asound/pcm /proc/asound/devices; do
  [ -r "$f" ] || continue
  say "--- $f"
  sed -n '1,160p' "$f" >> "$R" 2>/dev/null
done
for f in /dev/snd/pcmC*D*p; do
  [ -e "$f" ] || continue
  say "--- node: $f"
  ls -lZ "$f" >> "$R" 2>&1
done
for f in /proc/asound/card*/pcm*p/sub*/status; do
  [ -r "$f" ] || continue
  say "--- status: $f"
  sed -n '1,40p' "$f" >> "$R" 2>/dev/null
  hw="${f%/status}/hw_params"
  if [ -r "$hw" ]; then
    say "--- hw_params: $hw"
    sed -n '1,40p' "$hw" >> "$R" 2>/dev/null
  fi
done

section "IVANNA daemon: read-only process/log status"
if command -v pidof >/dev/null 2>&1; then say "pidof ivanna_daemon=$(pidof ivanna_daemon 2>/dev/null)"; fi
for f in /data/adb/ivanna_omega/daemon.log /data/adb/ivanna_omega.log /data/adb/ivanna_omega/hal_clock.log; do
  [ -r "$f" ] || continue
  say "--- recent HAL-CLOCK/ASRC entries: $f"
  grep -E 'HAL-CLOCK|ASRC-SW|clock_snapshot|no-source|waiting-samples' "$f" 2>/dev/null | tail -80 >> "$R"
done

section "HAL/PAL snapshot: metadata only"
SNAP=/data/vendor/ivanna_audio/clock_snapshot
if [ -e "$SNAP" ]; then
  ls -lZ "$SNAP" >> "$R" 2>&1
  if command -v stat >/dev/null 2>&1; then stat -c 'size=%s mode=%a uid=%u gid=%g mtime=%Y' "$SNAP" >> "$R" 2>&1; fi
  say "first 64 bytes (hex only; do not interpret as proof without a validated reader):"
  od -An -tx1 -N64 "$SNAP" >> "$R" 2>&1
else
  say "snapshot absent: $SNAP"
fi

section "Resultado honesto"
say "La presencia del nodo o del snapshot NO prueba que sea la ruta de audio activa."
say "Se requiere que el HAL/PAL propietario publique frames y timestamp asociados mientras reproduce audio."
say "Esta sonda no abre PCM, no consulta ioctl de PCM y no cambia el sistema."
echo "Informe guardado: $R"
