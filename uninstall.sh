#!/system/bin/sh
# IVANNA OMEGA SUPREME — uninstall.sh v2.1
PID_FILE=/data/adb/ivanna_daemon.pid
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE" 2>/dev/null)
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    rm -f "$PID_FILE"
fi
rm -f /data/adb/ivanna_omega.log \
      /data/adb/ivanna_omega_boot_counter \
      /data/adb/ivanna_omega_last_boot_ok \
      /data/adb/ivanna_daemon.log \
      /data/adb/ivanna_mqa.log \
      /data/adb/ivanna_concert.log \
      /data/adb/ivanna_control.log
setprop persist.ivanna.magisk_active 0
setprop persist.ivanna.daemon_active 0
