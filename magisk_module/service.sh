#!/system/bin/sh
# ==============================================================================
# IVANNA Autonomous Runtime Platform
# Main Lifecycle & Intelligence Loop
# ==============================================================================
MODDIR="${0%/*}"
source "$MODDIR/core/ivanna_autonomous_core.sh"

ivanna_platform_init "$MODDIR"

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
    if decision_evaluate "DAEMON_RUNTIME"; then
        if [ -x "$MODDIR/system/bin/ivanna_daemon" ]; then
            # HACK FALLBACK: Resolve APK path manually without `pm` for late_start execution
            APK_DIR=$(ls -d /data/app/*/*com.ivanna.omega* 2>/dev/null | head -n 1)
            if [ -z "$APK_DIR" ]; then
                APK_DIR=$(ls -d /data/app/*com.ivanna.omega* 2>/dev/null | head -n 1)
            fi

            if [ -n "$APK_DIR" ]; then
                export LD_LIBRARY_PATH="$APK_DIR/lib/arm64:$APK_DIR/lib/arm64-v8a:$LD_LIBRARY_PATH"
            fi
            
            nohup "$MODDIR/system/bin/ivanna_daemon" --socket "@omega_daemon_socket" --tcp-port 12121 --realtime > /data/adb/ivanna_omega/daemon.log 2>&1 &
            DAEMON_PID=$!
            echo $DAEMON_PID > /data/adb/ivanna_omega/daemon.pid
            
            # Allow some time to check if it crashed immediately
            sleep 1
            if kill -0 "$DAEMON_PID" 2>/dev/null; then
                setprop persist.ivanna.daemon_active 1 2>/dev/null
                echo -1000 > /proc/$DAEMON_PID/oom_score_adj 2>/dev/null || true
                chrt -f -p 98 "$DAEMON_PID" 2>/dev/null || true
                if [ -f /sys/devices/system/cpu/cpu4/cpufreq/cpuinfo_max_freq ]; then
                    taskset -p 0f "$DAEMON_PID" >/dev/null 2>&1 || true
                fi
                echo "$(date +%s)" > "$ISOLATION_DIR/DAEMON/watchdog_ts" 2>/dev/null
                echo "0" > "$ISOLATION_DIR/DAEMON/restart_streak" 2>/dev/null || true
            else
                log -p e -t "IVANNA" "ivanna_daemon crashed immediately on boot. Missing shared libs?"
                setprop persist.ivanna.daemon_active 0 2>/dev/null
            fi
        else
            log -p e -t "IVANNA" "ivanna_daemon binary missing or not executable"
            setprop persist.ivanna.daemon_active 0 2>/dev/null
        fi
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

    if ! predictive_failure_engine; then
        set_state "DEGRADED"
        forensic_dump "Anomalía predictiva superó umbral crítico de tolerancia"
        self_healing_framework
    fi

    [ -x "$MODDIR/core/ivanna_diag.sh" ] && "$MODDIR/core/ivanna_diag.sh" 2>/dev/null || true

    if component_crashed "DAEMON"; then
        setprop persist.ivanna.daemon_active 0 2>/dev/null
        forensic_dump "Colapso no controlado del componente aislado DAEMON"
        self_healing_framework
    fi

    AF_ACTIVE=$(dumpsys audio 2>/dev/null | grep -c "stream type:" 2>/dev/null || echo 0)
    if [ "${AF_ACTIVE:-0}" -gt 0 ] 2>/dev/null; then sleep 5; else sleep 30; fi
done
