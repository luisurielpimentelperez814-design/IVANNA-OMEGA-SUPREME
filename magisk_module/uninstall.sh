#!/system/bin/sh
# IVANNA OMEGA SUPREME — uninstall.sh v3.0
# FIX v3.0: limpieza completa de /data/adb/ivanna_omega/ incluyendo
#   hrtf/, rir/ y sofa/ (antes solo se borraban 2 archivos sueltos,
#   dejando cientos de MB de datasets sin limpiar).
#   Fallback pidof para matar daemon si PID_FILE se perdió.
#   Limpieza de session crash counter.

# ── Matar ivanna_daemon ───────────────────────────────────────────────────────
PID_FILE=/data/adb/ivanna_daemon.pid
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE" 2>/dev/null)
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null
        sleep 0.5
        kill -0 "$PID" 2>/dev/null && kill -9 "$PID" 2>/dev/null
    fi
    rm -f "$PID_FILE"
fi
# FIX v3.0: fallback pidof — si PID_FILE se perdió pero el daemon sigue vivo
# (kill -9 del service.sh padre, reflash sin reboot), lo matamos igual.
ORPHANS=$(pidof ivanna_daemon 2>/dev/null)
if [ -n "$ORPHANS" ]; then
    kill $ORPHANS 2>/dev/null
    sleep 0.3
    kill -9 $ORPHANS 2>/dev/null
fi

# ── Matar mqa_monitor.sh ──────────────────────────────────────────────────────
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

# ── SELinux cleanup ────────────────────────────────────────────────────────────
if command -v magiskpolicy >/dev/null 2>&1; then
    magiskpolicy --live \
        "deny untrusted_app su:unix_stream_socket connectto" \
        "deny untrusted_app magisk:unix_stream_socket connectto" \
        "deny isolated_app su:unix_stream_socket connectto" \
        "deny isolated_app magisk:unix_stream_socket connectto" \
        2>/dev/null || true
fi

# ── Props ─────────────────────────────────────────────────────────────────────
setprop persist.ivanna.magisk_active 0 2>/dev/null
setprop persist.ivanna.daemon_active 0 2>/dev/null
setprop ivanna.concert_mode 0 2>/dev/null

# ── Logs runtime ─────────────────────────────────────────────────────────────
rm -f /data/adb/ivanna_omega.log \
      /data/adb/ivanna_omega_boot_counter \
      /data/adb/ivanna_omega_last_boot_ok \
      /data/adb/ivanna_daemon.log \
      /data/adb/ivanna_daemon.log.old \
      /data/adb/ivanna_mqa.log \
      /data/adb/ivanna_concert.log \
      /data/adb/ivanna_control.log 2>/dev/null

# ── Datasets desplegados por customize.sh ─────────────────────────────────────
# FIX v3.0: antes solo se borraban 2 archivos sueltos. Los datasets
# completos (hrtf/, rir/, sofa/) son cientos de MB que quedaban en /data/.
# rmdir --ignore-fail-on-non-empty sería ideal pero no siempre está
# disponible en Android — usamos rm -rf de los subdirectorios conocidos.
rm -f  /data/adb/ivanna_omega/SAF_model.json \
       /data/adb/ivanna_omega/SAF_model_espacial.json \
       /data/adb/ivanna_omega/SAF_model_total.json \
       /data/adb/ivanna_omega/hrtf_dataset.ihr1 \
       /data/adb/ivanna_omega/omega_control_snapshot \
       /data/adb/ivanna_omega/daemon.log \
       /data/adb/ivanna_omega/daemon.log.old 2>/dev/null

# Directorios de datasets — rm -rf seguro porque son directorios del módulo
# con nombres que no existen en AOSP ni en ninguna app del sistema.
rm -rf /data/adb/ivanna_omega/hrtf/ \
       /data/adb/ivanna_omega/rir/ \
       /data/adb/ivanna_omega/sofa/ 2>/dev/null

# Directorio raíz — rmdir solo si quedó vacío (no borra archivos de usuario
# que pudieran haberse colocado ahí manualmente).
rmdir /data/adb/ivanna_omega 2>/dev/null

echo ""
echo "✅ IVANNA OMEGA SUPREME desinstalado"
echo "   • Daemon, MQA monitor y logs eliminados"
echo "   • Datasets HRTF, RIR y SOFA limpiados de /data/adb/"
echo "   • Props de sistema bajadas"
echo ""
echo "⚠  REINICIA el dispositivo para:"
echo "   • Limpiar reglas SELinux del kernel en vivo"
echo "   • Desregistrar libomega_effect.so de AudioFlinger"
