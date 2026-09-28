#!/system/bin/sh
# ==============================================================================
# IVANNA Autonomous Runtime Platform
# Destrucción Atómica y Borrado Seguro
# ==============================================================================

# Detener núcleo autónomo y componentes aislados
killall -9 ivanna_daemon mqa_monitor.sh service.sh 2>/dev/null

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

# Erradicación de la Plataforma Autónoma
rm -rf /data/adb/ivanna_omega 2>/dev/null

# Limpieza de rastros legacy
rm -f /data/adb/ivanna_omega.log \
      /data/adb/ivanna_omega_boot_counter \
      /data/adb/ivanna_omega_last_boot_ok \
      /data/adb/ivanna_daemon.pid \
      /data/adb/ivanna_mqa.pid \
      /data/adb/ivanna_daemon.log \
      /data/adb/ivanna_daemon.log.old \
      /data/adb/ivanna_mqa.log \
      /data/adb/ivanna_concert.log \
      /data/adb/ivanna_control.log \
      /data/adb/ivanna_omega/diag.log \
      /data/adb/ivanna_omega/diag.log.old 2>/dev/null

echo " "
echo " █▀▀ █▀▀█ █▀▀█ █▀▀█ █▀▀▄ █▀▀█ ▀▀█▀▀ █▀▀ █▀▀▄ "
echo " █▀▀ █▄▄▀ █▄▄▀ █▄▄█ █  █ █▄▄█   █   █▀▀ █  █ "
echo " ▀▀▀ ▀ ▀▀ ▀ ▀▀ ▀  ▀ ▀▀▀  ▀  ▀   ▀   ▀▀▀ ▀  ▀ "
echo " "
echo "✅ IVANNA Autonomous Runtime Platform ha sido erradicada."
echo "   • Motor Inteligente purgado."
echo "   • Observatorio e Historial Forense destruidos."
echo "   • Nodos aislados terminados."
echo ""
echo "⚠ REINICIO DE HARDWARE REQUERIDO"
