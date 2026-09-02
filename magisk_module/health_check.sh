#!/system/bin/sh
# IVANNA OMEGA SUPREME — health_check.sh v1.0
# Diagnóstico completo del módulo desde adb shell o terminal local.
# No requiere root para la mayoría de las comprobaciones.
# Uso: sh /data/adb/modules/ivanna_omega_supreme/health_check.sh [--json]

JSON=0
for arg in "$@"; do
    case "$arg" in --json) JSON=1 ;; esac
done

OK=0; WARN=0; FAIL=0
RESULTS=""

pass() { OK=$((OK+1));   RESULTS="$RESULTS\n  ✅ $1"; }
warn() { WARN=$((WARN+1)); RESULTS="$RESULTS\n  ⚠️  $1"; }
fail() { FAIL=$((FAIL+1)); RESULTS="$RESULTS\n  ❌ $1"; }
section() { RESULTS="$RESULTS\n\n── $1 ──────────────────────────────────────"; }

# ── 1. Props de sistema ───────────────────────────────────────────────────────
section "PROPIEDADES DE SISTEMA"
MAGISK_ACTIVE=$(getprop persist.ivanna.magisk_active 2>/dev/null)
DAEMON_ACTIVE=$(getprop persist.ivanna.daemon_active 2>/dev/null)
MODULE_VER=$(getprop persist.ivanna.version 2>/dev/null)

[ "$MAGISK_ACTIVE" = "1" ] && pass "persist.ivanna.magisk_active=1" \
    || fail "persist.ivanna.magisk_active=$MAGISK_ACTIVE (esperado: 1)"
[ "$DAEMON_ACTIVE" = "1" ] && pass "persist.ivanna.daemon_active=1 (daemon vivo)" \
    || fail "persist.ivanna.daemon_active=$DAEMON_ACTIVE (daemon DETENIDO)"
[ -n "$MODULE_VER" ] && pass "versión módulo: $MODULE_VER" \
    || warn "persist.ivanna.version vacío (module.prop sin leer en post-fs)"

# ── 2. Daemon socket ─────────────────────────────────────────────────────────
section "DAEMON SOCKET"
if [ -r /proc/net/unix ]; then
    if grep -q " @omega_daemon_socket$" /proc/net/unix 2>/dev/null; then
        pass "@omega_daemon_socket visible en /proc/net/unix"
    else
        fail "@omega_daemon_socket NO encontrado en /proc/net/unix"
    fi
else
    warn "/proc/net/unix no legible desde este contexto SELinux"
fi

# ── 3. PID files ──────────────────────────────────────────────────────────────
section "PID FILES"
PID_FILE=/data/adb/ivanna_daemon.pid
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE" 2>/dev/null)
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        pass "ivanna_daemon PID=$PID vivo"
    else
        fail "PID_FILE existe (PID=$PID) pero el proceso NO existe"
    fi
else
    fail "PID_FILE $PID_FILE no existe — daemon nunca arrancó en esta sesión"
fi

MQA_PID_FILE=/data/adb/ivanna_mqa.pid
if [ -f "$MQA_PID_FILE" ]; then
    MQA_PID=$(cat "$MQA_PID_FILE" 2>/dev/null)
    if [ -n "$MQA_PID" ] && kill -0 "$MQA_PID" 2>/dev/null; then
        pass "mqa_monitor PID=$MQA_PID vivo"
    else
        warn "MQA_PID_FILE existe (PID=$MQA_PID) pero proceso no encontrado (normal si mqa no aplica)"
    fi
else
    warn "MQA_PID_FILE no existe — auto-preset por app no activo"
fi

# ── 4. Assets en /data/adb/ ──────────────────────────────────────────────────
section "ASSETS EN /data/adb/ivanna_omega/"
DATA_DIR=/data/adb/ivanna_omega

[ -d "$DATA_DIR" ] && pass "directorio $DATA_DIR existe" \
    || fail "directorio $DATA_DIR NO existe (customize.sh no corrió)"

SAF="$DATA_DIR/SAF_model.json"
[ -f "$SAF" ] && pass "SAF_model.json presente ($(stat -c%s "$SAF" 2>/dev/null || echo ?) bytes)" \
    || fail "SAF_model.json AUSENTE — Φ_SAF∞ usará constantes horneadas"

HRTF_N=$(ls "$DATA_DIR/hrtf/"*.ihr1 2>/dev/null | wc -l)
[ "$HRTF_N" -ge 10 ] && pass "HRTF: $HRTF_N sujetos IHR1 presentes" \
    || ([ "$HRTF_N" -gt 0 ] && warn "HRTF: solo $HRTF_N sujetos (esperados ≥10)" \
        || fail "HRTF: directorio $DATA_DIR/hrtf/ vacío o inexistente")

RIR_N=$(ls "$DATA_DIR/rir/"*.wav 2>/dev/null | wc -l)
[ "$RIR_N" -ge 180 ] && pass "RIR: $RIR_N salas presentes" \
    || ([ "$RIR_N" -gt 0 ] && warn "RIR: solo $RIR_N salas (esperadas 200)" \
        || fail "RIR: directorio $DATA_DIR/rir/ vacío o inexistente")

SOFA_N=$(ls "$DATA_DIR/sofa/"*.sofa 2>/dev/null | wc -l)
[ "$SOFA_N" -ge 10 ] && pass "SOFA: $SOFA_N archivos AES69 presentes" \
    || ([ "$SOFA_N" -gt 0 ] && warn "SOFA: solo $SOFA_N archivos (esperados ≥20)" \
        || fail "SOFA: directorio $DATA_DIR/sofa/ vacío (customize.sh puede necesitar reinstalar)")

# ── 5. Logs recientes ─────────────────────────────────────────────────────────
section "LOGS"
DAEMON_LOG="$DATA_DIR/daemon.log"
if [ -f "$DAEMON_LOG" ]; then
    LOG_LINES=$(wc -l < "$DAEMON_LOG" 2>/dev/null || echo 0)
    LOG_SIZE=$(stat -c%s "$DAEMON_LOG" 2>/dev/null || echo 0)
    pass "daemon.log: $LOG_LINES líneas, ${LOG_SIZE} bytes"
    # Mostrar últimas 5 líneas del log para diagnóstico rápido
    LAST_LOG=$(tail -5 "$DAEMON_LOG" 2>/dev/null | sed 's/^/    /')
    RESULTS="$RESULTS\n    Últimas entradas:\n$LAST_LOG"
else
    warn "daemon.log no existe — daemon aún no ha corrido o log rotado"
fi

# Buscar errores recientes en el log
if [ -f "$DAEMON_LOG" ]; then
    ERRORS=$(grep -c "ERROR\|FATAL\|EADDRINUSE\|bind failed" "$DAEMON_LOG" 2>/dev/null || echo 0)
    [ "$ERRORS" -eq 0 ] && pass "0 errores FATAL/ERROR en daemon.log" \
        || warn "$ERRORS entradas ERROR/FATAL en daemon.log (revisar $DAEMON_LOG)"
fi

# ── 6. Safe mode ──────────────────────────────────────────────────────────────
section "SAFE MODE / ANTI-BOOTLOOP"
BOOT_COUNTER=/data/adb/ivanna_omega_boot_counter
LAST_OK=/data/adb/ivanna_omega_last_boot_ok
COUNT=$(cat "$BOOT_COUNTER" 2>/dev/null || echo 0)

if [ -f "$LAST_OK" ]; then
    pass "LAST_OK presente — service.sh completó boot estable"
else
    [ "$COUNT" -ge 3 ] && fail "LAST_OK ausente + COUNT=$COUNT — safe_mode puede estar activo" \
        || warn "LAST_OK ausente (COUNT=$COUNT) — service.sh aún no completó un loop estable"
fi

MODDIR="${0%/*}"
[ -f "$MODDIR/.safe_mode" ] && fail "SAFE_MODE ACTIVO — audio_effects desactivados para proteger boot" \
    || pass "safe_mode inactivo"

# ── 7. SELinux ────────────────────────────────────────────────────────────────
section "SELINUX"
SE_MODE=$(getenforce 2>/dev/null || echo "desconocido")
if [ "$SE_MODE" = "Enforcing" ]; then
    warn "SELinux: Enforcing — reglas del módulo deben estar aplicadas"
    # Comprobar si la regla clave está activa
    if command -v sesearch >/dev/null 2>&1; then
        sesearch --allow -s untrusted_app -t su -c unix_stream_socket -p connectto 2>/dev/null \
            | grep -q "connectto" && pass "SELinux: untrusted_app→su connectto CONFIRMADA" \
            || warn "SELinux: untrusted_app→su connectto no verificable (sesearch no disponible)"
    else
        warn "SELinux: sesearch no disponible — no se puede verificar reglas inline"
    fi
elif [ "$SE_MODE" = "Permissive" ]; then
    pass "SELinux: Permissive — sin restricciones"
else
    warn "SELinux: estado desconocido ($SE_MODE)"
fi

# ── 8. Resumen ────────────────────────────────────────────────────────────────
TOTAL=$((OK+WARN+FAIL))
printf "\n╔══════════════════════════════════════════════════╗\n"
printf "║  IVANNA OMEGA SUPREME — Health Check v1.0       ║\n"
printf "╠══════════════════════════════════════════════════╣\n"
printf "%b\n" "$RESULTS"
printf "\n╠══════════════════════════════════════════════════╣\n"
printf "║  ✅ OK: %-3d  ⚠️  WARN: %-3d  ❌ FAIL: %-3d        ║\n" "$OK" "$WARN" "$FAIL"
if [ "$FAIL" -eq 0 ] && [ "$WARN" -le 2 ]; then
    printf "║  ESTADO GENERAL: ✅ OPERATIVO                    ║\n"
elif [ "$FAIL" -eq 0 ]; then
    printf "║  ESTADO GENERAL: ⚠️  OPERATIVO CON ADVERTENCIAS   ║\n"
else
    printf "║  ESTADO GENERAL: ❌ DEGRADADO — revisar FAILs     ║\n"
fi
printf "╚══════════════════════════════════════════════════╝\n"

if [ "$JSON" -eq 1 ]; then
    printf '{"ok":%d,"warn":%d,"fail":%d,"daemon_active":"%s","version":"%s"}\n' \
        "$OK" "$WARN" "$FAIL" "$DAEMON_ACTIVE" "$MODULE_VER"
fi

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
