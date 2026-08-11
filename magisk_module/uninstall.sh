#!/system/bin/sh
# IVANNA OMEGA SUPREME — uninstall.sh v2.2
# FIX v2.2: matar mqa_monitor.sh via MQA_PID_FILE antes de limpiar archivos.
# v2.1 solo mataba el daemon (ivanna_daemon.pid) pero dejaba mqa_monitor.sh
# corriendo como huérfano tras la desinstalación — el monitor seguía llamando
# dumpsys cada 5s y enviando presets a un socket que ya no existía, causando
# wakelock permanente y consumo de batería hasta el próximo reboot.

# ── Matar daemon ──────────────────────────────────────────────────────────────
PID_FILE=/data/adb/ivanna_daemon.pid
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE" 2>/dev/null)
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    rm -f "$PID_FILE"
fi

# ── Matar mqa_monitor.sh (FIX v2.2) ──────────────────────────────────────────
MQA_PID_FILE=/data/adb/ivanna_mqa.pid
if [ -f "$MQA_PID_FILE" ]; then
    MQA_PID=$(cat "$MQA_PID_FILE" 2>/dev/null)
    if [ -n "$MQA_PID" ] && kill -0 "$MQA_PID" 2>/dev/null; then
        if ps -p "$MQA_PID" -o comm= 2>/dev/null | grep -q "mqa_monitor\|sh"; then
            kill "$MQA_PID" 2>/dev/null
        fi
    fi
    rm -f "$MQA_PID_FILE"
fi

# ── Limpiar logs y datos runtime ──────────────────────────────────────────────
rm -f /data/adb/ivanna_omega.log \
      /data/adb/ivanna_omega_boot_counter \
      /data/adb/ivanna_omega_last_boot_ok \
      /data/adb/ivanna_daemon.log \
      /data/adb/ivanna_mqa.log \
      /data/adb/ivanna_concert.log \
      /data/adb/ivanna_control.log

# HRTF dataset y SAF model deployanados por customize.sh — limpieza total.
# rmdir solo si queda vacío (no borra archivos de usuario).
rm -f /data/adb/ivanna_omega/hrtf_dataset.ihr1 \
      /data/adb/ivanna_omega/SAF_model.json \
      /data/adb/ivanna_omega/daemon.log \
      /data/adb/ivanna_omega/daemon.log.old
rmdir /data/adb/ivanna_omega 2>/dev/null

setprop persist.ivanna.magisk_active 0
setprop persist.ivanna.daemon_active 0

# ── SELinux cleanup ────────────────────────────────────────────────────────────
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
