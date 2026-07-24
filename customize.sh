#!/system/bin/sh
##########################################################################################
# IVANNA OMEGA SUPREME — customize.sh v2.1 (PATCH)
# - ELF check con head -c4 (no dd|od)
# - Detecta y valida ivanna_daemon si viene incluido
# - Idempotente en fusión de audio_effects.xml
##########################################################################################

SKIPUNZIP=0
OMEGA_NAME="omega_effect"
OMEGA_UUID="8d7d5e0a-a6eb-4fde-a0ff-cb1b2dd7275e"
SO_REL_VENDOR="system/vendor/lib64/soundfx/libomega_effect.so"
SO_REL_SYSTEM="system/lib64/soundfx/libomega_effect.so"
DAEMON_REL="system/bin/ivanna_daemon"

is_elf() {
  hex=$(head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')
  [ "$hex" = "7f454c46" ]
}

find_so_path() {
  for CAND in "$MODPATH/$SO_REL_VENDOR" "$MODPATH/$SO_REL_SYSTEM"; do
    [ -f "$CAND" ] && { echo "$CAND"; return; }
  done
}

append_library_once() {
  in_file="$1"; out_file="$2"
  if grep -q "<library name=\"$OMEGA_NAME\"" "$in_file" 2>/dev/null; then
    cp "$in_file" "$out_file"
  else
    sed "s#</libraries>#    <library name=\"$OMEGA_NAME\" path=\"libomega_effect.so\"/>\\n</libraries>#" "$in_file" > "$out_file"
  fi
}

append_effect_once() {
  in_file="$1"; out_file="$2"
  if grep -q "<effect name=\"$OMEGA_NAME\"" "$in_file" 2>/dev/null; then
    cp "$in_file" "$out_file"
  else
    sed "s#</effects>#    <effect name=\"$OMEGA_NAME\" library=\"$OMEGA_NAME\" uuid=\"$OMEGA_UUID\" priority=\"1000\"/>\\n</effects>#" "$in_file" > "$out_file"
  fi
}

append_music_apply_once() {
  in_file="$1"; out_file="$2"
  if grep -q '<postprocess>' "$in_file" 2>/dev/null && grep -q '<stream type="music">' "$in_file" 2>/dev/null; then
    awk '
      BEGIN { in_music = 0; has_apply = 0 }
      /<stream type="music">/ { in_music = 1; has_apply = 0; print; next }
      in_music && /<apply effect="omega_effect"\/>/ { has_apply = 1; print; next }
      in_music && /<\/stream>/ { if (!has_apply) print "            <apply effect=\"omega_effect\"/>"; in_music = 0; print; next }
      { print }
    ' "$in_file" > "$out_file"
  else
    sed 's#</audio_effects_conf>#    <postprocess>\n        <stream type="music">\n            <apply effect="omega_effect"/>\n        </stream>\n    </postprocess>\n</audio_effects_conf>#' "$in_file" > "$out_file"
  fi
}

count_matches() { grep -c "$1" "$2" 2>/dev/null || echo 0; }

ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "   IVANNA OMEGA SUPREME v2.1"
ui_print "   Motor Anti-Dolby + Daemon RT — Magisk installer"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print " "
ui_print "- Entorno: API=$API ABI=$ABI Magisk=$MAGISK_VER_CODE"

[ "$ABI" = "arm64-v8a" ] || abort "! Solo arm64-v8a soportado (detectado: $ABI)"
[ "$API" -ge 28 ] || abort "! Requiere Android 9/API 28+ (detectado: $API)"

# ── libomega_effect.so ────────────────────────────────────────────────────
SO_PATH=$(find_so_path)
[ -n "$SO_PATH" ] || abort "! No se encontró libomega_effect.so"
SO_SIZE=$(stat -c%s "$SO_PATH" 2>/dev/null || wc -c < "$SO_PATH")
[ "$SO_SIZE" -gt 0 ] || abort "! libomega_effect.so vacío"
if is_elf "$SO_PATH"; then
  ui_print "  libomega_effect.so ELF OK ($SO_SIZE bytes)"
else
  abort "! libomega_effect.so no es ELF válido"
fi

# ── ivanna_daemon (opcional pero recomendado) ─────────────────────────────
DAEMON_PATH="$MODPATH/$DAEMON_REL"
if [ -f "$DAEMON_PATH" ]; then
  if is_elf "$DAEMON_PATH"; then
    DAEMON_SIZE=$(stat -c%s "$DAEMON_PATH" 2>/dev/null || wc -c < "$DAEMON_PATH")
    ui_print "  ivanna_daemon ELF OK ($DAEMON_SIZE bytes)"
    chmod 0755 "$DAEMON_PATH" 2>/dev/null
  else
    ui_print "! ivanna_daemon no es ELF — se elimina, seguirá modo app-only"
    rm -f "$DAEMON_PATH"
  fi
else
  ui_print "- ivanna_daemon no incluido → modo app-only (DSP en app funciona igual)"
fi

# ── Fusión audio_effects.xml ──────────────────────────────────────────────
LIVE_XML=""
for CAND in /vendor/etc/audio_effects.xml /odm/etc/audio_effects.xml /system/etc/audio_effects.xml; do
  if [ -f "$CAND" ] && grep -q "<audio_effects_conf" "$CAND" 2>/dev/null; then LIVE_XML="$CAND"; break; fi
done
BASE_XML="$MODPATH/.base_audio_effects.xml"
if [ -n "$LIVE_XML" ]; then
  ui_print "- Base XML real: $LIVE_XML"
  cp "$LIVE_XML" "$BASE_XML"
else
  SKU_GUESS="holi"
  getprop | grep -qi "dolby\|dap" && SKU_GUESS="blair"
  ui_print "- XML real no legible; fallback sku_${SKU_GUESS}"
  cp "$MODPATH/vendor_base/sku_${SKU_GUESS}_audio_effects.xml" "$BASE_XML" 2>/dev/null || \
    cp "$MODPATH/vendor_base/sku_holi_audio_effects.xml" "$BASE_XML" 2>/dev/null || \
    abort "! No hay XML base válido para fusionar"
fi
cp "$BASE_XML" "$MODPATH/audio_effects.xml.orig"

MERGED_XML="$MODPATH/.merged_audio_effects.xml"
append_library_once "$BASE_XML" "${MERGED_XML}.1"
append_effect_once  "${MERGED_XML}.1" "${MERGED_XML}.2"
append_music_apply_once "${MERGED_XML}.2" "${MERGED_XML}.3"

LIB_COUNT=$(count_matches '<library name="omega_effect"' "${MERGED_XML}.3")
EFFECT_COUNT=$(count_matches '<effect name="omega_effect"' "${MERGED_XML}.3")
APPLY_COUNT=$(count_matches '<apply effect="omega_effect"/>' "${MERGED_XML}.3")
grep -q '<audio_effects_conf' "${MERGED_XML}.3" 2>/dev/null || abort "! XML fusionado inválido"
[ "$LIB_COUNT" -eq 1 ] && [ "$EFFECT_COUNT" -eq 1 ] && [ "$APPLY_COUNT" -ge 1 ] || \
  abort "! Fusión insegura (lib=$LIB_COUNT effect=$EFFECT_COUNT apply=$APPLY_COUNT)"

mkdir -p "$MODPATH/system/vendor/etc" "$MODPATH/system/etc"
cp "${MERGED_XML}.3" "$MODPATH/system/vendor/etc/audio_effects.xml"
cp "${MERGED_XML}.3" "$MODPATH/system/etc/audio_effects.xml"
rm -f "${MERGED_XML}.1" "${MERGED_XML}.2" "${MERGED_XML}.3" "$BASE_XML"

if [ -f "$MODPATH/.safe_mode" ]; then
  rm -f "$MODPATH/system/vendor/etc/audio_effects.xml" "$MODPATH/system/etc/audio_effects.xml"
  ui_print "- MODO SEGURO: audio_effects.xml no montado (proteger audioserver)"
fi

# ── Permisos ──────────────────────────────────────────────────────────────
set_perm_recursive "$MODPATH/system" 0 0 0755 0644
set_perm "$SO_PATH" 0 0 0644
[ -f "$DAEMON_PATH" ] && set_perm "$DAEMON_PATH" 0 0 0755
[ -f "$MODPATH/system/vendor/etc/audio_effects.xml" ] && set_perm "$MODPATH/system/vendor/etc/audio_effects.xml" 0 0 0644
[ -f "$MODPATH/system/etc/audio_effects.xml" ] && set_perm "$MODPATH/system/etc/audio_effects.xml" 0 0 0644
command -v chcon >/dev/null 2>&1 && chcon u:object_r:vendor_file:s0 "$SO_PATH" 2>/dev/null
[ -f "$DAEMON_PATH" ] && command -v chcon >/dev/null 2>&1 && chcon u:object_r:system_file:s0 "$DAEMON_PATH" 2>/dev/null

# ── Scripts del módulo con permisos de ejecución ──────────────────────────
for s in service.sh post-fs-data.sh uninstall.sh customize.sh \
         ivanna_control.sh mqa_monitor.sh concert_mode.sh; do
  [ -f "$MODPATH/$s" ] && set_perm "$MODPATH/$s" 0 0 0755
done

ui_print " "
ui_print "✓ Instalación validada v2.1. Reinicia el dispositivo."
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print " "
