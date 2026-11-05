#!/system/bin/sh
# pcm_owner.sh — SOLO LECTURA. Dice que proceso tiene abierto cada nodo PCM de /dev/snd y
# estado de cada stream en /proc/asound. Requiere root (su -c). No modifica nada.
# Sirve para responder con evidencia: el audio de la app de musica sale por pcmC1D0p (DAC USB)
# abierto por el HAL, o por pcmC0D2p (USB_AUDIO-RX, posible offload en ADSP)?
SND="${IVANNA_SND_DIR:-/dev/snd}"
PROC="${IVANNA_PROC_DIR:-/proc}"
for node in "$SND"/pcmC*D*p; do
  [ -e "$node" ] || continue
  owners=""
  for d in "$PROC"/[0-9]*; do
    pid="${d##*/}"
    ls -l "$d/fd" 2>/dev/null | grep -q "$node" && owners="$owners $pid($(tr '\0' ' ' < "$d/cmdline" 2>/dev/null | cut -c1-60))"
  done
  echo "node=$node owners=${owners:- ninguno}"
done
for st in /proc/asound/card*/pcm*p/sub0/status; do
  [ -r "$st" ] || continue
  echo "--- $st"; head -n 8 "$st"
done
echo "nota: esto lista quien tiene el nodo abierto; no prueba por si solo el camino del PCM de la app."
