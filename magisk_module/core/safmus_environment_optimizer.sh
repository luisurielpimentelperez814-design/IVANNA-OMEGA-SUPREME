#!/system/bin/sh
# SAFMUS environment coordinator: observation-first and fail-safe.
# Never rewrites vendor/HAL/sysfs nodes, disables SELinux, or bypasses permissions.
MODDIR="${0%/*}"
ROOT="/data/adb/ivanna_omega"
OUT="$ROOT/safmus"
PROFILE="$OUT/environment_profile.env"
REPORT="$OUT/environment_report.txt"
mkdir -p "$OUT" 2>/dev/null || exit 0
TMP="$PROFILE.tmp.$$"; RTMP="$REPORT.tmp.$$"
getprop_safe() { getprop "$1" 2>/dev/null | tr '\n' ' ' | cut -c1-160; }
read_num() { v=$(cat "$1" 2>/dev/null | tr -d '[:space:]'); case "$v" in ''|*[!0-9]*) echo ""; ;; *) echo "$v";; esac; }
mem_kb=$(awk '/MemAvailable:/ {print $2; exit}' /proc/meminfo 2>/dev/null)
case "$mem_kb" in ''|*[!0-9]*) mem_kb=0;; esac
cpu_count=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null); case "$cpu_count" in ''|*[!0-9]*) cpu_count=0;; esac
thermal_max=""
for z in /sys/class/thermal/thermal_zone*/temp; do
  [ -r "$z" ] || continue
  t=$(read_num "$z"); [ -n "$t" ] || continue
  if [ "$t" -gt 1000 ] 2>/dev/null; then t=$((t / 1000)); fi
  if [ "$t" -gt 0 ] 2>/dev/null && { [ -z "$thermal_max" ] || [ "$t" -gt "$thermal_max" ]; }; then thermal_max=$t; fi
done
camera_nodes=0
for n in /dev/video* /dev/media*; do [ -e "$n" ] && camera_nodes=$((camera_nodes + 1)); done
sound_nodes=0
for n in /dev/snd/pcmC*D*c /dev/snd/controlC*; do [ -e "$n" ] && sound_nodes=$((sound_nodes + 1)); done
audio_class=unknown
[ -d /sys/class/sound ] && audio_class=kernel_sound_class_present
budget=normal; reason="telemetria básica disponible"
if [ "$mem_kb" -gt 0 ] && [ "$mem_kb" -lt 262144 ]; then budget=conservative; reason="memoria disponible baja"; fi
if [ -n "$thermal_max" ] && [ "$thermal_max" -ge 75 ]; then budget=conservative; reason="temperatura reportada alta"; fi
if [ -f "$MODDIR/../.safe_mode" ]; then budget=safe_mode; reason="módulo en modo seguro"; fi
{
 echo "schema=1"; echo "timestamp=$(date +%s)"
 echo "android_release=$(getprop_safe ro.build.version.release)"
 echo "sdk=$(getprop_safe ro.build.version.sdk)"
 echo "manufacturer=$(getprop_safe ro.product.manufacturer)"
 echo "device=$(getprop_safe ro.product.device)"
 echo "soc=$(getprop_safe ro.board.platform)"
 echo "kernel=$(uname -r 2>/dev/null)"
 echo "cpu_count=$cpu_count"; echo "mem_available_kb=$mem_kb"
 echo "thermal_max_c=$thermal_max"; echo "camera_device_nodes=$camera_nodes"
 echo "sound_device_nodes=$sound_nodes"; echo "audio_class=$audio_class"
 echo "resource_budget=$budget"; echo "budget_reason=$reason"
 echo "actuation=observation_only"
} > "$TMP"
{
 echo "SAFMUS device/environment snapshot"; echo "Timestamp: $(date)"
 echo "Resource budget: $budget ($reason)"
 echo "CPU threads detected: $cpu_count"; echo "Available memory KB: $mem_kb"
 echo "Highest readable thermal zone C: ${thermal_max:-unknown}"
 echo "Camera/media device nodes: $camera_nodes (presence only; no camera opened)"
 echo "Sound device nodes: $sound_nodes (presence only; no capture/playback started)"
 echo "Audio class: $audio_class"
 echo "Policy: never mutate unknown vendor/HAL/sysfs controls or bypass Android permissions."
 echo "Room acoustics need a user-authorized in-app measurement; boot telemetry cannot infer a room."
} > "$RTMP"
chmod 600 "$TMP" "$RTMP" 2>/dev/null
mv -f "$TMP" "$PROFILE" 2>/dev/null || rm -f "$TMP"
mv -f "$RTMP" "$REPORT" 2>/dev/null || rm -f "$RTMP"
exit 0
