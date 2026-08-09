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
# HRTF dataset desplegado por customize.sh — limpieza total al desinstalar.
# El directorio se elimina sólo si queda vacío (rmdir), para no borrar
# archivos del usuario que pudieran haber sido colocados manualmente.
rm -f /data/adb/ivanna_omega/hrtf_dataset.ihr1 \
      /data/adb/ivanna_omega/daemon.log \
      /data/adb/ivanna_omega/daemon.log.old
rmdir /data/adb/ivanna_omega 2>/dev/null
setprop persist.ivanna.magisk_active 0
setprop persist.ivanna.daemon_active 0

# ── SELinux cleanup ────────────────────────────────────────────────────────────
# Las reglas `allow` inyectadas con `magiskpolicy --live` en boot (customize.sh
# + service.sh) permanecen activas en el kernel hasta el próximo reboot:
# el módulo ya no existe pero untrusted_app → su:unix_stream_socket { connectto }
# sigue viva, aumentando la superficie de ataque residual.
#
# Intentamos la remoción en caliente con `deny` (anula el allow en kernels que
# lo soporten en runtime). En kernels 4.14+ sin overlayfs esto puede no tener
# efecto; el reboot a continuación es la garantía.
if command -v magiskpolicy >/dev/null 2>&1; then
    magiskpolicy --live \
        "deny untrusted_app su:unix_stream_socket connectto" \
        "deny untrusted_app magisk:unix_stream_socket connectto" \
        "deny isolated_app su:unix_stream_socket connectto" \
        "deny isolated_app magisk:unix_stream_socket connectto" \
        2>/dev/null || true
    ui_print "- SELinux: deny rules aplicadas (efectivas si kernel lo soporta)"
else
    ui_print "- SELinux: magiskpolicy no disponible — reglas activas hasta el reboot"
fi
ui_print ""
ui_print "⚠  REINICIA el dispositivo para limpiar completamente"
ui_print "   las reglas SELinux de ivanna_omega del kernel en vivo."
