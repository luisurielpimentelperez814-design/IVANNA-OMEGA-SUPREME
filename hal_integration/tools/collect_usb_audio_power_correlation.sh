#!/system/bin/sh
# IVANNA Moto G85 USB power/audio correlation capture (read-only).
# Run from Termux: su -c 'sh /data/local/tmp/ivanna_usb_audio_correlation.sh'
# Copy this file to /data/local/tmp first. Output: /sdcard/Download/ivanna_usb_audio_correlation.txt
set -u
OUT=/sdcard/Download/ivanna_usb_audio_correlation.txt
SAMPLES=${SAMPLES:-30}
INTERVAL=${INTERVAL:-2}
mkdir -p /sdcard/Download 2>/dev/null
: > "$OUT"
sample=0
while [ "$sample" -lt "$SAMPLES" ]; do
  {
    echo "===== SAMPLE=$sample ====="
    date '+wall=%Y-%m-%dT%H:%M:%S%z'
    printf 'uptime='; cat /proc/uptime 2>/dev/null
    echo "-- POWER_SUPPLIES --"
    for d in /sys/class/power_supply/*; do
      [ -d "$d" ] || continue
      for f in type online status usb_type current_now voltage_now; do
        [ -r "$d/$f" ] || continue
        printf '%s.%s=' "${d##*/}" "$f"
        tr '\n' ' ' < "$d/$f" 2>/dev/null
        echo
      done
    done
    echo "-- AUDIOFLINGER --"
    dumpsys media.audio_flinger 2>/dev/null | grep -E -A 2 'name AudioOut_D|Timestamp stats:|FastMixer Timestamp stats|writeErrors=|Normal mixer raw underrun counters|mixPeriod='
    echo "-- RECENT_KERNEL_POWER_EVENTS --"
    dmesg 2>/dev/null | grep -iE 'usbin-collapse|aicl-done|icl_settled|select pdo info|smb5_wdog|setting GPIOs active' | tail -12
  } >> "$OUT"
  sample=$((sample + 1))
  sleep "$INTERVAL"
done
echo "Capture complete: $OUT"
echo "Samples=$SAMPLES interval_seconds=$INTERVAL; event lines are tail snapshots, not proof of same-sample causality."
