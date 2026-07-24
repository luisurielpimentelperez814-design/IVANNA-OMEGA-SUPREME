#!/system/bin/sh
# IVANNA OMEGA SUPREME — service.sh v2.2 (PATCH: daemon ELF ARM64 integrado)
# Late-start: lanza daemon real-time, monitor MQA y marca boot OK
# para que post-fs-data no dispare safe_mode.

MODDIR=${0%/*}
LOG=/data/adb/ivanna_omega.log
SOCKET=/dev/socket/ivanna_omega
DAEMON_BIN="$MODDIR/system/bin/ivanna_daemon"
STATE_DIR=/data/adb/ivanna_omega
LAST_OK=/data/adb/ivanna_omega_last_boot_ok

log() { echo "[$(date '+%H:%M:%S')] service: $1" >> "$LOG" 2>/dev/null; }

# ── 0. Estado persistente y shared memory ───────────────────────────────────
mkdir -p "$STATE_DIR" 2>/dev/null
chmod 0755 "$STATE_DIR" 2>/dev/null
# Reservar archivo de shm persistente (mmap fallback usado por el daemon)
[ -f "$STATE_DIR/shm" ] || { : > "$STATE_DIR/shm"; chmod 0644 "$STATE_DIR/shm"; }

# ── 1. Esperar boot ─────────────────────────────────────────────────────────
until [ "$(getprop sys.boot_completed)" = "1" ]; do sleep 2; done
log "boot_completed"

# ── 2. Reset contador anti-bootloop y marca de boot OK ──────────────────────
echo "0" > /data/adb/ivanna_omega_boot_counter
touch "$LAST_OK"
log "bootloop counter reset + last_boot_ok tocado"

# ── 3. Detectar dependencias opcionales ─────────────────────────────────────
HAS_NC=0;    command -v nc    >/dev/null 2>&1 && HAS_NC=1
HAS_CHRT=0;  command -v chrt  >/dev/null 2>&1 && HAS_CHRT=1
HAS_TASKSET=0; command -v taskset >/dev/null 2>&1 && HAS_TASKSET=1
HAS_IONICE=0;  command -v ionice  >/dev/null 2>&1 && HAS_IONICE=1
log "deps: nc=$HAS_NC chrt=$HAS_CHRT taskset=$HAS_TASKSET ionice=$HAS_IONICE"

# ── 4. audioserver realtime hint ────────────────────────────────────────────
AUDIO_PID=$(pidof audioserver 2>/dev/null | awk '{print $1}')
if [ -n "$AUDIO_PID" ]; then
    log "audioserver pid=$AUDIO_PID"
    [ "$HAS_CHRT" = "1" ] && chrt -f -p 96 "$AUDIO_PID" 2>/dev/null \
        && log "audioserver → SCHED_FIFO 96"
else
    log "AVISO: audioserver no encontrado"
fi

# ── 4b. Validación ELF ARM64-v8a del daemon (magic + e_machine=183) ─────────
validate_daemon_elf() {
    [ -f "$1" ] || return 1
    hex=$(head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$hex" = "7f454c46" ] || return 1
    # bytes 18..19 little-endian = e_machine (0xB7 = 183 = AARCH64)
    mach=$(dd if="$1" bs=1 skip=18 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$mach" = "b700" ]
}

# ── 5. Daemon IVANNA ────────────────────────────────────────────────────────
DAEMON_STARTED=0
if [ -f "$DAEMON_BIN" ] && validate_daemon_elf "$DAEMON_BIN"; then
    chmod 0755 "$DAEMON_BIN" 2>/dev/null
    log "ivanna_daemon ELF ARM64 verificado (magic=7f454c46 e_machine=183)"

    nohup "$DAEMON_BIN" \
        --socket "$SOCKET" \
        --rate 48000 \
        --buffer 64 \
        --realtime \
        > /data/adb/ivanna_daemon.log 2>&1 &
    DAEMON_PID=$!
    sleep 1

    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        DAEMON_STARTED=1
        log "ivanna_daemon arrancó pid=$DAEMON_PID"
        [ "$HAS_CHRT" = "1" ] && chrt -f -p 98 "$DAEMON_PID" 2>/dev/null \
            && log "ivanna_daemon → SCHED_FIFO 98"

        # Big cores dynamic detection
        if [ "$HAS_TASKSET" = "1" ]; then
            BIG=""
            for cpu in 0 1 2 3 4 5 6 7; do
                F=$(cat /sys/devices/system/cpu/cpu${cpu}/cpufreq/cpuinfo_max_freq 2>/dev/null || echo 0)
                [ "$F" -gt 2000000 ] && BIG="${BIG}${cpu},"
            done
            BIG="${BIG%,}"
            [ -n "$BIG" ] && taskset -pc "$BIG" "$DAEMON_PID" 2>/dev/null \
                && log "ivanna_daemon → big cores [$BIG]"
        fi

        [ "$HAS_IONICE" = "1" ] && ionice -c 1 -n 0 -p "$DAEMON_PID" 2>/dev/null \
            && log "ivanna_daemon → ionice RT 0"

        echo "$DAEMON_PID" > /data/adb/ivanna_daemon.pid

        # Restore mínimos vía ivanna_control.sh (solo si tenemos nc)
        if [ "$HAS_NC" = "1" ] && [ -x "$MODDIR/ivanna_control.sh" ]; then
            "$MODDIR/ivanna_control.sh" status  >> "$LOG" 2>&1
            "$MODDIR/ivanna_control.sh" bypass off >> "$LOG" 2>&1
            "$MODDIR/ivanna_control.sh" volume 0.8 >> "$LOG" 2>&1
            log "ivanna_control restore aplicado"
        else
            log "restore omitido (nc=$HAS_NC, ctrl=$( [ -x $MODDIR/ivanna_control.sh ] && echo yes || echo no ))"
        fi
    else
        log "ERROR: ivanna_daemon no arrancó — ver /data/adb/ivanna_daemon.log"
    fi
else
    log "ivanna_daemon inválido/no instalado en $DAEMON_BIN — modo app-only (DSP local sigue vía libomega_effect)"
fi

# Propiedad de sistema que refleja estado real del daemon
setprop persist.ivanna.daemon_active "$DAEMON_STARTED"

# ── 6. MQA monitor ──────────────────────────────────────────────────────────
if [ "$DAEMON_STARTED" = "1" ] && [ -x "$MODDIR/mqa_monitor.sh" ]; then
    nohup "$MODDIR/mqa_monitor.sh" "$MODDIR" > /data/adb/ivanna_mqa.log 2>&1 &
    log "mqa_monitor arrancado pid=$!"
fi

# ── 7. Dump de estado de audio effects ──────────────────────────────────────
if command -v dumpsys >/dev/null 2>&1; then
    dumpsys media.audio_policy 2>/dev/null \
        | grep -A2 -i "omega\|dolby\|effect" >> "$LOG" 2>&1
fi

log "service.sh v2.2 completado"
