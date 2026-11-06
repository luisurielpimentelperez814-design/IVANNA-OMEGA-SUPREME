#!/system/bin/sh
# collect_hal_preload_phase1.sh — SOLO LECTURA. No modifica archivos, props, SELinux ni procesos.
# Uso (Termux):  su -c 'sh /sdcard/collect_hal_preload_phase1.sh'   -> informe en /data/local/tmp/ivanna_phase1.txt
# Responde con evidencia a la Fase 1 (puntos 1-8) de la orden LD_PRELOAD/HAL.
R=/data/local/tmp/ivanna_phase1.txt
: > "$R"
sec() { printf '\n===== %s =====\n' "$1" >> "$R"; }
run() { printf '$ %s\n' "$*" >> "$R"; "$@" >> "$R" 2>&1; printf '[rc=%s]\n' "$?" >> "$R"; }

sec "0. Identidad (la auditoría del repo dice Android 14/SDK 34; la orden dice 15/35: verificar)"
for p in ro.build.version.release ro.build.version.sdk ro.build.fingerprint ro.product.model \
         ro.hardware ro.boot.slot_suffix ro.boot.verifiedbootstate ro.boot.flash.locked; do
  printf '%s=%s\n' "$p" "$(getprop $p)" >> "$R"; done
run getenforce
run /data/adb/magisk/magisk -v
run /data/adb/magisk/magisk -V

sec "1. Orden REAL de parseo de RC (init los registra en el log del kernel)"
dmesg 2>/dev/null | grep -E "init: .*(Parsing file|Parsed|Added|Loaded)" | head -120 >> "$R"
[ -s "$R" ] || true
echo "(si lo anterior esta vacio: dmesg restringido o buffer rotado; reintentar justo tras boot)" >> "$R"

sec "2. init.\${ro.hardware}.rc de vendor"
HW=$(getprop ro.hardware)
for f in /vendor/etc/init/hw/init.$HW.rc /vendor/etc/init/hw/init.qcom.rc; do
  [ -r "$f" ] && { echo "--- $f" >> "$R"; grep -nE "import|vendor.audio-hal|audio" "$f" >> "$R" 2>&1; }
done

sec "3/4. TODAS las definiciones de vendor.audio-hal y quien usa override"
for d in /system/etc/init /system_ext/etc/init /vendor/etc/init /odm/etc/init /product/etc/init /overlay.d /debug_ramdisk; do
  [ -d "$d" ] || continue
  grep -rIl "vendor.audio-hal" "$d" 2>/dev/null | while read -r f; do
    echo "--- $f" >> "$R"; ls -lZ "$f" >> "$R" 2>&1
    awk '/^service vendor.audio-hal/{p=1} p{print NR": "$0} /^$/{p=0}' "$f" >> "$R" 2>&1
  done
done

sec "5/6. Disponibilidad de la sonda y montajes (momento Magisk vs init)"
run ls -lZ /vendor/lib64/libaudio_probe.so
run ls -lZ /data/adb/modules/ivanna_audio_probe/system/vendor/lib64/libaudio_probe.so
grep -E " /vendor| /system | /debug_ramdisk| /data/adb" /proc/mounts >> "$R" 2>&1
run ls -la /data/adb/modules/ivanna_audio_probe
run ls -la /data/adb/modules/ivanna_audio_probe/disable /data/adb/modules/ivanna_audio_probe/skip_mount

sec "7. overlay.d / init de Magisk"
run ls -laZ /debug_ramdisk
run ls -la /debug_ramdisk/overlay.d
dmesg 2>/dev/null | grep -iE "magisk|overlay" | head -40 >> "$R"

sec "8. HAL activo: AT_SECURE, dominio, entorno, maps, linker"
P=$(getprop init.svc_debug_pid.vendor.audio-hal)
echo "PID=$P svc=$(getprop init.svc.vendor.audio-hal)" >> "$R"
if [ -n "$P" ] && [ -d "/proc/$P" ]; then
  run readlink /proc/$P/exe
  run cat /proc/$P/attr/current
  echo "--- AT_SECURE (tipo 23 en auxv; 1 = LD_PRELOAD sera DESCARTADO por libc/linker)" >> "$R"
  od -An -tu8 -v -w16 /proc/$P/auxv 2>>"$R" | awk '$1==23{print "AT_SECURE=" $2; f=1} END{if(!f)print "AT_SECURE: linea no encontrada"}' >> "$R"
  echo "--- referencia: AT_SECURE de otros servicios HAL" >> "$R"
  for n in android.hardware.power-service.qti android.hardware.health-service.qti servicemanager; do
    for q in $(pidof $n 2>/dev/null); do
      printf '%s pid=%s ctx=%s ' "$n" "$q" "$(cat /proc/$q/attr/current 2>/dev/null)" >> "$R"
      od -An -tu8 -v -w16 /proc/$q/auxv 2>/dev/null | awk '$1==23{print "AT_SECURE=" $2}' >> "$R"
    done
  done
  echo "--- entorno (puede estar saneado)" >> "$R"
  tr '\000' '\n' < /proc/$P/environ 2>/dev/null | grep -E '^(LD_|ANDROID_)' >> "$R"
  echo "--- maps con probe/pal" >> "$R"
  grep -E "libaudio_probe|libar-pal|libagm|audio.primary" /proc/$P/maps 2>/dev/null | awk '{print $1,$6}' | sort -u -k2 >> "$R"
  run cat /proc/$P/cgroup
fi
run ls -l /sys/fs/selinux/policy
run sha256sum /sys/fs/selinux/policy
run ls -ldZ /vendor/lib64/hw/audio.primary.blair.so /vendor/lib64/libar-pal.so /vendor/lib64/libagm.so

sec "Fin"
echo "Informe: $R   (adjuntarlo completo; no contiene datos personales)"
echo "Informe: $R"
