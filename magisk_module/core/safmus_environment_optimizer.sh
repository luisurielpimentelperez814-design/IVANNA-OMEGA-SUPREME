#!/system/bin/sh
# SAFMUS environment coordinator: observation-first and fail-safe.
# Never rewrites vendor/HAL/sysfs nodes, disables SELinux, or bypasses permissions.
MODDIR="${0%/*}"
ROOT="/data/adb/ivanna_omega"
OUT="$ROOT/safmus"
PROFILE="$OUT/environment_profile.env"
REPORT="$OUT/environment_report.txt"
CAPABILITIES="$OUT/hardware_capabilities.env"
mkdir -p "$OUT" 2>/dev/null || exit 1
TMP="$PROFILE.tmp.$$"; RTMP="$REPORT.tmp.$$"; CTMP="$CAPABILITIES.tmp.$$"
getprop_safe() { getprop "$1" 2>/dev/null | tr '\n' ' ' | cut -c1-160; }
read_num() { v=$(cat "$1" 2>/dev/null | tr -d '[:space:]'); case "$v" in ''|*[!0-9]*) echo ""; ;; *) echo "$v";; esac; }
mem_kb=$(awk '/MemAvailable:/ {print $2; exit}' /proc/meminfo 2>/dev/null)
case "$mem_kb" in ''|*[!0-9]*) mem_kb=0;; esac
cpu_count=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null); case "$cpu_count" in ''|*[!0-9]*) cpu_count=0;; esac
# Thermal telemetry: accept only known temperature-zone types and plausible
# millidegree-Celsius readings. Many Qualcomm zones expose levels/thresholds
# rather than temperatures; unknown types and ambiguous scales are ignored.
thermal_max=""
thermal_max_type=""
thermal_sensor_count=0
for z in /sys/class/thermal/thermal_zone*/temp; do
  [ -r "$z" ] || continue
  zone_dir=${z%/temp}
  sensor_type=$(cat "$zone_dir/type" 2>/dev/null | tr -d '\r\n')
  case "$sensor_type" in
    cpu-*|cpuss-*|gpuss-*|mapss-*|pmr735a_tz|pm6125_tz|pm7250b_tz|\
    msm_therm|pa_therm*|tspk_therm|quiet_therm|xo_therm|chg_therm|\
    conn_therm|qcom_battery|front_temp|back_temp|fuse_temp|battery|\
    audio|ddr|q6-hvx|camera|mdm-core*|mdm-vec|mdm-scl|video|cwlan|\
    modem-skin|modem-lte-sub6-pa*)
      ;;
    *)
      continue
      ;;
  esac

  raw=$(read_num "$z")
  [ -n "$raw" ] || continue
  # Known device readings are in millidegrees C. Ignore low/ambiguous values
  # (e.g. 625, 703, 0) rather than interpreting them as degrees.
  [ "$raw" -ge 10000 ] 2>/dev/null || continue
  [ "$raw" -le 125000 ] 2>/dev/null || continue
  t=$((raw / 1000))
  [ "$t" -ge 10 ] && [ "$t" -le 125 ] || continue

  thermal_sensor_count=$((thermal_sensor_count + 1))
  if [ -z "$thermal_max" ] || [ "$t" -gt "$thermal_max" ]; then
    thermal_max=$t
    thermal_max_type=$sensor_type
  fi
done
# Inventario transversal: presencia/capacidad observable; no confundir detección con control efectivo.
camera_nodes=0
for n in /dev/video* /dev/media*; do [ -e "$n" ] && camera_nodes=$((camera_nodes + 1)); done
sound_nodes=0
for n in /dev/snd/pcmC*D*c /dev/snd/controlC*; do [ -e "$n" ] && sound_nodes=$((sound_nodes + 1)); done
cpu_policies=0
for n in /sys/devices/system/cpu/cpufreq/policy*; do [ -d "$n" ] && cpu_policies=$((cpu_policies + 1)); done
gpu_nodes=0
for n in /sys/class/devfreq/*gpu* /sys/class/kgsl/kgsl-3d0 /sys/devices/platform/*gpu*; do [ -e "$n" ] && gpu_nodes=$((gpu_nodes + 1)); done
display_nodes=0
for n in /sys/class/backlight/* /sys/class/drm/card*-*/status; do [ -e "$n" ] && display_nodes=$((display_nodes + 1)); done
input_nodes=0
for n in /dev/input/event*; do [ -e "$n" ] && input_nodes=$((input_nodes + 1)); done
sensor_nodes=0
for n in /sys/bus/iio/devices/iio:device* /sys/class/sensors/*; do [ -e "$n" ] && sensor_nodes=$((sensor_nodes + 1)); done
battery_nodes=0
for n in /sys/class/power_supply/*; do [ -e "$n" ] && battery_nodes=$((battery_nodes + 1)); done
cpu_governors=""
for n in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
  [ -r "$n" ] || continue
  g=$(cat "$n" 2>/dev/null | tr -d '\n')
  [ -n "$g" ] && cpu_governors="${cpu_governors}${cpu_governors:+,}$g"
done
display_brightness=$(settings get system screen_brightness 2>/dev/null | tr -d '[:space:]')
display_refresh=$(settings get system peak_refresh_rate 2>/dev/null | tr -d '[:space:]')
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
 echo "cpu_count=$cpu_count"; echo "cpu_frequency_policies=$cpu_policies"; echo "cpu_governors=$cpu_governors"; echo "gpu_nodes=$gpu_nodes"; echo "display_nodes=$display_nodes"; echo "input_nodes=$input_nodes"; echo "sensor_nodes=$sensor_nodes"; echo "battery_nodes=$battery_nodes"; echo "display_brightness=$display_brightness"; echo "display_peak_refresh_setting=$display_refresh"; echo "mem_available_kb=$mem_kb"
 echo "thermal_max_c=$thermal_max"; echo "thermal_sensor_count=$thermal_sensor_count"; echo "thermal_max_sensor=$thermal_max_type"; echo "camera_device_nodes=$camera_nodes"
 echo "sound_device_nodes=$sound_nodes"; echo "audio_class=$audio_class"
 echo "resource_budget=$budget"; echo "budget_reason=$reason"
 echo "actuation=observation_only"
} > "$TMP"
{
 echo "SAFMUS device/environment snapshot"; echo "Timestamp: $(date)"
 echo "Resource budget: $budget ($reason)"
 echo "CPU threads detected: $cpu_count"; echo "Available memory KB: $mem_kb"
 echo "Highest validated thermal zone C: ${thermal_max:-unknown} (${thermal_max_type:-no_valid_sensor})"; echo "Valid thermal sensors: $thermal_sensor_count"
 echo "Camera/media device nodes: $camera_nodes (presence only; no camera opened)"
 echo "Sound device nodes: $sound_nodes (presence only; no capture/playback started)"
 echo "Audio class: $audio_class"
 echo "CPU frequency policies/governors: $cpu_policies / ${cpu_governors:-unknown}"
 echo "GPU nodes: $gpu_nodes; display/backlight nodes: $display_nodes"
 echo "Input nodes: $input_nodes; sensor nodes: $sensor_nodes; power-supply nodes: $battery_nodes"
 echo "Display settings: brightness=${display_brightness:-unknown}, peak_refresh=${display_refresh:-unknown}"
 echo "Policy: detect all available classes; only use validated, supported controls for changes."
 echo "Room acoustics need a user-authorized in-app measurement; boot telemetry cannot infer a room."
} > "$RTMP"
{
 echo "schema=1"
 echo "cpu_policies=$cpu_policies"
 echo "cpu_governors=$cpu_governors"
 echo "gpu_nodes=$gpu_nodes"
 echo "display_nodes=$display_nodes"
 echo "camera_nodes=$camera_nodes"
 echo "sound_nodes=$sound_nodes"
 echo "input_nodes=$input_nodes"
 echo "sensor_nodes=$sensor_nodes"
 echo "battery_nodes=$battery_nodes"
 echo "display_brightness=$display_brightness"
 echo "display_peak_refresh_setting=$display_refresh"
 echo "capability_detection_only=1"
} > "$CTMP"
chmod 600 "$TMP" "$RTMP" "$CTMP" 2>/dev/null || {
  rm -f "$TMP" "$RTMP" "$CTMP"
  exit 1
}
mv -f "$TMP" "$PROFILE" 2>/dev/null || {
  rm -f "$TMP" "$RTMP" "$CTMP"
  exit 1
}
mv -f "$RTMP" "$REPORT" 2>/dev/null || {
  rm -f "$RTMP" "$CTMP"
  exit 1
}
mv -f "$CTMP" "$CAPABILITIES" 2>/dev/null || {
  rm -f "$CTMP"
  exit 1
}
exit 0
