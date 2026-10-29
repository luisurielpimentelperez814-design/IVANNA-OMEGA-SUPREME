#!/system/bin/sh
# capture_usb_clock.sh — captura SOLO LECTURA, una sola sesión, para identificar el actuador ASRC / reloj USB.
# Ejecutar como root en el teléfono:  su -c "sh /sdcard/Download/capture_usb_clock.sh"
# No escribe controles de mixer, no abre PCM, no modifica propiedades.
#
# Fases: PRE (sin reproducir) -> DURING (reproduciendo por USB) -> POST (parado).
# Entrega: /sdcard/Download/ivanna_capture_<ts>.tar.gz

[ "$(id -u)" = 0 ] || { echo "ERROR: ejecuta como root (su)."; exit 1; }

TS=$(date +%Y%m%d_%H%M%S)
OUT=/data/local/tmp/ivanna_capture_$TS
mkdir -p "$OUT" || exit 1
HERE=$(cd "$(dirname "$0")" && pwd)
SAMPLE_SECONDS=${SAMPLE_SECONDS:-15}

say() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$OUT/session.log"; }
wait_enter() { echo ">>> $1  (Enter para continuar)"; read _x < /dev/tty; }

# ---------- 0. identidad ----------
{
  echo "model=$(getprop ro.product.model) device=$(getprop ro.product.device) board=$(getprop ro.board.platform)"
  echo "android=$(getprop ro.build.version.release) sdk=$(getprop ro.build.version.sdk) kernel=$(uname -r)"
  echo "--- audio/usb props"; getprop | grep -i -E "audio|usb" 
  echo "--- /dev/snd"; ls -l /dev/snd
  echo "--- procesos audio"; ps -A -o PID,NAME 2>/dev/null | grep -i -E "audio|agm|pal|ivanna"
} > "$OUT/00_identity.txt" 2>&1

# v2 (2026-10-29): la captura 20261028_171142 revelo que el glob /proc/asound/card*/pcm*p/sub*/status NO coincide
# en este kernel (el USB 'JieLi' es card0/pcm0p y no hay sub*/status: procfs sin SND_VERBOSE_PROCFS). Por eso:
#  - se registra explicitamente que existe bajo /proc/asound/cardN (ls -lZ) y la config del kernel;
#  - se muestrea ademas la estadistica de timestamps de AudioFlinger (unica fuente de reloj de salida visible
#    sin hooks), con hora monotonica, cada 5 s;
#  - el logcat excluye las denegaciones de omega_shm, que inundaban el tail y ocultaban HAL-CLOCK/ASRC.
# ---------- helpers ----------
proc_layout() { # $1 = fase
  { echo "== ls -lZ /proc/asound"; ls -lZ /proc/asound 2>&1
    for c in /proc/asound/card[0-9]*; do [ -d "$c" ] || continue; echo "== ls -lZ $c"; ls -lZ "$c" 2>&1
      for p in "$c"/pcm*p; do [ -d "$p" ] && { echo "== ls -lZ $p"; ls -lZ "$p" 2>&1; for sd in "$p"/sub*; do [ -d "$sd" ] && { echo "-- $sd"; ls -lZ "$sd" 2>&1; }; done; }; done
      [ -f "$c/stream0" ] && { echo "== $c/stream0"; cat "$c/stream0" 2>&1; }
      [ -f "$c/usbid" ] && { echo "== $c/usbid"; cat "$c/usbid" 2>&1; }
    done
    echo "== kernel SND procfs"; (zcat /proc/config.gz 2>/dev/null | grep -E "SND_PROC_FS|SND_VERBOSE_PROCFS|SND_USB_AUDIO") || echo "(config.gz no disponible)"
  } > "$OUT/${1}_proc_layout.txt" 2>&1
}
af_ts_stats() { # una linea con hora monotonica por hilo de salida con estadisticas de timestamps
  dumpsys media.audio_flinger 2>/dev/null | grep -E "^Output thread|Output devices:|Timestamp stats|localSR|underruns=|writeErrors=" \
    | sed "s/^/$(cat /proc/uptime | cut -d' ' -f1) /"
}
find_tinymix() {
  for t in tinymix tinymix_stock /vendor/bin/tinymix /vendor/bin/tinymix_stock /system/bin/tinymix; do
    case "$t" in /*) [ -x "$t" ] && { echo "$t"; return; } ;; *) command -v "$t" >/dev/null 2>&1 && { echo "$t"; return; } ;; esac
  done
}
TM=$(find_tinymix)

snapshot() { # $1 = fase
  P="$1"
  say "snapshot $P"
  proc_layout "$P"
  af_ts_stats > "$OUT/${P}_af_timestamp_stats.txt" 2>&1
  cp /proc/asound/cards "$OUT/${P}_cards.txt" 2>&1
  cp /proc/asound/pcm   "$OUT/${P}_pcm.txt"   2>&1
  # estado de TODOS los substreams de reproducción (no se asume cuál es el USB)
  for f in /proc/asound/card*/pcm*p/sub*/status; do
    d=$(dirname "$f"); tag=$(echo "$d" | sed 's#/proc/asound/##; s#/#_#g')
    { echo "== $d"; cat "$d/info" 2>&1; echo "-- status"; cat "$f" 2>&1; echo "-- hw_params"; cat "$d/hw_params" 2>&1; echo "-- sw_params"; cat "$d/sw_params" 2>&1; } > "$OUT/${P}_pcm_${tag}.txt"
  done
  # mixer de CADA tarjeta, con el comando exacto registrado
  if [ -n "$TM" ]; then
    for c in $(sed -n 's/^ *\([0-9]\+\) \[.*/\1/p' /proc/asound/cards); do
      {
        echo "CMD: $TM -D $c contents"
        $TM -D "$c" contents 2>&1 || { echo "CMD(fallback): $TM -D $c"; $TM -D "$c" 2>&1; }
      } > "$OUT/${P}_mixer_card$c.txt"
    done
  else
    echo "tinymix no encontrado" > "$OUT/${P}_mixer_NONE.txt"
  fi
  dumpsys media.audio_flinger > "$OUT/${P}_audioflinger.txt" 2>&1
  dumpsys audio               > "$OUT/${P}_audio.txt"        2>&1
  dumpsys media.audio_policy  > "$OUT/${P}_audiopolicy.txt"  2>&1
}

running_substreams() {
  for f in /proc/asound/card*/pcm*p/sub*/status; do
    grep -q '^closed' "$f" 2>/dev/null || echo "$f"
  done
}

sample_status() { # $1 = segundos; formato: wall_antes wall_despues ruta status...
  END=$(( $(date +%s) + $1 ))
  while [ "$(date +%s)" -lt "$END" ]; do
    for f in $RUNNING; do
      a=$(date +%s%N); s=$(cat "$f" 2>/dev/null | tr '\n' ' '); b=$(date +%s%N)
      echo "$a $b $f $s"
    done
    sleep 0.05
  done
}

# ---------- 1. PRE ----------
say "Fase PRE: cierra toda reproducción. Conecta el DAC/auricular USB."
wait_enter "Sin audio sonando y con el USB conectado"
snapshot pre

# ---------- 2. Frida (antes de reproducir) ----------
PIDS=$(ps -A -o PID,NAME 2>/dev/null | grep -i -E "audio\.service|audio-hal|audiohal|agm|audioserver|pal" | grep -v grep | sed 's/^ *\([0-9]\+\) .*/\1/' | sort -u)
echo "$PIDS" > "$OUT/frida_target_pids.txt"
JS="$HERE/hook_pal_agm.js"
if command -v frida >/dev/null 2>&1 && [ -f "$JS" ]; then
  say "frida local detectado: adjuntando a PIDs: $(echo $PIDS)"
  for p in $PIDS; do
    ( timeout $((SAMPLE_SECONDS + 240)) frida -p "$p" -l "$JS" -q -t $((SAMPLE_SECONDS + 200)) > "$OUT/frida_$p.log" 2>&1 & )
  done
  sleep 4
else
  say "frida NO está en el teléfono. Desde el PC (frida-server corriendo como root) adjunta AHORA a cada PID:"
  for p in $PIDS; do echo "    frida -U -p $p -l hook_pal_agm.js -o frida_$p.log"; done
  wait_enter "Hooks adjuntos (o sigue sin ellos si no puedes)"
fi

# ---------- 3. DURING ----------
wait_enter "AHORA inicia la reproducción de IVANNA por la salida USB y confirma que se oye"
sleep 2
RUNNING=$(running_substreams)
echo "$RUNNING" > "$OUT/running_substreams.txt"
[ -z "$RUNNING" ] && say "AVISO: ningún substream de reproducción en uso (¿offload fuera de /proc/asound?)"
snapshot during
say "muestreando $SAMPLE_SECONDS s a ~20 Hz: $(echo $RUNNING)"
( E=$(( $(date +%s) + SAMPLE_SECONDS )); while [ "$(date +%s)" -lt "$E" ]; do af_ts_stats; sleep 5; done ) > "$OUT/during_af_timestamp_series.txt" 2>&1 &
AFPID=$!
sample_status "$SAMPLE_SECONDS" > "$OUT/during_status_samples.txt"
wait $AFPID 2>/dev/null
logcat -d -v threadtime 2>/dev/null | grep -c "omega_shm" > "$OUT/during_omega_shm_denial_count.txt"
logcat -d -v threadtime 2>/dev/null | grep -i -E "HAL-CLOCK|ASRC|SAFMUS|OUTPUT_ROUTE|ivanna|USB audio" | grep -v "omega_shm" | tail -n 600 > "$OUT/during_ivanna_logcat.txt"
{ echo "--- ivanna_daemon"; ps -A -o PID,NAME | grep -i ivanna; } > "$OUT/during_ivanna_proc.txt" 2>&1

# ---------- 4. POST ----------
wait_enter "DETÉN la reproducción"
sleep 2
snapshot post

# ---------- 5. diffs y empaquetado ----------
for f in "$OUT"/pre_mixer_card*.txt; do
  [ -f "$f" ] || continue; n=$(basename "$f" | sed 's/^pre_//')
  diff -u "$f" "$OUT/during_$n" > "$OUT/DIFF_mixer_pre_vs_during_$n" 2>&1
done
mkdir -p /sdcard/Download
tar -czf "/sdcard/Download/ivanna_capture_$TS.tar.gz" -C /data/local/tmp "ivanna_capture_$TS" 2>&1
say "LISTO: /sdcard/Download/ivanna_capture_$TS.tar.gz"
