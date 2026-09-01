#!/system/bin/sh
# ==============================================================================
# IVANNA Autonomous Runtime Platform
# Main Lifecycle & Intelligence Loop
# ==============================================================================

MODDIR="${0%/*}"
source "$MODDIR/core/ivanna_autonomous_core.sh"

ivanna_platform_init "$MODDIR"

# ── FIX (socket no conecta al daemon tras reboot, 2026-08-31) ─────────────
# customize.sh aplica sepolicy.rule SOLO en instalación via magiskpolicy
# --live (volátil, se pierde al reiniciar). Sin reaplicarla en cada boot,
# SELinux enforcing vuelve a denegar el connect() de untrusted_app sobre
# @omega_daemon_socket/@omega_command_socket → la app reporta "daemon
# DETENIDO/DESCONECTADO" aunque el daemon esté vivo y el socket bindeado.
# Se reaplica aquí (service.sh corre en late_start, boot completado) con
# espera a que magiskpolicy esté disponible en el PATH del servicio.
if [ -f "$MODDIR/sepolicy.rule" ]; then
    for _i in 1 2 3 4 5 6 7 8 9 10; do
        if command -v magiskpolicy >/dev/null 2>&1; then
            magiskpolicy --live --apply "$MODDIR/sepolicy.rule" 2>/dev/null && break
        fi
        sleep 1
    done
fi

set_state "ENVIRONMENT_MAPPING"
build_device_runtime_profile
security_intelligence_scan
evolution_layer_check

set_state "OPTIMAL"

while true; do
    # 6. MOTOR DE DECISIONES & 10. CERO IMPACTO INVISIBLE
    if decision_evaluate "DAEMON_RUNTIME"; then
        # 7. CAPA DE AISLAMIENTO
        # Start daemon with both abstract socket and TCP loopback fallback for robustness
        if [ -x "$MODDIR/system/bin/ivanna_daemon" ]; then
            nohup "$MODDIR/system/bin/ivanna_daemon" --socket "@omega_daemon_socket" --tcp-port 12121 --realtime > /dev/null 2>&1 &
            echo $! > /data/adb/ivanna_omega/daemon.pid
        else
            log -p e -t "IVANNA" "ivanna_daemon binary missing"
        fi
        
        # ── Privilegios de clase OEM: OOM inmune + RT 98 (bajo AudioFlinger=~99)
        DAEMON_PID=$(cat /data/adb/ivanna_omega/daemon.pid 2>/dev/null)
        if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
            echo -1000 > /proc/$DAEMON_PID/oom_score_adj 2>/dev/null || true
            chrt -f -p 98 "$DAEMON_PID" 2>/dev/null || true
            # big.LITTLE: anclar daemon al cluster LITTLE (cores 0-3) — el DSP
            # de control no necesita big cores; deja los big para AudioFlinger
            # y apps. Ahorro energético medible en idle (cluster LITTLE ~40mW
            # vs ~300mW por big core en SD8 Gen2/3).
            if [ -f /sys/devices/system/cpu/cpu4/cpufreq/cpuinfo_max_freq ]; then
                taskset -p 0f "$DAEMON_PID" >/dev/null 2>&1 || true
            fi
            # Watchdog: latido compartido — el daemon escribe su uptime; si el
            # timestamp no avanza en 3 ciclos, se declara hang (no solo exit).
            echo "$(date +%s)" > "$ISOLATION_DIR/DAEMON/watchdog_ts" 2>/dev/null
            echo "0" > "$ISOLATION_DIR/DAEMON/restart_streak" 2>/dev/null || true
        fi
        setprop persist.ivanna.daemon_active 1 2>/dev/null
    fi

    if decision_evaluate "MQA_INTELLIGENCE"; then
        if [ -x "$MODDIR/mqa_monitor.sh" ]; then
            component_isolate_execute "MQA" "$MODDIR/mqa_monitor.sh" "$MODDIR"
            MQA_PID=$(cat "$ISOLATION_DIR/MQA/pid" 2>/dev/null)
            if [ -n "$MQA_PID" ]; then
                echo -1000 > /proc/$MQA_PID/oom_score_adj 2>/dev/null || true
            fi
        fi
    fi

    # 3. SISTEMA PREDICTIVO DE FALLOS
    if ! predictive_failure_engine; then
        set_state "DEGRADED"
        forensic_dump "Anomalía predictiva superó umbral crítico de tolerancia"
        self_healing_framework
    fi

    # Telemetría OEM: snapshot por ciclo (rotación interna, costo despreciable)
    [ -x "$MODDIR/core/ivanna_diag.sh" ] && "$MODDIR/core/ivanna_diag.sh" 2>/dev/null || true

    # Detectar caídas súbitas
    if component_crashed "DAEMON"; then
        setprop persist.ivanna.daemon_active 0 2>/dev/null
        forensic_dump "Colapso no controlado del componente aislado DAEMON"
        self_healing_framework
    fi

    # ── Zero Impact adaptativo: sondeo de actividad de audio via AudioFlinger.
    # Si no hay streams activos, el loop respira lento (30s) — el módulo cuesta
    # ~0% CPU en standby. Con audio activo, 5s para reacción rápida ante crash.
    AF_ACTIVE=$(dumpsys audio 2>/dev/null | grep -c "stream type:" 2>/dev/null || echo 0)
    if [ "${AF_ACTIVE:-0}" -gt 0 ] 2>/dev/null; then sleep 5; else sleep 30; fi
done
