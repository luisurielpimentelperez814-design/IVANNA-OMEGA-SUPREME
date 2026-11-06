#!/system/bin/sh
# Instala la sonda contadora en el lugar de libaudio_probe.so y reinicia SOLO vendor.audio-hal.
# Uso: su -c 'sh swap_probe_with_rollback.sh /sdcard/Download/libivanna_pal_write_counter.so'
# Reversion automatica si el HAL no vuelve a "running" con la sonda mapeada en 15 s.
NEW="$1"; MOD=/data/adb/modules/ivanna_audio_probe/system/vendor/lib64/libaudio_probe.so
BK=/data/adb/ivanna_omega/probe_backup; SVC=vendor.audio-hal
[ -r "$NEW" ] || { echo "falta $NEW"; exit 2; }
[ -f "$MOD" ] || { echo "no existe $MOD"; exit 2; }
mkdir -p "$BK"; [ -f "$BK/libaudio_probe.so.orig" ] || cp "$MOD" "$BK/libaudio_probe.so.orig"
sha256sum "$BK/libaudio_probe.so.orig" "$NEW"
rollback() { echo "ROLLBACK"; setprop ctl.stop $SVC; sleep 1; cat "$BK/libaudio_probe.so.orig" > "$MOD"; setprop ctl.start $SVC; }
ok() { P=$(getprop init.svc_debug_pid.$SVC); [ "$(getprop init.svc.$SVC)" = running ] && [ -n "$P" ] && grep -q libaudio_probe /proc/$P/maps 2>/dev/null; }
setprop ctl.stop $SVC; sleep 1
cat "$NEW" > "$MOD" || { rollback; exit 1; }     # mismo inodo: el bind-mount lo ve; el HAL esta parado
setprop ctl.start $SVC
i=0; while [ $i -lt 15 ]; do sleep 1; ok && break; i=$((i+1)); done
if ok; then echo "OK pid=$(getprop init.svc_debug_pid.$SVC) sonda mapeada"; sleep 3; ok && echo "estable" || { rollback; exit 1; }
else rollback; exit 1; fi
