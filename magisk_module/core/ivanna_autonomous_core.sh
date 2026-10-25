#!/system/bin/sh
# ==============================================================================
# IVANNA AUTONOMOUS RUNTIME PLATFORM
# Module Intelligence & Component Isolation Framework
# ==============================================================================
# 1. Motor de Estado Autónomo
# 2. Mapeo Completo del Dispositivo
# 3. Sistema Predictivo de Fallos
# 4. Sistema de Memoria (Memory Core)
# 5. Reparación Autónoma (Self-Healing)
# 6. Motor de Decisiones
# 7. Capa de Aislamiento
# 8. Sistema de Actualización Evolutiva
# 9. Seguridad Adaptativa
# 10. Cero Impacto Invisible
# 11. Observatorio Interno
# 12. Modo Forensic
# ==============================================================================

# Rutas Maestras
IVANNA_DATA_ROOT="/data/adb/ivanna_omega"
CORE_DATA="$IVANNA_DATA_ROOT/autonomous_core"
OBSERVATORY_LOG="$CORE_DATA/observatory.log"
MEMORY_CORE_DB="$CORE_DATA/memory_core.json"
PROFILE_DB="$CORE_DATA/device_profile.json"
FORENSICS_DIR="$IVANNA_DATA_ROOT/forensics"
ISOLATION_DIR="$CORE_DATA/isolation"

# ==============================================================================
# 11. OBSERVATORIO INTERNO (Single Source of Truth)
# ==============================================================================
observatory_log() {
    local TAG="$1"
    local MSG="$2"
    local STATE
    STATE=$(cat "$CORE_DATA/STATE" 2>/dev/null || echo "UNKNOWN")
    mkdir -p "$CORE_DATA" 2>/dev/null || return 1
    printf '[%s] [%s] [%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$TAG" "$STATE" "$MSG" >> "$OBSERVATORY_LOG"
}

# ==============================================================================
# INIT
# ==============================================================================
ivanna_platform_init() {
    GLOBAL_MODDIR="$1"
    if ! mkdir -p "$CORE_DATA" "$FORENSICS_DIR" "$ISOLATION_DIR" 2>/dev/null; then
        return 1
    fi
    chmod 700 "$CORE_DATA" "$FORENSICS_DIR" "$ISOLATION_DIR" 2>/dev/null
    if [ ! -f "$CORE_DATA/STATE" ]; then
        printf '%s\n' "UNKNOWN" > "$CORE_DATA/STATE" || return 1
        chmod 600 "$CORE_DATA/STATE" 2>/dev/null
    fi
    observatory_log "CORE" "IVANNA Autonomous Runtime Platform Inicializada."
}

# ==============================================================================
# 1. MOTOR DE ESTADO AUTÓNOMO
# ==============================================================================
# Estados: UNKNOWN, BOOT_ANALYSIS, ENVIRONMENT_MAPPING, OPTIMAL, PROTECTED, 
# DEGRADED, SELF_REPAIR, SAFE_MODE
set_state() {
    local NEW_STATE="$1"
    case "$NEW_STATE" in
        UNKNOWN|BOOT_ANALYSIS|ENVIRONMENT_MAPPING|OPTIMAL|PROTECTED|DEGRADED|SELF_REPAIR|SAFE_MODE) ;;
        *) observatory_log "STATE_ENGINE" "RECHAZADO: estado desconocido '$NEW_STATE'"; return 2 ;;
    esac
    [ -d "$CORE_DATA" ] || mkdir -p "$CORE_DATA" 2>/dev/null || return 1
    local TMP="$CORE_DATA/STATE.tmp.$"
    printf '%s\n' "$NEW_STATE" > "$TMP" || { rm -f "$TMP"; return 1; }
    chmod 600 "$TMP" 2>/dev/null
    mv -f "$TMP" "$CORE_DATA/STATE" || { rm -f "$TMP"; return 1; }
    observatory_log "STATE_ENGINE" "Transición de estado ejecutada -> $NEW_STATE"
}

get_state() {
    cat "$CORE_DATA/STATE" 2>/dev/null || echo "UNKNOWN"
}

# ==============================================================================
# 4. SISTEMA DE MEMORIA DEL MÓDULO (IVANNA Memory Core)
# ==============================================================================
json_escape() {
    # Escape JSON metacharacters and embedded newlines using POSIX sed.
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; :a; N; $!ba; s/\n/\\n/g'
}
memory_core_record() {
    local EVENT_TYPE="$1"
    local DATA="$2"
    local TS TYPE_ESC DATA_ESC LINE TMP
    TS=$(date +%s)
    TYPE_ESC=$(json_escape "$EVENT_TYPE") || return 1
    DATA_ESC=$(json_escape "$DATA") || return 1
    LINE="{\"timestamp\":$TS,\"type\":\"$TYPE_ESC\",\"data\":\"$DATA_ESC\"}"
    mkdir -p "$CORE_DATA" 2>/dev/null || return 1
    # One append operation per record; the database is JSON Lines (JSONL).
    printf '%s\n' "$LINE" >> "$MEMORY_CORE_DB" || return 1
    chmod 600 "$MEMORY_CORE_DB" 2>/dev/null
    observatory_log "MEMORY_CORE" "Evento registrado: $EVENT_TYPE"
}

# ==============================================================================
# 2. MAPEO COMPLETO DEL DISPOSITIVO
# ==============================================================================
build_device_runtime_profile() {
    observatory_log "PROFILER" "Generando Device Runtime Profile..."
    cat <<PROFOUT > "$PROFILE_DB"
{
    "android_version": "$(getprop ro.build.version.release)",
    "kernel": "$(uname -r)",
    "architecture": "$(uname -m)",
    "manufacturer": "$(getprop ro.product.manufacturer)",
    "soc": "$(getprop ro.board.platform)",
    "mem_total": "$(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2}')",
    "selinux": "$(getenforce 2>/dev/null || echo 'UNKNOWN')",
    "magisk": "$(magisk -V 2>/dev/null || echo 'UNKNOWN')"
}
PROFOUT
    memory_core_record "PROFILE_MAPPED" "Runtime Profile Snapshot Creado"
}

# ==============================================================================
# 9. SEGURIDAD ADAPTATIVA
# ==============================================================================
security_intelligence_scan() {
    observatory_log "SECURITY" "Iniciando escaneo de seguridad adaptativa..."
    local SELINUX_STATE=$(getenforce 2>/dev/null || echo "UNKNOWN")
    if [ "$SELINUX_STATE" = "Enforcing" ]; then
        observatory_log "SECURITY" "SELinux Enforcing detectado. Verificando reglas dinámicas."
    elif [ "$SELINUX_STATE" = "Permissive" ]; then
        observatory_log "SECURITY" "Entorno Permissive. Elevando advertencia de seguridad."
        set_state "PROTECTED"
    fi
}

# ==============================================================================
# 6. MOTOR DE DECISIONES & 10. CERO IMPACTO INVISIBLE
# ==============================================================================
decision_evaluate() {
    local TARGET_ACTION="$1"
    local CURRENT_STATE
    CURRENT_STATE=$(get_state)

    case "$TARGET_ACTION" in
        DAEMON_RUNTIME) ;;
        MQA_INTELLIGENCE) ;;
        *) observatory_log "DECISION" "VETO: acción desconocida '$TARGET_ACTION'"; return 2 ;;
    esac

    if [ "$CURRENT_STATE" = "SAFE_MODE" ] || [ "$CURRENT_STATE" = "SELF_REPAIR" ]; then
        observatory_log "DECISION" "VETO: $TARGET_ACTION bloqueada en estado $CURRENT_STATE"
        return 1
    fi
    if [ "$CURRENT_STATE" = "PROTECTED" ]; then
        observatory_log "DECISION" "VETO: $TARGET_ACTION bloqueada por política PROTECTED"
        return 1
    fi

    local COMP_ID
    case "$TARGET_ACTION" in
        DAEMON_RUNTIME) COMP_ID="DAEMON" ;;
        MQA_INTELLIGENCE) COMP_ID="MQA" ;;
    esac
    if is_component_active "$COMP_ID"; then
        observatory_log "DECISION" "NO-OP: $COMP_ID ya está activo"
        return 3
    fi
    observatory_log "DECISION" "AUTORIZADO: $TARGET_ACTION -> $COMP_ID"
    return 0
}

# Decision-to-execution bridge. Call as:
# decision_dispatch ACTION COMPONENT_ID /absolute/or/relative/executable [args...]
# Return: 0 started, 1 policy veto, 2 invalid action/component, 3 already active.
decision_dispatch() {
    local ACTION="$1"
    local REQUESTED_COMP="$2"
    shift 2 2>/dev/null || {
        observatory_log "DECISION" "VETO: faltan argumentos de dispatch"
        return 2
    }
    [ "$#" -gt 0 ] || {
        observatory_log "DECISION" "VETO: comando vacío para $ACTION"
        return 2
    }

    local EXPECTED_COMP
    case "$ACTION" in
        DAEMON_RUNTIME) EXPECTED_COMP="DAEMON" ;;
        MQA_INTELLIGENCE) EXPECTED_COMP="MQA" ;;
        *) observatory_log "DECISION" "VETO: acción no permitida '$ACTION'"; return 2 ;;
    esac
    [ "$REQUESTED_COMP" = "$EXPECTED_COMP" ] || {
        observatory_log "DECISION" "VETO: $ACTION no puede lanzar $REQUESTED_COMP"
        return 2
    }
    case "$1" in
        /*) [ -x "$1" ] || { observatory_log "DECISION" "VETO: ejecutable no disponible: $1"; return 2; } ;;
        *) observatory_log "DECISION" "VETO: se requiere ruta absoluta del ejecutable"; return 2 ;;
    esac

    decision_evaluate "$ACTION"
    local RC=$?
    [ "$RC" -eq 0 ] || return "$RC"
    component_isolate_execute "$REQUESTED_COMP" "$@"
    RC=$?
    if [ "$RC" -eq 0 ]; then
        memory_core_record "DECISION_DISPATCH" "$ACTION lanzó $REQUESTED_COMP"
    else
        observatory_log "DECISION" "ERROR: dispatch $ACTION/$REQUESTED_COMP rc=$RC"
    fi
    return "$RC"
}

# ==============================================================================
# 7. CAPA DE AISLAMIENTO (Component Isolation System)
# ==============================================================================
component_id_valid() {
    case "$1" in
        DAEMON|MQA) return 0 ;;
        *) return 1 ;;
    esac
}

proc_start_token() {
    # /proc/PID/stat: strip through the final ')' in comm; starttime is field 20
    # of the remaining fields (original field 22).
    [ -r "/proc/$1/stat" ] || return 1
    sed 's/^.*) //' "/proc/$1/stat" 2>/dev/null | awk '{print $20}'
}

pid_matches_component() {
    local COMP_DIR="$1" PID="$2" SAVED CURRENT STAT_REST
    case "$PID" in ''|*[!0-9]*) return 1 ;; esac
    [ "$PID" -gt 1 ] 2>/dev/null || return 1
    kill -0 "$PID" 2>/dev/null || return 1
    SAVED=$(cat "$COMP_DIR/starttime" 2>/dev/null)
    CURRENT=$(proc_start_token "$PID" 2>/dev/null)
    [ -n "$SAVED" ] && [ -n "$CURRENT" ] && [ "$SAVED" = "$CURRENT" ] || return 1
    STAT_REST=$(sed 's/^.*) //' "/proc/$PID/stat" 2>/dev/null | awk '{print $1}')
    [ "$STAT_REST" != "Z" ] || return 1
    return 0
}

component_isolate_execute() {
    local COMP_ID="$1"
    shift
    component_id_valid "$COMP_ID" || {
        observatory_log "ISOLATION" "RECHAZADO: ID de componente inválido"
        return 2
    }
    [ "$#" -gt 0 ] || return 2
    local COMP_DIR="$ISOLATION_DIR/$COMP_ID"
    mkdir -p "$COMP_DIR" 2>/dev/null || return 1

    if is_component_active "$COMP_ID"; then
        observatory_log "ISOLATION" "$COMP_ID ya está activo; se evita duplicado"
        return 3
    fi

    observatory_log "ISOLATION" "Lanzando $COMP_ID; no implica sandbox del kernel"
    "$@" >> "$COMP_DIR/stdout.log" 2>&1 &
    local PID=$!
    local START=""
    local i=0
    while [ "$i" -lt 10 ]; do
        START=$(proc_start_token "$PID" 2>/dev/null)
        [ -n "$START" ] && break
        kill -0 "$PID" 2>/dev/null || break
        i=$((i+1))
        sleep 0.1 2>/dev/null || sleep 1
    done
    if [ -z "$START" ] || ! kill -0 "$PID" 2>/dev/null; then
        wait "$PID" 2>/dev/null
        observatory_log "ISOLATION" "FALLO: $COMP_ID no sobrevivió al arranque; revisar $COMP_DIR/stdout.log"
        memory_core_record "COMPONENT_START_FAIL" "$COMP_ID no sobrevivió al arranque"
        return 1
    fi

    local PID_TMP="$COMP_DIR/pid.tmp.$$"
    local START_TMP="$COMP_DIR/starttime.tmp.$$"
    printf '%s\n' "$PID" > "$PID_TMP" || { rm -f "$PID_TMP" "$START_TMP"; kill "$PID" 2>/dev/null; return 1; }
    printf '%s\n' "$START" > "$START_TMP" || { rm -f "$PID_TMP" "$START_TMP"; kill "$PID" 2>/dev/null; return 1; }
    chmod 600 "$PID_TMP" "$START_TMP" 2>/dev/null
    mv -f "$START_TMP" "$COMP_DIR/starttime" || { rm -f "$PID_TMP"; kill "$PID" 2>/dev/null; return 1; }
    mv -f "$PID_TMP" "$COMP_DIR/pid" || { kill "$PID" 2>/dev/null; return 1; }

    if [ "$COMP_ID" = "DAEMON" ]; then
        local socket_ok=0
        i=0
        while [ "$i" -lt 20 ]; do
            if ! pid_matches_component "$COMP_DIR" "$PID"; then
                observatory_log "ISOLATION" "FATAL: DAEMON terminó antes de la comprobación del socket"
                memory_core_record "DAEMON_BIND_FAIL" "proceso muerto pre-bind; revisar stdout.log"
                rm -f "$COMP_DIR/pid" "$COMP_DIR/starttime"
                return 1
            fi
            if grep -q '@omega_daemon_socket' /proc/net/unix 2>/dev/null; then
                socket_ok=1
                break
            fi
            i=$((i+1))
            sleep 0.5 2>/dev/null || sleep 1
        done
        if [ "$socket_ok" -ne 1 ]; then
            observatory_log "ISOLATION" "ADVERTENCIA: DAEMON vivo, pero @omega_daemon_socket no fue observado en 10 s"
            memory_core_record "DAEMON_SOCKET_TIMEOUT" "PID=$PID socket no observado"
            return 1
        fi
        observatory_log "ISOLATION" "DAEMON vivo y socket abstracto observado"
    fi
    memory_core_record "COMPONENT_START" "$COMP_ID PID=$PID"
    return 0
}

is_component_active() {
    local COMP_ID="$1"
    component_id_valid "$COMP_ID" || return 1
    local COMP_DIR="$ISOLATION_DIR/$COMP_ID"
    [ -f "$COMP_DIR/pid" ] || return 1
    local PID
    PID=$(cat "$COMP_DIR/pid" 2>/dev/null)
    if pid_matches_component "$COMP_DIR" "$PID"; then
        return 0
    fi
    # Stale or recycled PID: clear only this component's metadata.
    rm -f "$COMP_DIR/pid" "$COMP_DIR/starttime"
    return 1
}

component_crashed() {
    local COMP_ID="$1"
    component_id_valid "$COMP_ID" || return 1
    local COMP_DIR="$ISOLATION_DIR/$COMP_ID"
    [ -f "$COMP_DIR/pid" ] || return 1
    local PID
    PID=$(cat "$COMP_DIR/pid" 2>/dev/null)
    if pid_matches_component "$COMP_DIR" "$PID"; then
        return 1
    fi
    rm -f "$COMP_DIR/pid" "$COMP_DIR/starttime"
    observatory_log "ISOLATION" "$COMP_ID PID ausente, zombie o reutilizado; metadata obsoleta limpiada"
    return 0
}

# ==============================================================================
# 3. SISTEMA PREDICTIVO DE FALLOS
# ==============================================================================
predictive_failure_engine() {
    local COMP_ID="DAEMON"
    local COMP_DIR="$ISOLATION_DIR/$COMP_ID"
    
    if [ -f "$COMP_DIR/pid" ]; then
        local PID=$(cat "$COMP_DIR/pid")
        if kill -0 "$PID" 2>/dev/null; then
            local MEM_RSS=$(ps -p $PID -o rss= 2>/dev/null | tr -d ' ')
            if [ -n "$MEM_RSS" ] && [ "$MEM_RSS" -gt 81920 ]; then
                observatory_log "PREDICTIVE" "⚠️ Anomalía térmica/memoria detectada en $COMP_ID: $MEM_RSS KB."
                memory_core_record "PREDICTIVE_WARNING" "Memory leak probable en $COMP_ID"
                return 1
            fi
            
            local LOG_SIZE=$(stat -c%s "$COMP_DIR/stdout.log" 2>/dev/null || echo 0)
            if [ "$LOG_SIZE" -gt 5242880 ]; then
                observatory_log "PREDICTIVE" "⚠️ Crecimiento explosivo de logs (ciclo infinito) detectado en $COMP_ID."
                return 1
            fi
        fi
    fi
    return 0
}

# ==============================================================================
# 12. MODO FORENSIC
# ==============================================================================
forensic_dump() {
    local CAUSE="$1"
    local TS=$(date +%s)
    local DUMP_FILE="$FORENSICS_DIR/ivanna_crash_$TS.dump"

    observatory_log "FORENSICS" "Generando volcado de escena: $CAUSE"
    
    cat <<FORENSIOUT > "$DUMP_FILE"
=== IVANNA AUTONOMOUS FORENSIC DUMP ===
TIMESTAMP: $(date)
CAUSE: $CAUSE
LAST_STATE: $(get_state)
RUNTIME_PROFILE:
$(cat "$PROFILE_DB" 2>/dev/null)
ISOLATION_STATES:
FORENSIOUT

    for d in "$ISOLATION_DIR"/*; do
        if [ -d "$d" ]; then
            echo ">> COMPONENT: $(basename "$d")" >> "$DUMP_FILE"
            echo "PID: $(cat "$d/pid" 2>/dev/null)" >> "$DUMP_FILE"
            echo "TAIL LOG:" >> "$DUMP_FILE"
            tail -n 15 "$d/stdout.log" 2>/dev/null >> "$DUMP_FILE"
        fi
    done
    
    echo "OBSERVATORY TIMELINE:" >> "$DUMP_FILE"
    tail -n 30 "$OBSERVATORY_LOG" 2>/dev/null >> "$DUMP_FILE"

    memory_core_record "FORENSIC_DUMP" "Dump creado en $DUMP_FILE"
}

# ==============================================================================
# 5. REPARACIÓN AUTÓNOMA (Self Healing Framework)
# ==============================================================================
self_healing_framework() {
    # Safe recovery is diagnostic-only: this framework does not own all child
    # process groups and must never kill arbitrary PIDs or claim a restart.
    observatory_log "HEALING" "Iniciando recuperación segura y no destructiva..."
    set_state "SELF_REPAIR" || return 1

    local d COMP_ID PID
    for d in "$ISOLATION_DIR"/*; do
        [ -d "$d" ] || continue
        COMP_ID=${d##*/}
        component_id_valid "$COMP_ID" || {
            observatory_log "HEALING" "Directorio desconocido preservado: $COMP_ID"
            continue
        }
        if component_crashed "$COMP_ID"; then
            observatory_log "HEALING" "$COMP_ID confirmado como inactivo; metadatos obsoletos limpiados"
            [ -f "$d/stdout.log" ] && tail -n 200 "$d/stdout.log" > "$d/stdout.last_failure.log" 2>/dev/null
        elif is_component_active "$COMP_ID"; then
            PID=$(cat "$d/pid" 2>/dev/null)
            observatory_log "HEALING" "$COMP_ID sigue vivo (PID $PID); no se mata ni reinicia automáticamente"
        fi
    done

    if ! build_device_runtime_profile; then
        observatory_log "HEALING" "Perfil no pudo regenerarse; estado DEGRADED"
        set_state "DEGRADED"
        return 1
    fi
    observatory_log "HEALING" "Diagnóstico completado; no se afirmó reparación de procesos"
    set_state "DEGRADED"
    return 0
}
# ==============================================================================
# 8. SISTEMA DE ACTUALIZACIÓN EVOLUTIVA
# ==============================================================================
evolution_layer_check() {
    observatory_log "EVOLUTION" "Verificando consistencia de versión estructural..."
    local CURRENT_VER=$(getprop persist.ivanna.version 2>/dev/null)
    local STORED_VER=$(cat "$CORE_DATA/VERSION" 2>/dev/null || echo "NONE")
    
    if [ -n "$CURRENT_VER" ] && [ "$CURRENT_VER" != "$STORED_VER" ]; then
        observatory_log "EVOLUTION" "Migración genética detectada: $STORED_VER -> $CURRENT_VER"
        memory_core_record "EVOLUTION" "Actualización estructural procesada"
        echo "$CURRENT_VER" > "$CORE_DATA/VERSION"
    fi
}
