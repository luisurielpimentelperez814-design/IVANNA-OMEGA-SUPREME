#!/system/bin/sh
# IVANNA OMEGA SUPREME v6.1 - Magisk Realtime Daemon Service
MODDIR=${0%/*}

# FIX: el binario compilado por CI es ivanna_daemon, no omega_daemon
DAEMON_BIN="$MODDIR/system/bin/ivanna_daemon"
LOGFILE="/data/adb/ivanna_omega/daemon.log"

# FIX: ivanna_daemon usa socket abstracto @omega_daemon_socket.
# Los sockets abstractos NO crean archivos en el filesystem, así que
# no hay ruta que comprobar. Se elimina wait_for_socket() que esperaba
# /dev/socket/ivanna_omega (filesystem) y nunca encontraba nada.
# Se sustituye por sleep 1 + kill -0 $DAEMON_PID para comprobar que
# el proceso sigue vivo.

mkdir -p /data/adb/ivanna_omega

echo "[$(date)] Starting IVANNA OMEGA Daemon (ivanna_daemon)" >> "$LOGFILE"

# Log rotation > 10MB
if [ -f "$LOGFILE" ] && [ $(stat -c%s "$LOGFILE") -gt 10485760 ]; then
    mv "$LOGFILE" "${LOGFILE}.old"
fi

if [ ! -f "$DAEMON_BIN" ]; then
    echo "[$(date)] ERROR: $DAEMON_BIN not found — módulo no instalado correctamente" >> "$LOGFILE"
    exit 1
fi

chmod 755 "$DAEMON_BIN"

while true; do
    echo "[$(date)] Launching $DAEMON_BIN" >> "$LOGFILE"
    # FIX: abstract socket @omega_daemon_socket — pasar explícitamente para
    # que coincida con OmegaEngineBridge.SOCKET_PRIMARY en Kotlin.
    "$DAEMON_BIN" --socket "@omega_daemon_socket" >> "$LOGFILE" 2>&1 &
    DAEMON_PID=$!

    # Esperar 1s a que el daemon arranque y abra el socket abstracto
    sleep 1

    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "[$(date)] Daemon PID=$DAEMON_PID activo — @omega_daemon_socket listo" >> "$LOGFILE"
        setprop persist.ivanna.daemon_active 1
    else
        echo "[$(date)] ERROR: daemon terminó inmediatamente (código=$(wait $DAEMON_PID; echo $?))" >> "$LOGFILE"
        setprop persist.ivanna.daemon_active 0
        sleep 2
        continue
    fi

    wait "$DAEMON_PID"
    EXIT_CODE=$?
    echo "[$(date)] Daemon PID=$DAEMON_PID terminó (código=$EXIT_CODE). Reiniciando en 2s..." >> "$LOGFILE"
    setprop persist.ivanna.daemon_active 0
    sleep 2
done


# SAF MODEL DEPLOY — copy from assets if present
SAF_ASSET="$MODDIR/system/etc/ivanna_omega/SAF_model_total.json"
SAF_DEST="/data/adb/ivanna_omega/SAF_model_total.json"

if [ -f "$SAF_ASSET" ]; then
    cp "$SAF_ASSET" "$SAF_DEST"
    chmod 644 "$SAF_DEST"
    echo "[$(date)] SAF_model_total.json deployed" >> "$LOGFILE"
fi