#!/system/bin/sh
MODDIR=${0%/*}
OUT=/data/vendor/ivanna_audio
mkdir -p "$OUT" 2>/dev/null
chmod 0770 "$OUT" 2>/dev/null
chown system:audio "$OUT" 2>/dev/null
{
  echo "IVANNA PAL presence probe module active"
  echo "time=$(date '+%F %T' 2>/dev/null)"
  echo "module=$MODDIR"
  echo "library=$MODDIR/system/vendor/lib64/libivanna_pal_presence_probe.so"
  echo "activation=manual init service LD_PRELOAD entry required"
  echo "automatic_injection=no"
  echo "audio_service_restart=no"
} > "$OUT/pal_probe_module_status.txt"
# Do not set LD_PRELOAD here: init has already launched HAL.
# Do not restart or ptrace the audio service.
exit 0
