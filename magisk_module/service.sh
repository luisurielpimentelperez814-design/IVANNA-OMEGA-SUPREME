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
# FIX: estaba después del `done` del while loop — código muerto. Movido antes.
SAF_ASSET="$MODDIR/system/etc/ivanna_omega/SAF_model_total.json"
SAF_DEST="/data/adb/ivanna_omega/SAF_model_total.json"
if [ -f "$SAF_ASSET" ]; then
    cp -f "$SAF_ASSET" "$SAF_DEST"
    chmod 644 "$SAF_DEST"
    echo "[$(date)] SAF_model_total.json deployed → $SAF_DEST" >> "$LOGFILE"
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

cleanup() {
    setprop persist.ivanna.daemon_active 0
    [ -n "$DAEMON_PID" ] && kill "$DAEMON_PID" 2>/dev/null
    echo "[$(date)] service.sh saliendo — daemon_active=0" >> "$LOGFILE"
}
trap cleanup EXIT HUP INT TERM

while true; do
    echo "[$(date)] Launching $DAEMON_BIN" >> "$LOGFILE"
    START_TS=$(date +%s)
    "$DAEMON_BIN" --socket "@omega_daemon_socket" --realtime >> "$LOGFILE" 2>&1 &
    DAEMON_PID=$!

    # Esperar 1s a que el daemon arranque y abra el socket abstracto
    sleep 1

    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "[$(date)] Daemon PID=$DAEMON_PID activo — @omega_daemon_socket listo" >> "$LOGFILE"
        setprop persist.ivanna.daemon_active 1
        # Marcar boot exitoso para el anti-bootloop de post-fs-data.sh
        touch "$LAST_OK"
    else
        echo "[$(date)] ERROR: daemon terminó inmediatamente — reintento en ${BACKOFF}s" >> "$LOGFILE"
        setprop persist.ivanna.daemon_active 0
        sleep "$BACKOFF"
        BACKOFF=$(( BACKOFF * 2 )); [ "$BACKOFF" -gt "$BACKOFF_MAX" ] && BACKOFF=$BACKOFF_MAX
        continue
    fi

    wait "$DAEMON_PID"
    EXIT_CODE=$?
    UPTIME=$(( $(date +%s) - START_TS ))
    setprop persist.ivanna.daemon_active 0
    if [ "$UPTIME" -ge 30 ]; then
        BACKOFF=2
    else
        BACKOFF=$(( BACKOFF * 2 )); [ "$BACKOFF" -gt "$BACKOFF_MAX" ] && BACKOFF=$BACKOFF_MAX
    fi
    echo "[$(date)] Daemon PID=$DAEMON_PID terminó (código=$EXIT_CODE, uptime=${UPTIME}s). Reiniciando en ${BACKOFF}s..." >> "$LOGFILE"
    sleep "$BACKOFF"
done
