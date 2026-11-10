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
        "deny untrusted_app_25 su:unix_stream_socket connectto" \
        "deny untrusted_app_25 magisk:unix_stream_socket connectto" \
        "deny untrusted_app_27 su:unix_stream_socket connectto" \
        "deny untrusted_app_27 magisk:unix_stream_socket connectto" \
        "deny untrusted_app_29 su:unix_stream_socket connectto" \
        "deny untrusted_app_29 magisk:unix_stream_socket connectto" \
        "deny untrusted_app_30 su:unix_stream_socket connectto" \
        "deny untrusted_app_30 magisk:unix_stream_socket connectto" \
        "deny untrusted_app_32 su:unix_stream_socket connectto" \
        "deny untrusted_app_32 magisk:unix_stream_socket connectto" \
        "deny untrusted_app_33 su:unix_stream_socket connectto" \
        "deny untrusted_app_33 magisk:unix_stream_socket connectto" \
        "deny untrusted_app_34 su:unix_stream_socket connectto" \
        "deny untrusted_app_34 magisk:unix_stream_socket connectto" \
        "deny untrusted_app_35 su:unix_stream_socket connectto" \
        "deny untrusted_app_35 magisk:unix_stream_socket connectto" \
        2>/dev/null || true
    # NOTA (auditoria linea 3): las reglas live de policy (incluido el
    # tipo ivanna_data_file) solo desaparecen con reinicio; el deny de
    # arriba corta el acceso al socket en caliente, pero el resto de la
    # policy viva se purga al reboot. No hay forma soportada de borrar
    # un typeattribute/type live sin reiniciar.
fi

setprop persist.ivanna.magisk_active 0 2>/dev/null
setprop persist.ivanna.daemon_active 0 2>/dev/null
setprop ivanna.concert_mode 0 2>/dev/null
setprop persist.ivanna.version "" 2>/dev/null

# Erradicación de la Plataforma Autónoma
rm -rf /data/adb/ivanna_omega 2>/dev/null
# Canal ASRC (auditoria linea 3): /data/vendor/ivanna_audio solo lo crea
# IVANNA (asrc_link.sh); borrarlo evita dejar snapshots de reloj huerfanos.
rm -rf /data/vendor/ivanna_audio 2>/dev/null

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
