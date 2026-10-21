#!/system/bin/sh
# Read-only, targeted extraction for the Moto G85 / Blair libar-pal.so.
# This script never writes to the device or changes mixer/DSP state.
# Usage:
#   sh extract_moto_g85_pal_timestamp.sh /vendor/lib64/libar-pal.so [output-dir]
# Needs llvm-objdump and preferably llvm-readelf/llvm-strings (Termux/NDK tools).
set -u

SO="${1:-/vendor/lib64/libar-pal.so}"
OUT="${2:-/sdcard/Download/ivanna_pal_timestamp_extract_$(date +%Y%m%d_%H%M%S 2>/dev/null || echo run)}"
mkdir -p "$OUT" 2>/dev/null || { echo "Cannot create output directory: $OUT"; exit 1; }
[ -r "$SO" ] || { echo "Cannot read library: $SO"; exit 1; }

OBJDUMP="$(command -v llvm-objdump 2>/dev/null || command -v objdump 2>/dev/null || true)"
READELF="$(command -v llvm-readelf 2>/dev/null || command -v readelf 2>/dev/null || true)"
STRINGS="$(command -v llvm-strings 2>/dev/null || command -v strings 2>/dev/null || true)"
[ -n "$OBJDUMP" ] || { echo "Missing llvm-objdump/objdump. Install Android NDK LLVM tools."; exit 1; }

{
  echo "library=$SO"
  sha256sum "$SO" 2>/dev/null || toybox sha256sum "$SO" 2>/dev/null || true
  echo "objdump=$OBJDUMP"
  echo "readelf=${READELF:-unavailable}"
  echo "strings=${STRINGS:-unavailable}"
} > "$OUT/identity.txt"

# Important: --start-address/--stop-address are ELF virtual addresses and must
# overlap .text. Earlier attempts used .rodata addresses with these flags.
"$OBJDUMP" -d --start-address=0x256850 --stop-address=0x256e20 "$SO" \
  > "$OUT/timestamp_callsite_disassembly.txt" 2>&1

if [ -n "$READELF" ]; then
  "$READELF" -W -S "$SO" > "$OUT/elf_sections.txt" 2>&1
  "$READELF" -W -l "$SO" > "$OUT/elf_segments.txt" 2>&1
  "$READELF" -W -s "$SO" > "$OUT/elf_symbols.txt" 2>&1
fi

# Dump the whole .rodata section with its own section-relative addresses.
# Do NOT pass .rodata virtual addresses to --start-address as if they were
# .text ranges. This output allows ADRP/ADD references to be mapped correctly.
"$OBJDUMP" -s -j .rodata "$SO" > "$OUT/rodata_dump.txt" 2>&1

if [ -n "$STRINGS" ]; then
  "$STRINGS" -a -t x "$SO" > "$OUT/strings_with_file_offsets.txt" 2>&1
  grep -i -E 'timestamp|session_time|absolute_time|mixer|pal|clock|rate|frame|payload' \
    "$OUT/strings_with_file_offsets.txt" > "$OUT/relevant_strings.txt" 2>/dev/null || true
fi

{
  echo "=== Call-site window (0x256850–0x256e20) ==="
  grep -E '2568[0-9a-f]{2}:|2569[0-9a-f]{2}:|256a[0-9a-f]{2}:|256b[0-9a-f]{2}:|256c[0-9a-f]{2}:|256d[0-9a-f]{2}:|256e0[0-9a-f]:' \
    "$OUT/timestamp_callsite_disassembly.txt" 2>/dev/null || true
  echo
  echo "=== Relevant strings (file offsets, not virtual addresses) ==="
  cat "$OUT/relevant_strings.txt" 2>/dev/null || true
  echo
  echo "=== Section mapping ==="
  grep -E '\.text|\.rodata|LOAD' "$OUT/elf_sections.txt" "$OUT/elf_segments.txt" 2>/dev/null || true
} > "$OUT/summary.txt"

( cd "$OUT" && tar -czf "$OUT.tar.gz" . 2>/dev/null ) || true
echo "Read-only extraction complete: $OUT"
echo "Upload $OUT.tar.gz (or summary.txt + timestamp_callsite_disassembly.txt + rodata_dump.txt)."
