##########################################################################################
# IVANNA OMEGA SUPREME — customize.sh
# Magisk module install logic
#
# Autor: Luis Uriel Pimentel Pérez / IVANNA Team
# v1.8: FIX CRÍTICO — ya NO se reemplaza /vendor/etc/audio_effects.xml por un XML
#       mínimo (eso borraba bassboost, equalizer, virtualizer, reverb, Dolby, AEC/NS
#       de llamadas, y los helpers de ring/notification/voice del OEM). Ahora se
#       FUSIONA omega_effect dentro del XML real del dispositivo, preservando todo
#       lo demás. Se usa el XML vivo del propio equipo como base; si no se puede
#       leer, se cae a una copia de referencia (blair=con Dolby / holi=sin Dolby)
#       empaquetada en el módulo.
##########################################################################################

SKIPUNZIP=0   # dejamos que Magisk extraiga a $MODPATH automáticamente

# ── 1. Comprobaciones de entorno ─────────────────────────────────────────────────────────
ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "   IVANNA OMEGA SUPREME v1.8"
ui_print "   © 2026 Luis Uriel Pimentel Pérez"
ui_print "   Motor de audio neuromórfico (Anti-Dolby)"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print " "
ui_print "- Comprobando entorno..."
ui_print "  Magisk versionCode : $MAGISK_VER_CODE"
ui_print "  API                : $API"
ui_print "  ABI                : $ABI"

if [ "$ABI" != "arm64-v8a" ]; then
  abort "! Este módulo solo soporta arm64-v8a (tu dispositivo: $ABI)"
fi

if [ "$API" -lt 28 ]; then
  abort "! Requiere Android 9 (API 28) o superior. Detectado API $API"
fi

# ── 2. Verificación de la librería nativa ────────────────────────────────────────────────
SO_REL="system/vendor/lib64/soundfx/libomega_effect.so"
SO_PATH="$MODPATH/$SO_REL"

ui_print "- Verificando libomega_effect.so..."
if [ ! -f "$SO_PATH" ]; then
  abort "! No se encontró $SO_REL dentro del ZIP. Módulo corrupto."
fi

SO_SIZE=$(stat -c%s "$SO_PATH" 2>/dev/null || wc -c < "$SO_PATH")
ui_print "  Tamaño: ${SO_SIZE} bytes"

# ELF magic check (0x7F 'E' 'L' 'F')
MAGIC=$(dd if="$SO_PATH" bs=1 count=4 2>/dev/null | od -An -c | tr -d ' \n')
case "$MAGIC" in
  *ELF*) ui_print "  ELF header OK" ;;
  *)     abort "! libomega_effect.so no es un ELF válido" ;;
esac

# Símbolo obligatorio para el framework de audio de Android
if command -v strings >/dev/null 2>&1; then
  if strings "$SO_PATH" | grep -q "AUDIO_EFFECT_LIBRARY_INFO_SYM"; then
    ui_print "  Símbolo AUDIO_EFFECT_LIBRARY_INFO_SYM presente"
  else
    ui_print "! ADVERTENCIA: no se encuentra AUDIO_EFFECT_LIBRARY_INFO_SYM."
    ui_print "  El framework de audio NO cargará el efecto como postprocess."
    ui_print "  Se activará el MODO SEGURO (solo JNI, sin registro global)."
    touch "$MODPATH/.safe_mode"
  fi
fi

# ── 3. Fusión de omega_effect dentro del audio_effects.xml REAL del vendor ───────────────
# Android solo lee UN archivo (/vendor/etc/audio_effects.xml, con fallback a /odm/ o
# /system/etc/ según el OEM). En vez de sustituirlo por un XML mínimo (lo que rompía
# bassboost/equalizer/Dolby/AEC/NS de llamadas), lo copiamos tal cual y le INSERTAMOS
# nuestra librería + efecto + <apply> dentro del stream "music", dejando todo lo demás
# (voice_call, ring, alarm, notification, system) intacto.
ui_print "- Localizando audio_effects.xml real del dispositivo..."

LIVE_XML=""
for CAND in /vendor/etc/audio_effects.xml /odm/etc/audio_effects.xml /system/etc/audio_effects.xml; do
  if [ -f "$CAND" ] && grep -q "<audio_effects_conf" "$CAND" 2>/dev/null; then
    LIVE_XML="$CAND"
    break
  fi
done

BASE_XML="$MODPATH/.base_audio_effects.xml"

if [ -n "$LIVE_XML" ]; then
  ui_print "  Encontrado: $LIVE_XML (usando como base real)"
  cp "$LIVE_XML" "$BASE_XML"
else
  ui_print "! No se pudo leer el XML real del vendor. Usando referencia empaquetada..."
  SKU_GUESS="blair"
  if getprop | grep -qi "dolby\|dap"; then
    SKU_GUESS="blair"
  else
    SKU_GUESS="holi"
  fi
  ui_print "  SKU estimado: $SKU_GUESS"
  cp "$MODPATH/vendor_base/sku_${SKU_GUESS}_audio_effects.xml" "$BASE_XML" 2>/dev/null \
    || cp "$MODPATH/vendor_base/sku_blair_audio_effects.xml" "$BASE_XML"
fi

# Guardamos copia intacta del original para soporte/rollback
cp "$BASE_XML" "$MODPATH/audio_effects.xml.orig"

MERGED_XML="$MODPATH/.merged_audio_effects.xml"

# 3a. Insertar <library> antes de </libraries>
sed 's#</libraries>#    <library name="omega_effect" path="libomega_effect.so"/>\n</libraries>#' \
  "$BASE_XML" > "${MERGED_XML}.1"

# 3b. Insertar <effect> antes de </effects>
sed 's#</effects>#    <effect name="omega_effect" library="omega_effect" uuid="8d7d5e0a-a6eb-4fde-a0ff-cb1b2dd7275e" priority="1000"/>\n</effects>#' \
  "${MERGED_XML}.1" > "${MERGED_XML}.2"

# 3c. Aplicar omega_effect al stream "music" (cubre música, video/juegos vía STREAM_MUSIC).
#     NO se toca voice_call para no interferir con llamadas.
if grep -q '<postprocess>' "${MERGED_XML}.2" && grep -q '<stream type="music">' "${MERGED_XML}.2"; then
  awk '{
    print $0
    if ($0 ~ /<stream type="music">/) { print "            <apply effect=\"omega_effect\"/>" }
  }' "${MERGED_XML}.2" > "${MERGED_XML}.3"
else
  ui_print "  No había <postprocess>/music en el XML base; se añade bloque nuevo"
  sed 's#</audio_effects_conf>#    <postprocess>\n        <stream type="music">\n            <apply effect="omega_effect"/>\n        </stream>\n    </postprocess>\n</audio_effects_conf>#' \
    "${MERGED_XML}.2" > "${MERGED_XML}.3"
fi

mkdir -p "$MODPATH/system/vendor/etc"
mkdir -p "$MODPATH/system/etc"
cp "${MERGED_XML}.3" "$MODPATH/system/vendor/etc/audio_effects.xml"
cp "${MERGED_XML}.3" "$MODPATH/system/etc/audio_effects.xml"
rm -f "${MERGED_XML}.1" "${MERGED_XML}.2" "${MERGED_XML}.3" "$BASE_XML"

ui_print "  audio_effects.xml fusionado (bassboost/equalizer/Dolby/AEC/NS preservados)"
ui_print "  omega_effect aplicado a: music (backup del original en audio_effects.xml.orig)"

# ── 4. Modo seguro: si el .so no expone la interfaz, no registramos el overlay XML ───────
if [ -f "$MODPATH/.safe_mode" ]; then
  ui_print "- MODO SEGURO activo: se retira el overlay XML para evitar crash de audioserver"
  rm -f "$MODPATH/system/vendor/etc/audio_effects.xml" \
        "$MODPATH/system/etc/audio_effects.xml"
  ui_print "  La .so queda instalada solo como librería JNI (accesible desde la app IVANNA)"
fi

# ── 5. Permisos ──────────────────────────────────────────────────────────────────────────
ui_print "- Ajustando permisos y propietarios..."
set_perm_recursive "$MODPATH/system"                      0 0 0755 0644
set_perm           "$SO_PATH"                             0 0 0644
[ -f "$MODPATH/system/vendor/etc/audio_effects.xml" ] && \
  set_perm         "$MODPATH/system/vendor/etc/audio_effects.xml" 0 0 0644
[ -f "$MODPATH/system/etc/audio_effects.xml" ] && \
  set_perm         "$MODPATH/system/etc/audio_effects.xml"        0 0 0644

# ── 6. Contexto SELinux para audioserver ─────────────────────────────────────────────────
if command -v chcon >/dev/null 2>&1; then
  chcon u:object_r:vendor_file:s0 "$SO_PATH" 2>/dev/null && \
    ui_print "  SELinux: $SO_REL → u:object_r:vendor_file:s0"
fi

# ── 7. Resumen final ─────────────────────────────────────────────────────────────────────
ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ -f "$MODPATH/.safe_mode" ]; then
  ui_print "  Instalación completada en MODO SEGURO."
  ui_print "  → La .so no exporta la fábrica AudioEffect."
  ui_print "  → Recompila omega_effect.cpp añadiendo la"
  ui_print "    estructura audio_effect_library_t con"
  ui_print "    create_effect/release_effect/get_descriptor"
  ui_print "    para activar el efecto global."
else
  ui_print "  Instalación completada correctamente."
  ui_print "  Reinicia para activar el efecto global."
  ui_print "  omega_effect se fusionó en tu audio_effects.xml"
  ui_print "  real — bassboost/equalizer/Dolby/AEC de llamadas"
  ui_print "  siguen funcionando como antes."
fi
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print " "
