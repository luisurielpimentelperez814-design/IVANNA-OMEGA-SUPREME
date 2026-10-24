#!/system/bin/sh
# SAFMUS/Magisk audio-effect audit. Diagnostics only: never edits vendor XML,
# changes SELinux, or kills OEM audio services.
MODDIR="${0%/*}"; ROOT="/data/adb/ivanna_omega"; OUT="$ROOT/safmus"
mkdir -p "$OUT" 2>/dev/null || exit 0
REPORT="$OUT/audio_effect_registration.env"; TMP="$REPORT.tmp.$$"
UUID="4956414e-4e41-4f4d-4547-415355505245"
CONFIG=""; LIB=""
for f in "$MODDIR/system/vendor/etc/audio_effects.xml" "$MODDIR/system/etc/audio_effects_ivanna_omega.xml" "$MODDIR/system/etc/audio_effects.xml"; do
  if [ -s "$f" ] && grep -q "$UUID" "$f" 2>/dev/null; then CONFIG="$f"; break; fi
done
for f in "$MODDIR/system/vendor/lib64/soundfx/libomega_effect.so" "$MODDIR/system/lib64/soundfx/libomega_effect.so" "$MODDIR/system/vendor/lib/soundfx/libomega_effect.so" "$MODDIR/system/lib/soundfx/libomega_effect.so"; do
  [ -s "$f" ] && LIB="$f" && break
done
elf_ok=0
if [ -n "$LIB" ]; then
  MAGIC=$(head -c4 "$LIB" 2>/dev/null | od -An -tx1 | tr -d ' \n')
  [ "$MAGIC" = "7f454c46" ] && elf_ok=1
fi
config_ok=0; [ -n "$CONFIG" ] && config_ok=1
library_present=0; [ -n "$LIB" ] && library_present=1
registration_ready=0
[ "$config_ok" = 1 ] && [ "$elf_ok" = 1 ] && registration_ready=1
{
 echo "schema=1"
 echo "timestamp=$(date +%s)"
 echo "expected_uuid=$UUID"
 echo "config_path=${CONFIG:-missing}"
 echo "config_uuid_match=$config_ok"
 echo "library_path=${LIB:-missing}"
 echo "library_present=$library_present"
 echo "library_elf_valid=$elf_ok"
 echo "registration_prerequisites_ready=$registration_ready"
 echo "system_wide_audio_coverage=unverified"
 echo "reason=$(if [ "$registration_ready" = 1 ]; then echo prerequisites_present_not_proof_of_audioflinger_activation; elif [ "$config_ok" != 1 ]; then echo matching_effect_xml_not_found; else echo native_effect_library_missing_or_invalid; fi)"
} > "$TMP"
chmod 600 "$TMP" 2>/dev/null
mv -f "$TMP" "$REPORT" 2>/dev/null || rm -f "$TMP"
echo "$(date) SAFMUS audio audit: config=$config_ok library=$library_present elf=$elf_ok ready=$registration_ready (no claim of system-wide processing)" >> "$ROOT/daemon.log" 2>/dev/null
exit 0
