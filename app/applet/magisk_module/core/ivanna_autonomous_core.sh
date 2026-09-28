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
    local STATE=$(cat "$CORE_DATA/STATE" 2>/dev/null || echo "UNKNOWN")
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [$TAG] [$STATE] $MSG" >> "$OBSERVATORY_LOG"
}

# ==============================================================================
# INIT
# ==============================================================================
ivanna_platform_init() {
    GLOBAL_MODDIR="$1"
    mkdir -p "$CORE_DATA" "$FORENSICS_DIR" "$ISOLATION_DIR" 2>/dev/null
    if [ ! -f "$CORE_DATA/STATE" ]; then
        echo "UNKNOWN" > "$CORE_DATA/STATE"
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
    echo "$NEW_STATE" > "$CORE_DATA/STATE"
    observatory_log "STATE_ENGINE" "Transición de estado ejecutada -> $NEW_STATE"
}

get_state() {
    cat "$CORE_DATA/STATE" 2>/dev/null || echo "UNKNOWN"
}

# ==============================================================================
# 4. SISTEMA DE MEMORIA DEL MÓDULO (IVANNA Memory Core)
# ==============================================================================
memory_core_record() {
    local EVENT_TYPE="$1"
    local DATA="$2"
    observatory_log "MEMORY_CORE" "Registrando evento: $EVENT_TYPE"
    echo "{\"timestamp\":$(date +%s),\"type\":\"$EVENT_TYPE\",\"data\":\"$DATA\"}" >> "$MEMORY_CORE_DB"
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
    local CURRENT_STATE=$(get_state)

    if [ "$CURRENT_STATE" = "SAFE_MODE" ]; then
        observatory_log "DECISION" "VETO: Acción $TARGET_ACTION denegada en SAFE_MODE."
        return 1
    fi

    # Cero Impacto Invisible
    if [ "$TARGET_ACTION" = "DAEMON_RUNTIME" ]; then
        if is_component_active "DAEMON"; then
            return 1
        fi
        observatory_log "DECISION" "AUTORIZADO: $TARGET_ACTION es necesario y seguro."
        return 0
    fi

    if [ "$TARGET_ACTION" = "MQA_INTELLIGENCE" ]; then
        if is_component_active "MQA"; then
            return 1
        fi
        observatory_log "DECISION" "AUTORIZADO: $TARGET_ACTION es seguro."
        return 0
    fi

    return 1
}

# ==============================================================================
# 7. CAPA DE AISLAMIENTO (Component Isolation System)
# ==============================================================================
component_isolate_execute() {
    local COMP_ID="$1"
    shift
    local CMD=("$@")

    local COMP_DIR="$ISOLATION_DIR/$COMP_ID"
    mkdir -p "$COMP_DIR"

    observatory_log "ISOLATION" "Desplegando $COMP_ID en burbuja de aislamiento..."
    
    "${CMD[@]}" >> "$COMP_DIR/stdout.log" 2>&1 &
    local PID=$!
    echo "$PID" > "$COMP_DIR/pid"

    # FIX (socket ausente en /proc/net/unix — el daemon crasheaba antes del
    # bind y el panel mostraba CORRIENDO pero DESCONECTADO): el executor solo
    # guardaba el PID sin confirmar que el proceso sobrevivió al arranque.
    # Ahora, para el DAEMON, espera a que el socket abstracto quede bindeado
    # (visible en /proc/net/unix) y loguea el resultado real — si el binario
    # murió (ELF corrupto, ENOEXEC, bind fallido), queda registrado aquí en
    # vez de fallar en silencio y dejar el panel sin pista de la causa.
    if [ "$COMP_ID" = "DAEMON" ]; then
        local i=0
        while [ $i -lt 20 ]; do
            if grep -q "@omega_daemon_socket" /proc/net/unix 2>/dev/null; then
                observatory_log "ISOLATION" "DAEMON bindeó @omega_daemon_socket OK (intento $i)"
                break
            fi
            if ! kill -0 "$PID" 2>/dev/null; then
                observatory_log "ISOLATION" "FATAL: DAEMON murió antes del bind — ver $COMP_DIR/stdout.log"
                memory_core_record "DAEMON_BIND_FAIL" "proceso muerto pre-bind, stdout.log tiene la causa"
                break
            fi
            i=$((i+1)); sleep 0.5 2>/dev/null || sleep 1
        done
    fi

    memory_core_record "COMPONENT_START" "$COMP_ID PID=$PID"
}

is_component_active() {
    local COMP_ID="$1"
    local COMP_DIR="$ISOLATION_DIR/$COMP_ID"
    if [ -f "$COMP_DIR/pid" ]; then
        local PID=$(cat "$COMP_DIR/pid")
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

component_crashed() {
    local COMP_ID="$1"
    local COMP_DIR="$ISOLATION_DIR/$COMP_ID"
    if [ -f "$COMP_DIR/pid" ]; then
        local PID=$(cat "$COMP_DIR/pid")
        if [ -n "$PID" ] && ! kill -0 "$PID" 2>/dev/null; then
            rm -f "$COMP_DIR/pid"
            return 0
        fi
    fi
    return 1
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
    observatory_log "HEALING" "Iniciando Protocolo de Autorreparación..."
    set_state "SELF_REPAIR"

    for d in "$ISOLATION_DIR"/*; do
        if [ -d "$d" ]; then
            local PID=$(cat "$d/pid" 2>/dev/null)
            if [ -n "$PID" ]; then
                kill -9 "$PID" 2>/dev/null
                observatory_log "HEALING" "Componente aislado aniquilado (PID $PID)."
            fi
            rm -f "$d/pid"
            mv "$d/stdout.log" "$d/stdout_repaired.log" 2>/dev/null
        fi
    done

    build_device_runtime_profile
    
    observatory_log "HEALING" "Homeostasis restaurada. Retornando a flujo operativo."
    set_state "OPTIMAL"
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
