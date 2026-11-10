#!/system/bin/sh

MODDIR=${0%/*}

DAEMON="$MODDIR/system/bin/ivanna_daemon"
STATE="/data/adb/ivanna_omega"
LOG="$STATE/daemon.log"

mkdir -p "$STATE" || exit 1

# Supervision continua: un solo supervisor vivo (verificado por cmdline, no solo por PID).
if [ -z "$IVANNA_SUPERVISED" ] && [ -f "$MODDIR/supervisor.sh" ]; then
    SUP_OLD=$(cat "$STATE/supervisor.pid" 2>/dev/null)
    case "$SUP_OLD" in ''|*[!0-9]*) SUP_OLD="" ;; esac
    if [ -z "$SUP_OLD" ] || ! grep -aq supervisor.sh "/proc/$SUP_OLD/cmdline" 2>/dev/null; then
        chmod 755 "$MODDIR/supervisor.sh" 2>/dev/null
        nohup sh "$MODDIR/supervisor.sh" >/dev/null 2>&1 &
    fi
fi

# FIX (omega_shm, causa raiz MLS): la app (untrusted_app:s0:cXXX,...) leia
# /data/adb/ivanna_omega/omega_shm pero su write()/mmap(PROT_WRITE) era
# denegado en bucle (>100 denials en 10 s, ver logcat "denied { write }
# ... omega_shm"). Las reglas "allow untrusted_app adb_data_file file write"
# no bastaban: aplicadas en vivo con rc=0 los denials seguian, porque el bloqueo
# es una restriccion MLS (categorias distintas entre app y archivo s0), que es
# independiente de las reglas allow. Verificado en Moto G85 (Android 15,
# Magisk 30.7): marcar el tipo como mlstrustedobject llevo los denials a 0.
#
# CAMBIO (auditoria linea 3, 2026-11-10): el bypass MLS ya NO se aplica a
# adb_data_file entero (eso eximia de MLS a TODO /data/adb/, de todos los
# modulos). Ahora el bypass vive solo en el tipo dedicado ivanna_data_file
# (sepolicy.rule seccion 0) y service.sh reetiqueta /data/adb/ivanna_omega
# y /data/vendor/ivanna_audio a ese tipo justo despues de aplicar la
# policy (bloque SELINUX-RELABEL, mas abajo). Los archivos nuevos heredan
# el tipo del directorio padre, asi que omega_shm y el snapshot nacen ya
# como ivanna_data_file. Si el reetiquetado falla, queda registrado en
# daemon.log (SELINUX-BOOT) y el control por socket sigue funcionando;
# NO se reanade el typeattribute amplio como fallback silencioso.
# (Antes aqui: magiskpolicy --live "typeattribute adb_data_file mlstrustedobject" — eliminado.)

# FIX (Ruta B, Moto G85 2026-11-09): Magisk 31.1 NO carga sepolicy.rule en boot,
# así que las reglas live de customize.sh no sobreviven al reinicio. Sin ellas,
# audioserver no puede leer el snapshot del demonio
# (/data/adb/ivanna_omega/omega_control_snapshot) y el efecto queda sin control
# aunque el daemon responda. Se reaplica la policy COMPLETA en vivo en cada
# arranque, con el mismo fallback línea por línea de customize.sh
# (magiskpolicy --live --apply aborta al primer tipo inexistente en esta
# versión de Android y no aplicaría ninguna regla).
# v2.5.386 — A PRUEBA DE BRICK: magiskpolicy sin timeout podía colgar el
# arranque si el binario se quedaba esperando (nunca se vio, pero service.sh
# corre en boot y NO puede permitirse un cuelgue). run_bounded ejecuta con
# `timeout 15` cuando el applet existe (toybox) y directo cuando no; el
# bucle línea por línea además está topado a 200 reglas.
run_bounded() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 15 "$@" 2>/dev/null
    else
        "$@" 2>/dev/null
    fi
}
if command -v magiskpolicy >/dev/null 2>&1 && [ -f "$MODDIR/sepolicy.rule" ]; then
    if run_bounded magiskpolicy --live --apply "$MODDIR/sepolicy.rule"; then
        echo "$(date) SELINUX-BOOT: sepolicy.rule aplicada (--apply)" >> "$LOG"
    else
        SE_OK=0; SE_FAIL=0; SE_N=0
        while IFS= read -r RULE; do
            SE_N=$((SE_N+1)); [ "$SE_N" -gt 200 ] && break
            case "$RULE" in ''|'#'*) continue ;; esac
            if run_bounded magiskpolicy --live "$RULE"; then SE_OK=$((SE_OK+1)); else SE_FAIL=$((SE_FAIL+1)); fi
        done < "$MODDIR/sepolicy.rule"
        echo "$(date) SELINUX-BOOT: sepolicy.rule línea por línea: $SE_OK aplicadas, $SE_FAIL omitidas (tipos ausentes en esta versión)" >> "$LOG"
    fi
else
    echo "$(date) SELINUX-BOOT: magiskpolicy o sepolicy.rule no disponible — policy no reaplicada en boot" >> "$LOG"
fi

# SELINUX-RELABEL (auditoria linea 3, 2026-11-10): alcance minimo del
# bypass MLS. Solo /data/adb/ivanna_omega (estado, SHM, snapshot) y
# /data/vendor/ivanna_audio (canal ASRC, via asrc_link.sh) se etiquetan
# como ivanna_data_file, el unico tipo mlstrustedobject. Debe correr
# DESPUES de aplicar sepolicy.rule (el tipo tiene que existir en la
# policy viva antes de chcon) y ANTES de levantar el daemon (los
# archivos que el daemon crea heredan el tipo del directorio padre).
# Verificacion: ls -Zd debe mostrar ivanna_data_file. Si falla, se
# registra y se sigue: el socket de control funciona igual; solo la
# escritura del SHM por la app puede volver a dar denials MLS.
if command -v chcon >/dev/null 2>&1; then
    IVANNA_CTX="u:object_r:ivanna_data_file:s0"
    chcon -R "$IVANNA_CTX" "$STATE" 2>/dev/null
    if ls -Zd "$STATE" 2>/dev/null | grep -q ivanna_data_file; then
        echo "$(date) SELINUX-BOOT: reetiquetado ivanna_data_file OK ($STATE)" >> "$LOG"
    else
        echo "$(date) SELINUX-BOOT: FALLO reetiquetado ivanna_data_file ($STATE sigue sin ese tipo; revisar sepolicy.rule seccion 0 y magiskpolicy --live 'type ivanna_data_file')" >> "$LOG"
    fi
else
    echo "$(date) SELINUX-BOOT: chcon no disponible — sin reetiquetado ivanna_data_file" >> "$LOG"
fi

# Load the decision engine if present; keep boot compatible with older packages.
CORE="$MODDIR/core/ivanna_autonomous_core.sh"
if [ -f "$CORE" ]; then
    . "$CORE" || {
        echo "$(date) ERROR: no se pudo cargar el motor autónomo" >> "$LOG"
        exit 1
    }
fi

# SAFMUS: map whole-device resource conditions before starting the shared daemon.
# The probe is read-only; it publishes a bounded budget for later consumers.
SAFMUS_AUDIO_AUDIT="$MODDIR/core/safmus_audio_effect_audit.sh"
if [ -f "$SAFMUS_AUDIO_AUDIT" ]; then
    chmod 755 "$SAFMUS_AUDIO_AUDIT" 2>/dev/null
    "$SAFMUS_AUDIO_AUDIT" >> "$LOG" 2>&1
fi

SAFMUS_ENV="$MODDIR/core/safmus_environment_optimizer.sh"
if [ -x "$SAFMUS_ENV" ]; then
    "$SAFMUS_ENV" >> "$LOG" 2>&1
elif [ -f "$SAFMUS_ENV" ]; then
    chmod 755 "$SAFMUS_ENV" 2>/dev/null
    "$SAFMUS_ENV" >> "$LOG" 2>&1
fi

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
        # Existing healthy instance satisfies the same runtime-health contract.
        touch /data/adb/ivanna_omega_last_boot_ok 2>/dev/null
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

# Gate the launch through the autonomous decision engine when available.
# The module safe-mode flag above remains the authoritative emergency veto.
if command -v decision_evaluate >/dev/null 2>&1; then
    decision_evaluate "DAEMON_RUNTIME"
    DECISION_RC=$?
    if [ "$DECISION_RC" -ne 0 ]; then
        echo "$(date) DECISION VETO: DAEMON_RUNTIME rc=$DECISION_RC state=$(get_state)" >> "$LOG"
        setprop persist.ivanna.daemon_active 0 2>/dev/null
        exit 0
    fi
else
    echo "$(date) WARN: motor de decisiones ausente; se conserva gate legacy" >> "$LOG"
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

# Canal de estado ASRC / reloj HAL (/data/vendor/ivanna_audio). Sin este
# directorio el daemon no puede publicar asrc_state ni abrir clock_snapshot.v2
# (ENOENT) y el panel ASRC queda vacio. Ver asrc_link.sh para la causa raiz.
ASRC_LINK="$MODDIR/asrc_link.sh"
if [ -f "$ASRC_LINK" ]; then
    . "$ASRC_LINK"
    if ivanna_asrc_link_prepare; then
        echo "$(date) ASRC-LINK: $CLOCK_DIR listo" >> "$LOG"
    else
        echo "$(date) ASRC-LINK: FALLO creando $CLOCK_DIR" >> "$LOG"
    fi
fi

# Verificación ASRC v5 en arranque (SOLO LECTURA: ivanna_asrc_preload_status
# lee /proc/<pid>/maps y /proc/<pid>/environ del HAL y el directorio del
# canal; no cambia el entorno de ningún proceso, no reinicia el HAL ni
# escribe archivos — lo prohíben tests/test_preload_link.py). El HAL
# arranca antes que este script, así que si el overlay.d inyectó la .so
# ya debe verse mapeada. Un veredicto distinto de LOADED solo se registra
# como WARNING: el audio y el arranque del daemon no dependen de este
# chequeo, y el kill switch (pal_asrc.off) es bypass intencional, no fallo.
if command -v ivanna_asrc_preload_status >/dev/null 2>&1; then
    ASRC_CHECK=$(ivanna_asrc_preload_status 2>&1)
    echo "$ASRC_CHECK" >> "$LOG"
    case "$ASRC_CHECK" in
        *asrc_verdict=LOADED*)
            echo "$(date) ASRC-CHECK: write-stage v5 mapeado en el HAL" >> "$LOG" ;;
        *)
            echo "$(date) ASRC-CHECK: WARNING write-stage v5 no confirmado en el HAL (ver líneas ASRC arriba; el audio sigue sin depender de esto)" >> "$LOG" ;;
    esac
fi

# Control autenticado solo por socket UNIX abstracto. El daemon rechaza TCP
# deliberadamente; no pasar un flag de fallback que la app no debe usar.
#
# El daemon es el plano de control (sockets/SHM/watchdog/sondeo de reloj),
# no el callback que procesa PCM. Se conserva --realtime por compatibilidad
# con scripts/versiones anteriores, pero ivanna_daemon fuerza su hilo principal
# y sus trabajadores a SCHED_OTHER para no competir con AudioFlinger/táctil.
# La prioridad RT extrema debe reservarse a un hilo de audio dedicado y medido.
"$DAEMON" --realtime >> "$LOG" 2>&1 &

PID=$!
echo "$PID" > "$STATE/daemon.pid"

# El daemon crea los archivos con 0660 root:root; se abre lectura a la app
# cuando aparecen. Segundo plano, termina solo.
if command -v ivanna_asrc_link_watch >/dev/null 2>&1; then
    ( n=$(ivanna_asrc_link_watch); echo "$(date) ASRC-LINK: $n archivo(s) del canal con permisos de lectura" >> "$LOG" ) >/dev/null 2>&1 &
fi

# Confirm both process survival and the actual abstract socket, not PID alone.
SOCKET_READY=0
i=0
while [ "$i" -lt 20 ]; do
    if ! kill -0 "$PID" 2>/dev/null; then
        break
    fi
    if grep -aq omega_daemon_socket /proc/net/unix 2>/dev/null; then
        SOCKET_READY=1
        break
    fi
    i=$((i + 1))
    sleep 0.5 2>/dev/null || sleep 1
done

if kill -0 "$PID" 2>/dev/null && [ "$SOCKET_READY" -eq 1 ]; then
    echo "$(date) daemon listo PID=$PID socket=@omega_daemon_socket" >> "$LOG"
    setprop persist.ivanna.daemon_active 1 2>/dev/null
    command -v set_state >/dev/null 2>&1 && set_state "OPTIMAL"
    echo "0" > "$CRASH_COUNT_FILE"
    # Health marker consumed by post-fs-data on the next boot. It certifies
    # only that the IVANNA daemon survived and its control socket was observed;
    # it is not a claim that Android completed every boot phase.
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
    if kill -0 "$PID" 2>/dev/null; then
        echo "$(date) ERROR daemon vivo pero socket no listo tras 10 s" >> "$LOG"
        command -v set_state >/dev/null 2>&1 && set_state "DEGRADED"
    else
        echo "$(date) ERROR daemon murió antes de quedar listo" >> "$LOG"
        command -v set_state >/dev/null 2>&1 && set_state "DEGRADED"
    fi
    setprop persist.ivanna.daemon_active 0 2>/dev/null
    echo $((CRASHES + 1)) > "$CRASH_COUNT_FILE"
fi
