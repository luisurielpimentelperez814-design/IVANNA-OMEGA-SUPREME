#!/system/bin/sh

MODDIR=${0%/*}

DAEMON="$MODDIR/system/bin/ivanna_daemon"
STATE="/data/adb/ivanna_omega"
LOG="$STATE/daemon.log"

mkdir -p "$STATE"

# Rotación de log: daemon.log crecía sin límite en /data (>1 MiB → .old).
if [ -f "$LOG" ]; then
    LOG_SZ=$(wc -c < "$LOG" 2>/dev/null || echo 0)
    case "$LOG_SZ" in ''|*[!0-9]*) LOG_SZ=0 ;; esac
    if [ "$LOG_SZ" -gt 1048576 ]; then
        mv -f "$LOG" "$LOG.old" 2>/dev/null
    fi
fi

chmod 755 "$DAEMON" 2>/dev/null
chmod 755 "$MODDIR/system/bin/ivanna_client" 2>/dev/null
chmod 755 "$MODDIR/service.sh" 2>/dev/null

if pidof ivanna_daemon >/dev/null 2>&1; then
    # FIX (síntoma exacto reportado: panel muestra DAEMON=CORRIENDO en verde
    # y SOCKET=DESCONECTADO en rojo al mismo tiempo): pidof solo confirma que
    # existe un proceso con ese nombre, NUNCA que esté sirviendo el socket de
    # verdad. Si un arranque anterior dejó el proceso colgado ANTES de
    # llegar a bind() (o atorado en su propio loop de reintento — ver
    # ivanna_daemon.cpp, 12 intentos de 500ms), este script se rendía aquí
    # con "ya estaba activo" y jamás volvía a intentar nada — el usuario
    # quedaba atorado hasta un reinicio completo del dispositivo, sin que
    # nada en el módulo lo detectara ni corrigiera solo.
    # Mismo comando exacto que ya usa la app (MagiskBridge.kt) para decidir
    # SOCKET=CONECTADO/DESCONECTADO, para no divergir de lo que el usuario ve.
    if grep -aq omega_daemon_socket /proc/net/unix 2>/dev/null; then
        echo "$(date) daemon ya estaba activo y el socket ya está bindeado" >> "$LOG"
        exit 0
    fi
    echo "$(date) proceso ivanna_daemon encontrado pero SIN socket en /proc/net/unix — instancia colgada de un arranque anterior, matando y relanzando" >> "$LOG"
    for p in $(pidof ivanna_daemon); do kill -9 "$p" 2>/dev/null; done
    sleep 1
fi

# Anti-bootloop: Si el módulo está en safe_mode, no iniciar daemon
if [ -f "$MODDIR/.safe_mode" ]; then
    echo "$(date) SAFE_MODE activo — abortando arranque de ivanna_daemon" >> "$LOG"
    setprop persist.ivanna.daemon_active 0 2>/dev/null
    exit 0
fi

# Detección de bucles de choque continuos
CRASH_COUNT_FILE="$STATE/daemon_crash_streak"
CRASHES=$(cat "$CRASH_COUNT_FILE" 2>/dev/null || echo 0)
# Contador corrupto/no numérico: tratar como 0 (evita "integer expression expected").
case "$CRASHES" in ''|*[!0-9]*) CRASHES=0 ;; esac
if [ "$CRASHES" -ge 5 ]; then
    echo "$(date) FATAL: 5 caídas consecutivas de ivanna_daemon. Activando .safe_mode" >> "$LOG"
    touch "$MODDIR/.safe_mode"
    setprop persist.ivanna.daemon_active 0 2>/dev/null
    exit 1
fi

rm -f "$STATE/daemon.pid"
# FIX (socket nunca conectaba, confirmado en dispositivo real por el usuario):
# un daemon anterior muerto sin cleanup podía dejar omega_shm corrupto o con
# tamaño viejo. El siguiente arranque de service.sh nunca lo limpiaba, así
# que el daemon nuevo podía fallar al recrear/mapear la SHM esperada por la
# app. Verificado por eliminación contra el resto de su secuencia manual:
# chmod 755 y "rm daemon.pid" ya los hacía este script; --socket explícito
# es idéntico al default (DEFAULT_SOCKET_PATH="@omega_daemon_socket" en
# ivanna_daemon.cpp) — ninguna de esas tres cambiaba nada. Esta línea sí.
rm -f "$STATE/omega_shm"

echo "$(date) iniciando ivanna_daemon" >> "$LOG"

"$DAEMON" >> "$LOG" 2>&1 &

PID=$!
echo "$PID" > "$STATE/daemon.pid"

sleep 3

if kill -0 "$PID" 2>/dev/null; then
    echo "$(date) daemon activo PID=$PID" >> "$LOG"
    setprop persist.ivanna.daemon_active 1 2>/dev/null
    echo "0" > "$CRASH_COUNT_FILE"
    touch /data/adb/ivanna_omega_last_boot_ok 2>/dev/null
    CLIENT="$MODDIR/system/bin/ivanna_client"
    if [ -x "$CLIENT" ]; then
        if [ -f "$STATE/supreme_axes.cfg" ]; then
            "$CLIENT" "$(cat "$STATE/supreme_axes.cfg")" >> "$LOG" 2>&1
            echo "$(date) 5 Ejes Supremos restaurados desde supreme_axes.cfg" >> "$LOG"
        else
            "$CLIENT" "SET_SUPREME_PRESET:supreme" >> "$LOG" 2>&1
            echo "$(date) 5 Ejes Supremos inicializados con preset supreme" >> "$LOG"
        fi
    fi
else
    echo "$(date) ERROR daemon murió al iniciar" >> "$LOG"
    setprop persist.ivanna.daemon_active 0 2>/dev/null
    echo $((CRASHES + 1)) > "$CRASH_COUNT_FILE"
fi
