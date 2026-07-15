#!/system/bin/sh
# IVANNA OMEGA SUPREME — service.sh v2.0
# Late-start: daemon real-time + MQA monitor + anti-bootloop reset.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log
SOCKET=/dev/socket/ivanna_omega
DAEMON_BIN="$MODDIR/system/bin/ivanna_daemon"

log() { echo "[$(date '+%H:%M:%S')] service: $1" >> "$LOG"; }

# ── 1. Esperar boot ────────────────────────────────────────────────────────────
until [ "$(getprop sys.boot_completed)" = "1" ]; do sleep 2; done
log "boot_completed"

# ── 2. Resetear contador anti-bootloop ────────────────────────────────────────
echo "0" > /data/adb/ivanna_omega_boot_counter
log "bootloop counter reset"

# ── 3. Verificar audioserver activo ───────────────────────────────────────────
AUDIO_PID=$(pidof audioserver 2>/dev/null | awk '{print $1}')
if [ -n "$AUDIO_PID" ]; then
    log "audioserver pid=$AUDIO_PID"
    # Aplicar prioridad real-time al audioserver (si tenemos permisos)
    chrt -f -p 96 "$AUDIO_PID" 2>/dev/null && log "audioserver → SCHED_FIFO 96"
else
    log "ADVERTENCIA: audioserver no encontrado"
fi

# ── 4. Iniciar daemon IVANNA con prioridad real-time ──────────────────────────
if [ -f "$DAEMON_BIN" ]; then
    # Lanzar daemon en background
    nohup "$DAEMON_BIN" \
        --socket "$SOCKET" \
        --rate 48000 \
        --buffer 64 \
        --realtime \
        > /data/adb/ivanna_daemon.log 2>&1 &
    DAEMON_PID=$!
    sleep 1

    # Verificar que arrancó
    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        log "ivanna_daemon arrancó pid=$DAEMON_PID"

        # SCHED_FIFO prioridad 98 (máxima para audio, debajo de IRQ en 99)
        chrt -f -p 98 "$DAEMON_PID" 2>/dev/null \
            && log "ivanna_daemon → SCHED_FIFO 98" \
            || log "ADVERTENCIA: chrt SCHED_FIFO falló (necesita CAP_SYS_NICE)"

        # CPU affinity: big cores (ARM big.LITTLE: CPUs 4-7 en Snapdragon 7xx/8xx)
        # Detect big cores dynamically
        BIG_CORES=""
        for cpu in $(seq 0 7); do
            FREQ_MAX=$(cat /sys/devices/system/cpu/cpu${cpu}/cpufreq/cpuinfo_max_freq 2>/dev/null || echo 0)
            [ "$FREQ_MAX" -gt 2000000 ] && BIG_CORES="${BIG_CORES}${cpu},"
        done
        BIG_CORES="${BIG_CORES%,}"
        if [ -n "$BIG_CORES" ]; then
            taskset -pc "$BIG_CORES" "$DAEMON_PID" 2>/dev/null \
                && log "ivanna_daemon → big cores [$BIG_CORES]"
        fi

        # Prioridad de I/O clase real-time nivel 0
        ionice -c 1 -n 0 -p "$DAEMON_PID" 2>/dev/null \
            && log "ivanna_daemon → ionice RT 0"

        echo "$DAEMON_PID" > /data/adb/ivanna_daemon.pid

        # Restauración mínima post-boot: confirmar canal de control y dejar
        # defaults seguros si no hay configuración persistida del daemon.
        if [ -x "$MODDIR/ivanna_control.sh" ]; then
            "$MODDIR/ivanna_control.sh" status >> "$LOG" 2>&1
            "$MODDIR/ivanna_control.sh" bypass off >> "$LOG" 2>&1
            "$MODDIR/ivanna_control.sh" volume 0.8 >> "$LOG" 2>&1
            log "ivanna_control restore aplicado"
        fi
    else
        log "ERROR: ivanna_daemon no arrancó"
    fi
else
    log "ivanna_daemon no encontrado en $DAEMON_BIN — modo app-only"
fi

# ── 5. Monitor MQA / Tidal / Qobuz ───────────────────────────────────────────
if [ -x "$MODDIR/mqa_monitor.sh" ]; then
    nohup "$MODDIR/mqa_monitor.sh" "$MODDIR" > /data/adb/ivanna_mqa.log 2>&1 &
    log "mqa_monitor arrancado pid=$!"
fi

# ── 6. Registrar estado de audio_effects en el log ───────────────────────────
if command -v dumpsys >/dev/null 2>&1; then
    dumpsys media.audio_policy 2>/dev/null \
        | grep -A2 -i "omega\|dolby\|effect" >> "$LOG" 2>&1
fi

log "service.sh completado"
