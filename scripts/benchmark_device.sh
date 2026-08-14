#!/system/bin/sh
# benchmark_device.sh — IVANNA OMEGA SUPREME v1.0
# Mide latencia end-to-end, CPU, RAM, térmica y estabilidad del daemon en dispositivo real.
#
# Uso (como root desde adb shell):
#   adb shell su -c "sh /data/adb/modules/ivanna_omega_supreme/scripts/benchmark_device.sh"
#
# Salida: JSON en /data/adb/ivanna_omega/benchmark_<timestamp>.json

RESULTS_DIR="/data/adb/ivanna_omega"
TIMESTAMP=$(date +%s)
OUT="$RESULTS_DIR/benchmark_${TIMESTAMP}.json"
mkdir -p "$RESULTS_DIR"

log()  { echo "[BENCH] $*"; }
warn() { echo "[WARN]  $*"; }
fail() { echo "[FAIL]  $*" >&2; exit 1; }

[ "$(id -u)" = "0" ] || fail "Requiere root: adb shell → su"
command -v dumpsys >/dev/null 2>&1 || fail "dumpsys no disponible"

SOCKET_ALIVE=0
grep -q "@omega_daemon_socket$" /proc/net/unix 2>/dev/null && SOCKET_ALIVE=1
[ "$SOCKET_ALIVE" = "1" ] || warn "daemon no detectado — latencia socket = null"

log "IVANNA OMEGA benchmark — output: $OUT"

# ── 1. Latencia AudioFlinger ──────────────────────────────────────────────────
log "1/8 latencia AudioFlinger..."
AF_RAW=$(dumpsys media.audio_flinger 2>/dev/null | grep -iE "output latency|latency:" | head -5)
LATENCY_MS=$(echo "$AF_RAW" | grep -oE "[0-9]+\.[0-9]+" | head -1)
[ -z "$LATENCY_MS" ] && LATENCY_MS=$(echo "$AF_RAW" | grep -oE "[0-9]+" | head -1)
[ -z "$LATENCY_MS" ] && LATENCY_MS="null"
log "   AudioFlinger latency: ${LATENCY_MS} ms"

# ── 2. CPU daemon (5 muestras × 1 s) ─────────────────────────────────────────
log "2/8 CPU daemon (5 s)..."
DAEMON_PID=$(cat /data/adb/ivanna_daemon.pid 2>/dev/null || \
             pgrep -x ivanna_daemon 2>/dev/null | head -1 || echo "")
CPU_AVG="null"
if [ -n "$DAEMON_PID" ] && [ -d "/proc/$DAEMON_PID" ]; then
    SUM=0; COUNT=0
    for i in 1 2 3 4 5; do
        T1=$(cut -d' ' -f14,15 /proc/$DAEMON_PID/stat 2>/dev/null | awk '{print $1+$2}')
        U1=$(cut -d' ' -f1 /proc/uptime 2>/dev/null)
        sleep 1
        T2=$(cut -d' ' -f14,15 /proc/$DAEMON_PID/stat 2>/dev/null | awk '{print $1+$2}')
        U2=$(cut -d' ' -f1 /proc/uptime 2>/dev/null)
        PCT=$(awk "BEGIN{dt=($U2-$U1)*100; if(dt>0) printf \"%.2f\",($T2-$T1)/dt*100; else print 0}")
        SUM=$(awk "BEGIN{print $SUM + $PCT}"); COUNT=$((COUNT+1))
    done
    CPU_AVG=$(awk "BEGIN{if($COUNT>0) printf \"%.2f\",$SUM/$COUNT; else print 0}")
    log "   CPU avg: ${CPU_AVG}%"
else
    warn "daemon PID no encontrado — CPU = null"
fi

# ── 3. RAM daemon ─────────────────────────────────────────────────────────────
log "3/8 RAM daemon..."
RAM_KB="null"
if [ -n "$DAEMON_PID" ] && [ -d "/proc/$DAEMON_PID" ]; then
    RAM_KB=$(grep VmRSS /proc/$DAEMON_PID/status 2>/dev/null | awk '{print $2}')
    log "   VmRSS: ${RAM_KB} kB"
fi

# ── 4. Temperatura SoC ────────────────────────────────────────────────────────
log "4/8 temperatura SoC..."
TEMPS=""
for zone in 0 1 2 7 8; do
    p="/sys/class/thermal/thermal_zone${zone}/temp"
    [ -f "$p" ] || continue
    raw=$(cat "$p" 2>/dev/null)
    cel=$(awk "BEGIN{if($raw>1000) printf \"%.1f\",$raw/1000; else print $raw}")
    TEMPS="${TEMPS}{\"zone\":$zone,\"celsius\":$cel},"
done
TEMPS="${TEMPS%,}"
[ -z "$TEMPS" ] && TEMPS="{\"zone\":0,\"celsius\":null}"
log "   Temps: $TEMPS"

# ── 5. Frecuencia CPU cluster de rendimiento ──────────────────────────────────
log "5/8 CPU freq..."
CPU_FREQ="null"
for cpu in cpu7 cpu6 cpu4 cpu0; do
    fp="/sys/devices/system/cpu/${cpu}/cpufreq/scaling_cur_freq"
    if [ -f "$fp" ]; then
        raw=$(cat "$fp" 2>/dev/null)
        CPU_FREQ=$(awk "BEGIN{printf \"%.0f\",$raw/1000}")
        log "   ${cpu} freq: ${CPU_FREQ} MHz"
        break
    fi
done

# ── 6. Clip count SafetyLimiter ───────────────────────────────────────────────
log "6/8 clip count SafetyLimiter..."
CLIP_COUNT="null"
CTL="/data/adb/modules/ivanna_omega_supreme/ivanna_control.sh"
if [ -f "$CTL" ]; then
    RAW=$(sh "$CTL" telemetry 2>/dev/null || true)
    CC=$(echo "$RAW" | grep -oE '"clip_count":[0-9]+' | grep -oE '[0-9]+$' | head -1)
    [ -n "$CC" ] && CLIP_COUNT=$CC
fi
log "   clip_count: $CLIP_COUNT"

# ── 7. Latencia round-trip socket daemon ──────────────────────────────────────
log "7/8 socket round-trip..."
SOCKET_US="null"
if [ "$SOCKET_ALIVE" = "1" ] && command -v nc >/dev/null 2>&1; then
    T1=$(date +%s%N 2>/dev/null || echo 0)
    echo '{"cmd":"ping"}' | nc -U "@omega_daemon_socket" -w1 >/dev/null 2>&1 || true
    T2=$(date +%s%N 2>/dev/null || echo 0)
    [ "$T1" != "0" ] && [ "$T2" != "0" ] && \
        SOCKET_US=$(awk "BEGIN{printf \"%.0f\",($T2-$T1)/1000}")
    log "   socket round-trip: ${SOCKET_US} µs"
fi

# ── 8. Metadatos del dispositivo ──────────────────────────────────────────────
log "8/8 metadatos..."
MODEL=$(getprop ro.product.model 2>/dev/null || echo "unknown")
ANDROID=$(getprop ro.build.version.release 2>/dev/null || echo "unknown")
KERNEL=$(uname -r 2>/dev/null || echo "unknown")
SOC=$(getprop ro.hardware 2>/dev/null || echo "unknown")
SELINUX=$(getenforce 2>/dev/null || echo "unknown")

# ── Evaluar umbrales ──────────────────────────────────────────────────────────
PASS_LAT="null"; PASS_CPU="null"; PASS_RAM="null"; PASS_CLIP="null"
[ "$LATENCY_MS" != "null" ] && \
    PASS_LAT=$(awk "BEGIN{print ($LATENCY_MS+0 <= 5) ? \"true\" : \"false\"}")
[ "$CPU_AVG" != "null" ] && \
    PASS_CPU=$(awk "BEGIN{print ($CPU_AVG+0 <= 5) ? \"true\" : \"false\"}")
[ "$RAM_KB" != "null" ] && \
    PASS_RAM=$(awk "BEGIN{print ($RAM_KB+0 <= 8192) ? \"true\" : \"false\"}")
[ "$CLIP_COUNT" = "0" ] && PASS_CLIP="true"
[ "$CLIP_COUNT" = "null" ] && PASS_CLIP="null"
[ "$CLIP_COUNT" != "null" ] && [ "$CLIP_COUNT" != "0" ] && PASS_CLIP="false"

# ── Escribir JSON ─────────────────────────────────────────────────────────────
cat > "$OUT" << ENDJSON
{
  "ivanna_benchmark": {
    "version": "1.0",
    "timestamp_unix": $TIMESTAMP,
    "device": {
      "model": "$MODEL",
      "android": "$ANDROID",
      "kernel": "$KERNEL",
      "soc": "$SOC",
      "selinux": "$SELINUX"
    },
    "daemon": {
      "pid": "${DAEMON_PID:-null}",
      "socket_alive": $SOCKET_ALIVE,
      "cpu_avg_pct": $CPU_AVG,
      "ram_kb": ${RAM_KB:-null},
      "socket_roundtrip_us": $SOCKET_US,
      "clip_count": $CLIP_COUNT
    },
    "audio": {
      "audioflinger_latency_ms": $LATENCY_MS
    },
    "thermal": {
      "zones": [$TEMPS]
    },
    "cpu_perf_mhz": $CPU_FREQ,
    "thresholds": {
      "latency_ms": 5,
      "cpu_pct": 5,
      "ram_kb": 8192,
      "clip_count": 0
    },
    "pass": {
      "latency": $PASS_LAT,
      "cpu":     $PASS_CPU,
      "ram":     $PASS_RAM,
      "clips":   $PASS_CLIP
    }
  }
}
ENDJSON

# ── Resumen ───────────────────────────────────────────────────────────────────
log ""
log "══════════════════════════════════════════"
log " IVANNA OMEGA — RESULTADO BENCHMARK"
log "══════════════════════════════════════════"
log " Latencia AudioFlinger : ${LATENCY_MS} ms    (≤ 5 ms → $PASS_LAT)"
log " CPU daemon (avg 5 s)  : ${CPU_AVG}%         (≤ 5%   → $PASS_CPU)"
log " RAM daemon            : ${RAM_KB} kB        (≤ 8192 → $PASS_RAM)"
log " Clip count limiter    : $CLIP_COUNT          (= 0    → $PASS_CLIP)"
log " Socket round-trip     : ${SOCKET_US} µs"
log "══════════════════════════════════════════"
log " JSON: $OUT"

# ── TAREA 1: latencia DSP round-trip (medida in-app vía JNI CLOCK_MONOTONIC) ──
# La app escribe /data/local/tmp/ivanna_dsp_roundtrip_us.json al correr el
# benchmark; si no existe (app no corrida o sin permisos), se reporta n/a.
DSP_RT_FILE="/data/local/tmp/ivanna_dsp_roundtrip_us.json"
if [ -f "$DSP_RT_FILE" ]; then
    DSP_RT=$(cat "$DSP_RT_FILE")
else
    DSP_RT='{"dsp_roundtrip_us":"n/a"}'
fi
echo "$DSP_RT"
