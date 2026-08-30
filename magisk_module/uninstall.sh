#!/system/bin/sh
# IVANNA OMEGA SUPREME — uninstall.sh (Googleplex Perfect Edition)
# Erradicación absoluta de la presencia de IVANNA del subsistema Android.

PID_FILE=/data/adb/ivanna_daemon.pid
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE" 2>/dev/null)
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null
        sleep 0.5
        kill -9 "$PID" 2>/dev/null
    fi
    rm -f "$PID_FILE"
fi

ORPHANS=$(pidof ivanna_daemon 2>/dev/null)
if [ -n "$ORPHANS" ]; then
    kill -9 $ORPHANS 2>/dev/null
fi

MQA_PID_FILE=/data/adb/ivanna_mqa.pid
if [ -f "$MQA_PID_FILE" ]; then
    MQA_PID=$(cat "$MQA_PID_FILE" 2>/dev/null)
    if [ -n "$MQA_PID" ] && kill -0 "$MQA_PID" 2>/dev/null; then
        kill -9 "$MQA_PID" 2>/dev/null
    fi
    rm -f "$MQA_PID_FILE"
fi

if command -v magiskpolicy >/dev/null 2>&1; then
    magiskpolicy --live \
        "deny untrusted_app su:unix_stream_socket connectto" \
        "deny untrusted_app magisk:unix_stream_socket connectto" \
        "deny isolated_app su:unix_stream_socket connectto" \
        "deny isolated_app magisk:unix_stream_socket connectto" \
        2>/dev/null || true
fi

setprop persist.ivanna.magisk_active 0 2>/dev/null
setprop persist.ivanna.daemon_active 0 2>/dev/null
setprop ivanna.concert_mode 0 2>/dev/null
setprop persist.ivanna.version "" 2>/dev/null

rm -f /data/adb/ivanna_omega.log \
      /data/adb/ivanna_omega_boot_counter \
      /data/adb/ivanna_omega_last_boot_ok \
      /data/adb/ivanna_daemon.log \
      /data/adb/ivanna_daemon.log.old \
      /data/adb/ivanna_mqa.log \
      /data/adb/ivanna_concert.log \
      /data/adb/ivanna_control.log 2>/dev/null

rm -rf /data/adb/ivanna_omega 2>/dev/null

echo " "
echo " █▀▀ █▀▀█ █▀▀█ █▀▀█ █▀▀▄ █▀▀█ ▀▀█▀▀ █▀▀ █▀▀▄ "
echo " █▀▀ █▄▄▀ █▄▄▀ █▄▄█ █  █ █▄▄█   █   █▀▀ █  █ "
echo " ▀▀▀ ▀ ▀▀ ▀ ▀▀ ▀  ▀ ▀▀▀  ▀  ▀   ▀   ▀▀▀ ▀  ▀ "
echo " "
echo "✅ IVANNA OMEGA SUPREME neutralizado por completo."
echo "   • Subprocesos DSP de Kernel erradicados."
echo "   • Parámetros acústicos purgandos."
echo "   • Propiedades persistentes destruidas."
echo ""
echo "⚠ REINICIO DE HARDWARE REQUERIDO para finalizar desvinculación"
echo "  de la cadena AudioFlinger."
