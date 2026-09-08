#!/system/bin/sh

MODDIR=${0%/*}

DAEMON="$MODDIR/system/bin/ivanna_daemon"
STATE="/data/adb/ivanna_omega"
LOG="$STATE/daemon.log"

mkdir -p "$STATE"

chmod 755 "$DAEMON" 2>/dev/null
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
else
    echo "$(date) ERROR daemon murió al iniciar" >> "$LOG"
fi
