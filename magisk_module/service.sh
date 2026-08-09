#!/system/bin/sh
# IVANNA OMEGA SUPREME v6.2 - Magisk Realtime Daemon Service
MODDIR=${0%/*}

DAEMON_BIN="$MODDIR/system/bin/ivanna_daemon"
LOGFILE="/data/adb/ivanna_omega/daemon.log"
LAST_OK="/data/adb/ivanna_omega_last_boot_ok"

mkdir -p /data/adb/ivanna_omega

# Log rotation > 10MB
if [ -f "$LOGFILE" ] && [ "$(stat -c%s "$LOGFILE" 2>/dev/null || echo 0)" -gt 10485760 ]; then
    mv "$LOGFILE" "${LOGFILE}.old"
fi

echo "[$(date)] service.sh v6.2 iniciado" >> "$LOGFILE"

# ── SELinux: cargar reglas del módulo en tiempo real ─────────────────────────
# Sin esto, untrusted_app (la app) no puede connect() al socket abstracto del
# daemon root — Android lo bloquea silenciosamente.
SEPOLICY_RULE="$MODDIR/sepolicy.rule"
if [ -f "$SEPOLICY_RULE" ]; then
    if command -v magiskpolicy >/dev/null 2>&1; then
        magiskpolicy --live --apply "$SEPOLICY_RULE" >> "$LOGFILE" 2>&1
        echo "[$(date)] SELinux rules aplicadas desde $SEPOLICY_RULE" >> "$LOGFILE"
    else
        echo "[$(date)] WARN: magiskpolicy no encontrado — SELinux rules NO aplicadas" >> "$LOGFILE"
    fi
else
    echo "[$(date)] WARN: sepolicy.rule no encontrado — conexiones desde la app pueden fallar" >> "$LOGFILE"
fi

# ── SAF MODEL DEPLOY ──────────────────────────────────────────────────────────
# FIX (auditoría 2026-08-09):
#   Antes SAF_ASSET apuntaba a "system/etc/ivanna_omega/SAF_model_total.json"
#   y SAF_DEST a "/data/adb/ivanna_omega/SAF_model_total.json". Pero:
#     * customize.sh:12 despliega el modelo REAL desde "$MODPATH/saf/SAF_model.json".
#     * app/src/main/java/.../SaFEngine.kt:155 busca en
#       "/data/adb/ivanna_omega/SAF_model.json" (path canónico app).
#     * SaFOptimizer.cpp x8 referencias, SaFBridge.kt, todos usan "SAF_model.json".
#   Resultado: en la instalación real el `cp` fallaba en silencio (el `if [ -f ]`
#   no tenía `else`), /data/adb/ivanna_omega/SAF_model_total.json nunca aparecía,
#   y el motor Φ_SAF caía a constantes horneadas SIN AVISAR. La personalización
#   HRTF (214 subjects, 7-PCA) quedaba muerta para todos los usuarios.
#
#   v6.2 → usar el mismo asset que customize.sh y el mismo path que la app.
#   Se añade `else` explícito para que el fallo aparezca en el log de boot,
#   no como silencio.
SAF_ASSET="$MODDIR/saf/SAF_model.json"
SAF_DEST="/data/adb/ivanna_omega/SAF_model.json"
if [ -f "$SAF_ASSET" ]; then
    cp -f "$SAF_ASSET" "$SAF_DEST"
    chmod 644 "$SAF_DEST"
    echo "[$(date)] SAF_model.json deployed → $SAF_DEST ($(stat -c%s "$SAF_ASSET" 2>/dev/null || echo ?) bytes)" >> "$LOGFILE"
else
    echo "[$(date)] WARN: SAF_model.json NO encontrado en $SAF_ASSET — motor Φ_SAF usará constantes horneadas (214 subjects baked in SaFOptimizer.cpp)" >> "$LOGFILE"
fi

# ── Verificar binario ─────────────────────────────────────────────────────────
if [ ! -f "$DAEMON_BIN" ]; then
    echo "[$(date)] ERROR: $DAEMON_BIN not found — módulo no instalado correctamente" >> "$LOGFILE"
    exit 1
fi
chmod 755 "$DAEMON_BIN"

# ── Watchdog loop ─────────────────────────────────────────────────────────────
# FIX 1: la prop persist.ivanna.daemon_active se quedaba en 1 si este script
#        moria sin pasar por el final del bucle (kill, desactivar el modulo,
#        shutdown). La app la leia y mostraba ONLINE con el socket muerto.
#        Un trap la baja pase lo que pase.
# FIX 2: backoff exponencial. Si el daemon crashea al arrancar (SHM ocupada,
#        SELinux, binario incompatible) el bucle reintentaba cada 2 s para
#        siempre: log creciendo sin parar, wakelocks y bateria. Ahora
#        2s -> 4 -> 8 ... hasta 60 s, y se resetea en cuanto el daemon
#        aguanta encendido mas de 30 s.
BACKOFF=2
BACKOFF_MAX=60
PID_FILE=/data/adb/ivanna_daemon.pid
MQA_PID=""

cleanup() {
    setprop persist.ivanna.daemon_active 0
    [ -n "$DAEMON_PID" ] && kill "$DAEMON_PID" 2>/dev/null
    # FIX: matar mqa_monitor si estaba corriendo
    [ -n "$MQA_PID" ] && kill "$MQA_PID" 2>/dev/null
    # FIX: limpiar PID file en cualquier salida
    rm -f "$PID_FILE"
    echo "[$(date)] service.sh saliendo — daemon_active=0" >> "$LOGFILE"
}
trap cleanup EXIT HUP INT TERM

while true; do
    echo "[$(date)] Launching $DAEMON_BIN" >> "$LOGFILE"
    START_TS=$(date +%s)
    "$DAEMON_BIN" --socket "@omega_daemon_socket" --realtime >> "$LOGFILE" 2>&1 &
    DAEMON_PID=$!

    # FIX: escribir PID file para que uninstall.sh pueda matar el daemon.
    # Antes service.sh guardaba DAEMON_PID solo en variable de shell y
    # uninstall.sh leía /data/adb/ivanna_daemon.pid que NUNCA existía,
    # dejando al daemon corriendo tras la desinstalación hasta el reboot.
    echo "$DAEMON_PID" > "$PID_FILE"

    # Esperar hasta 3s a que el daemon arranque Y publique el socket abstracto.
    # FIX: antes solo se comprobaba kill -0 (proceso vivo). El proceso puede
    # estar vivo pero el socket aún no bindeado (SHM init lento, SELinux, etc.)
    # — la app leía daemon_active=1 e intentaba connect() con ECONNREFUSED.
    # Ahora se espera que @omega_daemon_socket aparezca en /proc/net/unix.
    SOCK_READY=0
    SOCK_TRIES=0
    while [ $SOCK_TRIES -lt 6 ]; do
        sleep 0.5
        kill -0 "$DAEMON_PID" 2>/dev/null || break
        grep -q " @omega_daemon_socket$" /proc/net/unix 2>/dev/null && { SOCK_READY=1; break; }
        SOCK_TRIES=$((SOCK_TRIES + 1))
    done

    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        if [ "$SOCK_READY" -eq 1 ]; then
            echo "[$(date)] Daemon PID=$DAEMON_PID activo — @omega_daemon_socket listo" >> "$LOGFILE"
        else
            echo "[$(date)] WARN: Daemon PID=$DAEMON_PID vivo pero socket no detectado en /proc/net/unix tras 3s" >> "$LOGFILE"
        fi
        setprop persist.ivanna.daemon_active 1
        touch "$LAST_OK"

        # FIX: lanzar mqa_monitor.sh como background daemon.
        # Antes nunca se iniciaba — el auto-preset por app (Tidal→Flat,
        # Spotify→Warm, YouTube→Spatial) nunca corría aunque el código
        # estaba completo en mqa_monitor.sh.
        if [ -f "$MODDIR/mqa_monitor.sh" ] && [ -x "$MODDIR/mqa_monitor.sh" ]; then
            # Matar monitor anterior si estaba corriendo de una iteración previa
            [ -n "$MQA_PID" ] && kill "$MQA_PID" 2>/dev/null
            "$MODDIR/mqa_monitor.sh" "$MODDIR" >> "$LOGFILE" 2>&1 &
            MQA_PID=$!
            echo "[$(date)] mqa_monitor.sh PID=$MQA_PID iniciado" >> "$LOGFILE"
        fi
    else
        echo "[$(date)] ERROR: daemon terminó inmediatamente — reintento en ${BACKOFF}s" >> "$LOGFILE"
        setprop persist.ivanna.daemon_active 0
        rm -f "$PID_FILE"
        sleep "$BACKOFF"
        BACKOFF=$(( BACKOFF * 2 )); [ "$BACKOFF" -gt "$BACKOFF_MAX" ] && BACKOFF=$BACKOFF_MAX
        continue
    fi

    wait "$DAEMON_PID"
    EXIT_CODE=$?
    UPTIME=$(( $(date +%s) - START_TS ))
    setprop persist.ivanna.daemon_active 0
    rm -f "$PID_FILE"
    # Matar monitor cuando el daemon muere — ya no tiene a quién enviar comandos
    [ -n "$MQA_PID" ] && kill "$MQA_PID" 2>/dev/null; MQA_PID=""
    if [ "$UPTIME" -ge 30 ]; then
        BACKOFF=2
    else
        BACKOFF=$(( BACKOFF * 2 )); [ "$BACKOFF" -gt "$BACKOFF_MAX" ] && BACKOFF=$BACKOFF_MAX
    fi
    echo "[$(date)] Daemon PID=$DAEMON_PID terminó (código=$EXIT_CODE, uptime=${UPTIME}s). Reiniciando en ${BACKOFF}s..." >> "$LOGFILE"
    sleep "$BACKOFF"
done
