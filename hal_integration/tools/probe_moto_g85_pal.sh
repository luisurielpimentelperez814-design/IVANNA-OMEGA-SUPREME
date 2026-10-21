#!/bin/sh
# probe_moto_g85_pal.sh — sonda de SOLO LECTURA para cerrar el enlace PAL <-> ASRC de IVANNA.
#
# Para qué: hal_integration/ivanna_qualcomm_pal_clock_integration.hpp necesita dos
# datos del DISPOSITIVO que no se pueden adivinar (ver MOTO_G85_PAL_CLOCK_INTEGRATION.md):
#   (1) de dónde sale una muestra PAREADA frames+timestamp (símbolo/estructura PAL real);
#   (2) qué módulo del grafo de audio actúa de ASRC/slip/drift y su control (ID/unidades).
# Esta sonda NO modifica nada: no escribe en el sistema, no cambia mixers, no abre streams.
# Solo lista símbolos, cadenas y configuración, y escribe un informe de texto.
#
# Uso (Termux, con root):
#   su -c "sh /ruta/probe_moto_g85_pal.sh"            # informe en /sdcard/Download/ivanna_pal_probe_<fecha>/
#   su -c "sh /ruta/probe_moto_g85_pal.sh /data/local/tmp/mi_informe"   # otra carpeta
# Opcional (mejor resultado): pkg install binutils   (aporta readelf/nm/strings)
#
# Overrides (si tu ROM usa otras rutas): IVANNA_PROBE_LIBDIRS="dir1 dir2" IVANNA_PROBE_ETCDIRS="dir1 dir2"
#
# Entrega: sube el .tar.gz resultante (o report.txt) y con eso se conecta el HAL con
# datos reales, sin suposiciones.

OUT="${1:-/sdcard/Download/ivanna_pal_probe_$(date +%Y%m%d_%H%M%S 2>/dev/null || echo run)}"
mkdir -p "$OUT" 2>/dev/null || { echo "No puedo crear $OUT"; exit 1; }
R="$OUT/report.txt"
: > "$R"

# Palabras que delatan un actuador de deriva/ASRC o un par frames+timestamp.
KW_ASRC='asrc|sample.?slip|slip|drift|rate.?adapt|rat_render|clk.?recov|clock.?recov|rate.?match|ppm|ratio'
KW_TS='get_timestamp|session_time|presentation_position|timestamp|absolute_time'

say()  { printf '%s\n' "$*" >> "$R"; }
hdr()  { say ""; say "=================== $* ==================="; }
have() { command -v "$1" >/dev/null 2>&1; }

hdr "0. Identidad del dispositivo"
for p in ro.product.model ro.product.device ro.board.platform ro.hardware ro.build.fingerprint \
         ro.build.version.release ro.build.version.sdk ro.vendor.build.fingerprint; do
  say "$p = $(getprop $p 2>/dev/null)"
done
say "uid = $(id 2>/dev/null)"
say "herramientas: readelf=$(have readelf && echo si || echo no) nm=$(have nm && echo si || echo no) strings=$(have strings && echo si || echo no) tinymix=$(have tinymix && echo si || echo no) dumpsys=$(have dumpsys && echo si || echo no)"

hdr "1. Bibliotecas PAL / HAL de audio presentes"
LIBS=""
LIBDIRS="${IVANNA_PROBE_LIBDIRS:-/vendor/lib64 /vendor/lib /system/lib64 /system/lib /system_ext/lib64 /odm/lib64 /vendor/lib64/hw}"
for d in $LIBDIRS; do
  [ -d "$d" ] || continue
  for f in "$d"/libar-pal.so "$d"/libpalclient.so "$d"/libagm*.so "$d"/libgsl.so "$d"/libats.so \
           "$d"/vendor.qti.hardware.pal@1.0-impl.so "$d"/audio.primary.*.so "$d"/libaudioroute*.so \
           "$d"/libaudiohalqti*.so "$d"/hw/audio.primary.*.so; do
    [ -f "$f" ] || continue
    LIBS="$LIBS $f"
    say "$(ls -l "$f" 2>/dev/null | awk '{print $5, $NF}')"
  done
done
[ -n "$LIBS" ] || say "(no se encontró ninguna: ¿falta root o la ruta es distinta?)"

hdr "2. Símbolos dinámicos relacionados con timestamp / ASRC (readelf/nm)"
for f in $LIBS; do
  if have readelf; then SYMS="$(readelf -Ws --dyn-syms "$f" 2>/dev/null | awk '{print $NF}')"
  elif have nm;    then SYMS="$(nm -D "$f" 2>/dev/null | awk '{print $NF}')"
  else SYMS=""; fi
  [ -n "$SYMS" ] || continue
  HIT="$(printf '%s\n' "$SYMS" | grep -i -E "$KW_TS|$KW_ASRC" | sort -u)"
  [ -n "$HIT" ] || continue
  say ""; say "--- $f"
  printf '%s\n' "$HIT" >> "$R"
done

hdr "3. Cadenas de módulos/parámetros en las bibliotecas (strings)"
if have strings; then
  for f in $LIBS; do
    HIT="$(strings -a "$f" 2>/dev/null | grep -i -E "$KW_ASRC" | sort -u | head -80)"
    [ -n "$HIT" ] || continue
    say ""; say "--- $f"
    printf '%s\n' "$HIT" >> "$R"
  done
else
  say "(sin 'strings': instala binutils en Termux para obtener esta sección)"
fi

hdr "4. Configuración del grafo de audio / mixer paths / ACDB (texto)"
ETCDIRS="${IVANNA_PROBE_ETCDIRS:-/vendor/etc /odm/etc /vendor/etc/audio /vendor/etc/audio_policy /vendor/etc/acdbdata /vendor/etc/qal /vendor/etc/pal}"
for d in $ETCDIRS; do
  [ -d "$d" ] || continue
  grep -r -I -i -l -E "$KW_ASRC" "$d" 2>/dev/null | head -60
done | sort -u > "$OUT/config_files_with_hits.txt"
say "archivos con coincidencias: $(wc -l < "$OUT/config_files_with_hits.txt" 2>/dev/null) (lista en config_files_with_hits.txt)"
# Contexto de las coincidencias en los XML de grafo/mixer (acotado).
while read -r f; do
  case "$f" in *.xml|*.conf|*.txt|*.json)
    say ""; say "--- $f"
    grep -n -i -E "$KW_ASRC" "$f" 2>/dev/null | head -25 >> "$R" ;;
  esac
done < "$OUT/config_files_with_hits.txt"

hdr "5. Controles de mixer (tinymix) con nombres de reloj/ASRC/ppm"
if have tinymix; then
  tinymix 2>/dev/null | grep -i -E "$KW_ASRC|clk|clock|pll" | head -80 >> "$R"
else
  say "(sin tinymix en PATH; prueba /vendor/bin/tinymix o /system/bin/tinymix)"
  for t in /vendor/bin/tinymix /system/bin/tinymix /vendor/bin/tinymix_new; do
    [ -x "$t" ] && { say "usando $t"; "$t" 2>/dev/null | grep -i -E "$KW_ASRC|clk|clock|pll" | head -80 >> "$R"; break; }
  done
fi

hdr "6. AudioFlinger: ¿el hilo de salida expone posición de presentación?"
if have dumpsys; then
  dumpsys media.audio_flinger 2>/dev/null | grep -i -E "output thread|sample rate|frame count|latency|standby|HAL format|timestamp|presentation|underrun|write" | head -60 >> "$R"
else
  say "(sin dumpsys)"
fi

hdr "7. Resumen automático (lo que decide el siguiente paso)"
N_TS=0; N_ASRC=0
if have strings; then
  for f in $LIBS; do
    case "$f" in *libar-pal.so|*libpalclient.so|*pal@1.0-impl.so)
      c="$(strings -a "$f" 2>/dev/null | grep -c -i -E "get_timestamp|session_time")"; N_TS=$((N_TS + c)) ;; esac
  done
fi
# Cuenta SOLO evidencia: excluye encabezados, rutas de archivo y las propias frases del informe.
N_ASRC="$(grep -v -E '^(=|---|\(|LECTURA|referencias|líneas|  ->|herramientas|archivos con|usando)' "$R" 2>/dev/null \
          | grep -c -i -E 'asrc|sample.?slip|rate.?adapt|rat_render|clk.?recov|rate.?match')"
say "referencias a timestamp/session_time en PAL: $N_TS"
say "líneas del informe con pistas de ASRC/slip/rate-adapter: $N_ASRC"
if [ "$N_ASRC" -eq 0 ] 2>/dev/null; then
  say "LECTURA: no hay pista de un actuador de razón variable en lo inspeccionado."
  say "  -> Sin actuador verificado, la corrección por HAL debe seguir DESACTIVADA."
else
  say "LECTURA: hay candidatos. Aún NO son evidencia suficiente: falta confirmar módulo"
  say "  presente en el grafo de reproducción, ID de parámetro, payload y unidades."
fi

( cd "$OUT" && tar -czf "$OUT.tar.gz" . 2>/dev/null ) && echo "Informe: $R"
echo "Paquete: $OUT.tar.gz (si tar no existe, usa la carpeta $OUT)"
echo "Esta sonda no modificó nada del sistema."
