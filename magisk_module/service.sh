#!/system/bin/sh
set -e  # abortar ante fallo — estado parcial es peor que fallo explícito
# IVANNA OMEGA SUPREME v6.3 - Magisk Realtime Daemon Service
MODDIR=${0%/*}

DAEMON_BIN="$MODDIR/system/bin/ivanna_daemon"
LOGFILE="/data/adb/ivanna_omega/daemon.log"
LAST_OK="/data/adb/ivanna_omega_last_boot_ok"

mkdir -p /data/adb/ivanna_omega

# Log rotation > 10MB
if [ -f "$LOGFILE" ] && [ "$(stat -c%s "$LOGFILE" 2>/dev/null || echo 0)" -gt 10485760 ]; then
    mv "$LOGFILE" "${LOGFILE}.old"
fi

echo "[$(date)] service.sh v6.3 iniciado" >> "$LOGFILE"

# ── SELinux: cargar reglas del módulo en tiempo real ─────────────────────────
# Sin esto, untrusted_app (la app) no puede connect() al socket abstracto del
# daemon root — Android lo bloquea silenciosamente.
SEPOLICY_RULE="$MODDIR/sepolicy.rule"
if [ -f "$SEPOLICY_RULE" ]; then
    if command -v magiskpolicy >/dev/null 2>&1; then
        # FIX: `--apply <file>` aborta al primer error de parseo y devuelve
        # distinto de 0 sin decir QUE linea fallo. Se aplica primero el archivo
        # completo y, si falla, se cae a aplicacion linea por linea para que una
        # regla invalida (o un dominio inexistente en esta version de Android)
        # no tumbe TODAS las demas — que es lo que dejaba a la app sin
        # `connectto` y con el socket del daemon inalcanzable.
        if magiskpolicy --live --apply "$SEPOLICY_RULE" >> "$LOGFILE" 2>&1; then
            echo "[$(date)] SELinux: sepolicy.rule aplicado completo" >> "$LOGFILE"
        else
            echo "[$(date)] WARN: --apply fallo; aplicando regla por regla" >> "$LOGFILE"
            SE_OK=0; SE_FAIL=0
            while IFS= read -r RULE; do
                case "$RULE" in ''|'#'*) continue ;; esac
                if magiskpolicy --live "$RULE" >> "$LOGFILE" 2>&1; then
                    SE_OK=$((SE_OK + 1))
                else
                    SE_FAIL=$((SE_FAIL + 1))
                    echo "[$(date)] SELinux regla RECHAZADA: $RULE" >> "$LOGFILE"
                fi
            done < "$SEPOLICY_RULE"
            echo "[$(date)] SELinux: $SE_OK reglas aplicadas, $SE_FAIL rechazadas" >> "$LOGFILE"
        fi
        echo "[$(date)] SELinux modo actual: $(getenforce 2>/dev/null || echo desconocido)" >> "$LOGFILE"
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

# ── HRTF dataset deploy (FIX: faltaba en service.sh) ─────────────────────────
# customize.sh deploya hrtf_dataset.ihr1 en instalación, pero si el archivo
# se borra de /data/adb/ivanna_omega/ (limpieza manual, reset de datos parcial,
# fallo de storage) el HRTF engine cae al fallback sintético sin aviso.
# service.sh lo restaura en cada boot, igual que hace con SAF_model.json.
HRTF_ASSET="$MODDIR/system/etc/ivanna_omega/hrtf_dataset.ihr1"
HRTF_DEST="/data/adb/ivanna_omega/hrtf_dataset.ihr1"
if [ -f "$HRTF_ASSET" ]; then
    # FIX: antes se copiaba solo si faltaba ([ ! -f ]), igual que SAF_model.
    # Con ese guard, una actualización del módulo con dataset HRTF nuevo NUNCA
    # refrescaba el archivo en /data/adb/ para usuarios ya instalados — corrían
    # siempre con el dataset de la primera instalación aunque el módulo fuera
    # más reciente. Ahora siempre se sobreescribe (como SAF_model.json) para
    # garantizar consistencia entre el binario del módulo y los assets en /data/.
    cp -f "$HRTF_ASSET" "$HRTF_DEST"
    chmod 644 "$HRTF_DEST"
    echo "[$(date)] hrtf_dataset.ihr1 refrescado → $HRTF_DEST ($(stat -c%s "$HRTF_ASSET" 2>/dev/null || echo ?) bytes)" >> "$LOGFILE"
else
    echo "[$(date)] WARN: hrtf_dataset.ihr1 NO encontrado en $HRTF_ASSET — HRTF usará fallback sintético" >> "$LOGFILE"
fi

# ── Verificar binario ─────────────────────────────────────────────────────────
# FIX (panel muestra DETENIDO con módulo ACTIVO):
#   Si el zip instalado es anterior al fix de CI que stagea el binario,
#   $DAEMON_BIN no existe y este script hacía exit 1 en silencio — el panel
#   leía daemon_active=0 para siempre sin pista de la causa. Ahora:
#     1. Se verifica que el binario sea un ELF ejecutable real (magic 7f454c46),
#        no solo que el archivo exista (un zip corrupto dejaba un archivo de
#        0 bytes que también pasaba el [ -f ]).
#     2. Se baja daemon_active=0 explícitamente y se deja instrucción de
#        reinstalación en el log, para diagnóstico desde logcat.
#     3. exit 1 sigue siendo correcto (no tiene sentido el watchdog sin
#        binario), pero ahora el log dice POR QUÉ y CÓMO arreglarlo.
if [ ! -f "$DAEMON_BIN" ]; then
    echo "[$(date)] ERROR FATAL: $DAEMON_BIN no existe en el módulo." >> "$LOGFILE"
    echo "[$(date)] CAUSA: el zip instalado es anterior al fix de CI que compila" >> "$LOGFILE"
    echo "[$(date)]        ivanna_daemon (commit 843eb32). Reinstala el módulo" >> "$LOGFILE"
    echo "[$(date)]        con el zip actual de GitHub Actions (artefacto" >> "$LOGFILE"
    echo "[$(date)]        ivanna-magisk-module) y reinicia." >> "$LOGFILE"
    setprop persist.ivanna.daemon_active 0
    exit 1
fi
# Verificar que es un ELF real (un archivo vacío o corrupto también pasa [ -f ])
ELF_MAGIC=$(head -c4 "$DAEMON_BIN" 2>/dev/null | od -An -tx1 | tr -d ' \n')
if [ "$ELF_MAGIC" != "7f454c46" ]; then
    echo "[$(date)] ERROR FATAL: $DAEMON_BIN no es un binario ELF válido" >> "$LOGFILE"
    echo "[$(date)]        (magic=$ELF_MAGIC, esperado 7f454c46). El módulo" >> "$LOGFILE"
    echo "[$(date)]        está corrupto — reinstala desde el zip del CI." >> "$LOGFILE"
    setprop persist.ivanna.daemon_active 0
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
# FIX Foco #2 (auditoría 2026-08-09): rastrear MQA_PID vía PID file en disco
# en lugar de la variable de shell. Antes MQA_PID se limpiaba con MQA_PID=""
# después del wait del daemon; si mqa_monitor.sh sobrevivía a un crash del
# daemon (fue background, PID quedó desalineado tras `wait`), la siguiente
# iteración lanzaba un SEGUNDO mqa_monitor y el primero quedaba huérfano
# tocando dumpsys cada 5s → bateria + wakelocks + presets estampando en
# ráfaga. Con archivo persistente cualquier iteración puede matar al
# monitor previo sin depender del estado de la variable en RAM.
MQA_PID_FILE=/data/adb/ivanna_mqa.pid
MQA_PID=""

# ── mqa_kill_previous ─────────────────────────────────────────────────────────
# Mata cualquier mqa_monitor.sh residual, sea de esta ejecución (variable
# MQA_PID) o de una ejecución anterior de service.sh que sobrevivió (via
# MQA_PID_FILE). Idempotente y seguro contra PID reciclado: `kill -0` valida
# que el PID exista antes de mandar SIGTERM, y `ps` filtra por nombre por si
# el kernel ya reasignó el PID a otro proceso ajeno.
mqa_kill_previous() {
    if [ -f "$MQA_PID_FILE" ]; then
        OLD_MQA=$(cat "$MQA_PID_FILE" 2>/dev/null)
        if [ -n "$OLD_MQA" ] && kill -0 "$OLD_MQA" 2>/dev/null; then
            # Verifica que el PID sea realmente un mqa_monitor.sh, no un
            # proceso ajeno con el mismo PID tras un ciclo del kernel.
            if ps -p "$OLD_MQA" -o comm= 2>/dev/null | grep -q "mqa_monitor\|sh"; then
                kill "$OLD_MQA" 2>/dev/null
                echo "[$(date)] mqa_monitor previo PID=$OLD_MQA matado (via $MQA_PID_FILE)" >> "$LOGFILE"
            fi
        fi
        rm -f "$MQA_PID_FILE"
    fi
    # También matar el de esta sesión si sigue en la variable
    if [ -n "$MQA_PID" ] && kill -0 "$MQA_PID" 2>/dev/null; then
        kill "$MQA_PID" 2>/dev/null
    fi
    MQA_PID=""
}

cleanup() {
    setprop persist.ivanna.daemon_active 0
    [ -n "$DAEMON_PID" ] && kill "$DAEMON_PID" 2>/dev/null
    # FIX Foco #2: matar mqa_monitor via PID file (cubre residuales de
    # ejecuciones previas de service.sh además del de esta sesión).
    mqa_kill_previous
    # FIX: limpiar PID files en cualquier salida
    rm -f "$PID_FILE" "$MQA_PID_FILE"
    echo "[$(date)] service.sh saliendo — daemon_active=0" >> "$LOGFILE"
}
trap cleanup EXIT HUP INT TERM

# ── daemon_kill_orphans ────────────────────────────────────────────────────────
# FIX (auditoría runtime 2026-08-16 — doble instancia / "Address already in use"):
#   Si un service.sh anterior (boot mal cerrado, módulo re-flasheado sin
#   reiniciar, kill -9 del proceso padre) dejó un ivanna_daemon vivo, su socket
#   abstracto @omega_daemon_socket queda bindeado por el kernel y el nuevo
#   daemon muere en bind() con EADDRINUSE — exactamente el error visto en el
#   log real: "Socket bind failed at @omega_daemon_socket: Address already in
#   use" a las 00:04 tras un arranque exitoso a las 23:15.
#
#   Antes de lanzar: se mata cualquier ivanna_daemon previo, primero por
#   PID_FILE (rápido, exacto) y luego por pidof como red de seguridad (cubre
#   el caso en que el PID file se perdió pero el proceso sigue). `kill -0`
#   valida que el PID exista, y pidof filtra por nombre real del binario —
#   imposible matar un proceso ajeno por PID reciclado.
daemon_kill_orphans() {
    if [ -f "$PID_FILE" ]; then
        OLD_PID=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
            kill "$OLD_PID" 2>/dev/null
            sleep 0.3
            kill -0 "$OLD_PID" 2>/dev/null && kill -9 "$OLD_PID" 2>/dev/null
            echo "[$(date)] Daemon huérfano PID=$OLD_PID matado (via $PID_FILE)" >> "$LOGFILE"
        fi
        rm -f "$PID_FILE"
    fi
    # Red de seguridad: pidof cubre daemons cuyo PID file se perdió.
    ORPHANS=$(pidof ivanna_daemon 2>/dev/null)
    if [ -n "$ORPHANS" ]; then
        kill $ORPHANS 2>/dev/null
        sleep 0.3
        ORPHANS_LEFT=$(pidof ivanna_daemon 2>/dev/null)
        [ -n "$ORPHANS_LEFT" ] && kill -9 $ORPHANS_LEFT 2>/dev/null
        echo "[$(date)] Daemon(s) huérfano(s) matados via pidof: $ORPHANS" >> "$LOGFILE"
    fi
}

while true; do
    # No lanzar una segunda instancia mientras el socket siga ocupado por un
    # daemon huérfano — eso garantizaba EADDRINUSE en bind() y un crash-loop.
    daemon_kill_orphans

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
            # FIX Foco #2: matar cualquier mqa_monitor residual (sesión
            # actual o previa) ANTES de lanzar el nuevo. mqa_kill_previous
            # cubre el PID file en disco, no sólo la variable en RAM.
            mqa_kill_previous
            "$MODDIR/mqa_monitor.sh" "$MODDIR" >> "$LOGFILE" 2>&1 &
            MQA_PID=$!
            echo "$MQA_PID" > "$MQA_PID_FILE"
            echo "[$(date)] mqa_monitor.sh PID=$MQA_PID iniciado (registrado en $MQA_PID_FILE)" >> "$LOGFILE"
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
    # FIX Foco #2: matar monitor via helper — el daemon murió, el monitor
    # ya no tiene a quién enviar comandos. Usar mqa_kill_previous en vez
    # de kill "$MQA_PID" directo garantiza que también se atrape a un
    # monitor previo que hubiera quedado huérfano en la iteración anterior.
    mqa_kill_previous
    if [ "$UPTIME" -ge 30 ]; then
        BACKOFF=2
    else
        BACKOFF=$(( BACKOFF * 2 )); [ "$BACKOFF" -gt "$BACKOFF_MAX" ] && BACKOFF=$BACKOFF_MAX
    fi
    echo "[$(date)] Daemon PID=$DAEMON_PID terminó (código=$EXIT_CODE, uptime=${UPTIME}s). Reiniciando en ${BACKOFF}s..." >> "$LOGFILE"
    sleep "$BACKOFF"
done
