#!/system/bin/sh
# SAFMUS deep audio-effect audit: read-only diagnostics; never edits vendor XML,
# changes SELinux, restarts/kills OEM services, or claims activation without evidence.
# Installed at <module-root>/core/safmus_audio_effect_audit.sh.
umask 077
SCRIPT_DIR=${0%/*}
MODDIR=${SCRIPT_DIR%/*}
ROOT=/data/adb/ivanna_omega
OUT=$ROOT/safmus
UUID=4956414e-4e41-4f4d-4547-415355505245
REPORT=$OUT/audio_effect_registration.env
TMP=$OUT/audio_effect_registration.env.tmp.$$
CONFIG= LIB= XML_VALID=unknown XML_MATCH_COUNT=0
mkdir -p "$OUT" 2>/dev/null || exit 0
trap 'rm -f "$TMP"' EXIT HUP INT TERM

# Prefer module overlays, but keep runtime candidates distinct in the report.
for f in \
  "$MODDIR/system/vendor/etc/audio_effects.xml" \
  "$MODDIR/system/etc/audio_effects_ivanna_omega.xml" \
  "$MODDIR/system/etc/audio_effects.xml" \
  /vendor/etc/audio_effects.xml \
  /vendor/etc/audio_effects*.xml \
  /system/etc/audio_effects_ivanna_omega.xml \
  /system/etc/audio_effects.xml \
  /odm/etc/audio_effects.xml \
  /product/etc/audio_effects.xml; do
  [ -f "$f" ] && [ -s "$f" ] || continue
  if grep -q "$UUID" "$f" 2>/dev/null; then CONFIG=$f; break; fi
done

# Count UUID occurrences and validate XML when a parser is available.
if [ -n "$CONFIG" ]; then
  XML_MATCH_COUNT=$(grep -o "$UUID" "$CONFIG" 2>/dev/null | wc -l | tr -d ' ')
  if command -v xmllint >/dev/null 2>&1; then
    xmllint --noout "$CONFIG" >/dev/null 2>&1 && XML_VALID=yes || XML_VALID=no
  elif grep -qi '<effects\|<effect\|<libraries' "$CONFIG" 2>/dev/null &&
       grep -qi '</effects\|</effect\|</libraries' "$CONFIG" 2>/dev/null; then
    XML_VALID=structure_only
  else
    XML_VALID=unknown
  fi
fi

# Search known overlay/runtime locations. Do not treat mere file presence as load success.
for f in \
  "$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so" \
  "$MODDIR/system/lib64/soundfx/libomega_effect.so" \
  "$MODDIR/system/vendor/lib/soundfx/libomega_effect.so" \
  "$MODDIR/system/lib/soundfx/libomega_effect.so" \
  /vendor/lib64/soundfx/libomega_effect.so \
  /vendor/lib/soundfx/libomega_effect.so \
  /vendor/lib64/libomega_effect.so \
  /system/vendor/lib64/soundfx/libomega_effect.so \
  /system/lib64/soundfx/libomega_effect.so \
  /odm/lib64/soundfx/libomega_effect.so \
  /product/lib64/soundfx/libomega_effect.so; do
  [ -f "$f" ] && [ -s "$f" ] && { LIB=$f; break; }
done

ELF_OK=0 ELF_CLASS=unknown ELF_MACHINE=unknown LIB_SHA256=unavailable
if [ -n "$LIB" ]; then
  MAGIC=$(head -c 4 "$LIB" 2>/dev/null | od -An -tx1 | tr -d ' \n')
  [ "$MAGIC" = 7f454c46 ] && ELF_OK=1
  if command -v readelf >/dev/null 2>&1; then
    ELF_CLASS=$(readelf -h "$LIB" 2>/dev/null | sed -n 's/^[[:space:]]*Class:[[:space:]]*//p' | head -n 1)
    ELF_MACHINE=$(readelf -h "$LIB" 2>/dev/null | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p' | head -n 1)
    [ -n "$ELF_CLASS" ] || ELF_CLASS=unreadable
    [ -n "$ELF_MACHINE" ] || ELF_MACHINE=unreadable
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    LIB_SHA256=$(sha256sum "$LIB" 2>/dev/null | awk '{print $1}')
    [ -n "$LIB_SHA256" ] || LIB_SHA256=unavailable
  fi
fi

CONFIG_OK=0; [ -n "$CONFIG" ] && CONFIG_OK=1
LIB_PRESENT=0; [ -n "$LIB" ] && LIB_PRESENT=1
PREREQ=0
[ "$CONFIG_OK" = 1 ] && [ "$ELF_OK" = 1 ] && [ "$XML_VALID" != no ] && PREREQ=1

case "$CONFIG" in "$MODDIR"/*) CONFIG_SOURCE=module_overlay ;; /vendor/*|/system/*|/odm/*|/product/*) CONFIG_SOURCE=runtime_path ;; *) CONFIG_SOURCE=missing ;; esac
case "$LIB" in "$MODDIR"/*) LIB_SOURCE=module_overlay ;; /vendor/*|/system/*|/odm/*|/product/*) LIB_SOURCE=runtime_path ;; *) LIB_SOURCE=missing ;; esac

# AudioFlinger evidence is opportunistic: dumpsys output varies by Android/OEM.
AF_EVIDENCE=unavailable AF_MATCH=0 AF_CAPTURE=none
if command -v dumpsys >/dev/null 2>&1; then
  AF_TMP=$OUT/audio_flinger_dump.tmp.$$
  if dumpsys media.audio_flinger > "$AF_TMP" 2>/dev/null; then
    AF_EVIDENCE=queried
    if grep -qi 'libomega_effect\|4956414e-4e41-4f4d-4547-415355505245' "$AF_TMP" 2>/dev/null; then
      AF_MATCH=1
      AF_CAPTURE=$(grep -i -m 1 'libomega_effect\|4956414e-4e41-4f4d-4547-415355505245' "$AF_TMP" 2>/dev/null | tr '\r\n' '  ' | cut -c1-240)
    fi
  else
    AF_EVIDENCE=query_failed
  fi
  rm -f "$AF_TMP"
fi
AF_VERIFIED=unverified
[ "$AF_MATCH" = 1 ] && AF_VERIFIED=observed_in_dumpsys_not_proof_of_active_processing

REASON=prerequisites_missing
if [ "$PREREQ" = 1 ]; then REASON=prerequisites_present_activation_unverified; fi
[ "$XML_VALID" = no ] && REASON=matching_xml_failed_well_formedness_check
[ "$AF_MATCH" = 1 ] && REASON=dumpsys_mentions_effect_runtime_processing_still_unverified
[ "$CONFIG_OK" != 1 ] && REASON=matching_effect_xml_not_found
[ "$LIB_PRESENT" != 1 ] && REASON=native_effect_library_missing

{
  printf '%s\n' 'schema=3'
  printf 'timestamp=%s\n' "$(date +%s)"
  printf 'expected_uuid=%s\n' "$UUID"
  printf 'module_root=%s\n' "$MODDIR"
  printf 'config_path=%s\n' "${CONFIG:-missing}"
  printf 'config_source=%s\n' "$CONFIG_SOURCE"
  printf 'config_uuid_match=%s\n' "$CONFIG_OK"
  printf 'config_uuid_occurrences=%s\n' "${XML_MATCH_COUNT:-0}"
  printf 'config_xml_validation=%s\n' "$XML_VALID"
  printf 'library_path=%s\n' "${LIB:-missing}"
  printf 'library_source=%s\n' "$LIB_SOURCE"
  printf 'library_present=%s\n' "$LIB_PRESENT"
  printf 'library_elf_header_valid=%s\n' "$ELF_OK"
  printf 'library_elf_class=%s\n' "$ELF_CLASS"
  printf 'library_elf_machine=%s\n' "$ELF_MACHINE"
  printf 'library_sha256=%s\n' "$LIB_SHA256"
  printf 'registration_prerequisites_ready=%s\n' "$PREREQ"
  printf 'audioflinger_dump_status=%s\n' "$AF_EVIDENCE"
  printf 'audioflinger_effect_mention=%s\n' "$AF_MATCH"
  printf 'audioflinger_effect_evidence=%s\n' "${AF_CAPTURE:-none}"
  printf 'audioflinger_activation_verified=%s\n' "$AF_VERIFIED"
  printf '%s\n' 'system_wide_audio_coverage=unverified'
  printf 'reason=%s\n' "$REASON"
} > "$TMP" || exit 0

chmod 600 "$TMP" 2>/dev/null
mv -f "$TMP" "$REPORT" 2>/dev/null || exit 0
printf '%s SAFMUS deep audio audit: config=%s xml=%s lib=%s elf=%s prereq=%s af=%s (coverage unverified)\n' \
  "$(date)" "$CONFIG_OK" "$XML_VALID" "$LIB_PRESENT" "$ELF_OK" "$PREREQ" "$AF_MATCH" >> "$ROOT/daemon.log" 2>/dev/null
exit 0
