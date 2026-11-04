#!/system/bin/sh
# Read-only evidence collector for IVANNA Moto G85 early-init PAL preload work.
# It does not flash, patch, mount, restart, or change device settings.
set -eu

fail() { echo "ERROR: $*" >&2; exit 1; }

[ "$(id -u)" = "0" ] || fail "Run as root (for example: su -c 'sh collect_init_boot_evidence.sh')."

SLOT="$(getprop ro.boot.slot_suffix 2>/dev/null || true)"
case "$SLOT" in
  _a|_b) ;;
  *) fail "Unexpected or empty ro.boot.slot_suffix: '$SLOT'. No partition was read." ;;
esac

PART="/dev/block/by-name/init_boot${SLOT}"
[ -e "$PART" ] || fail "Active init_boot partition not found: $PART"
[ -b "$PART" ] || fail "Resolved init_boot path is not a block device: $PART"

VENDOR_RC="/vendor/etc/init/android.hardware.audio.service_64.rc"
[ -r "$VENDOR_RC" ] || fail "Cannot read expected vendor audio service rc: $VENDOR_RC"

OUT="${1:-/sdcard/Download/ivanna-pal-preload-evidence}"
mkdir -p "$OUT"
[ -d "$OUT" ] || fail "Cannot create output directory: $OUT"

IMG="$OUT/init_boot${SLOT}.img"
META="$OUT/device_metadata.txt"
RC="$OUT/android.hardware.audio.service_64.rc"

echo "Reading active init_boot partition (read-only): $PART"
dd if="$PART" of="$IMG" bs=1M
[ -s "$IMG" ] || fail "The init_boot image copy is empty."

cp "$VENDOR_RC" "$RC"

{
  echo "collection_time=$(date '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null || true)"
  echo "device=$(getprop ro.product.device)"
  echo "model=$(getprop ro.product.model)"
  echo "android_release=$(getprop ro.build.version.release)"
  echo "sdk=$(getprop ro.build.version.sdk)"
  echo "build_fingerprint=$(getprop ro.build.fingerprint)"
  echo "slot_suffix=$SLOT"
  echo "init_boot_partition=$PART"
  echo "init_boot_realpath=$(readlink -f "$PART" 2>/dev/null || echo unavailable)"
  echo "init_boot_size_bytes=$(wc -c < "$IMG" | tr -d ' ')"
  echo "init_boot_sha256=$(sha256sum "$IMG" | awk '{print $1}')"
  echo "magisk_version=$(magisk -V 2>/dev/null || echo unavailable)"
  echo "magisk_version_string=$(magisk -v 2>/dev/null || echo unavailable)"
  echo "magisk_path=$(magisk --path 2>/dev/null || echo unavailable)"
  echo "verified_boot_state=$(getprop ro.boot.verifiedbootstate)"
  echo "flash_locked=$(getprop ro.boot.flash.locked)"
  echo "selinux=$(getenforce 2>/dev/null || echo unavailable)"
  echo
  echo "[partition]"
  ls -l "$PART"
  echo
  echo "[audio service rc sha256]"
  sha256sum "$RC"
  echo
  echo "[relevant module directories]"
  for d in /data/adb/modules/ivanna_omega_supreme /data/adb/modules/omega_pal_presence_probe /data/adb/modules/omega_hal_probe; do
    if [ -d "$d" ]; then
      echo "present=$d"
      [ ! -f "$d/module.prop" ] || sed -n '1,12p' "$d/module.prop"
    else
      echo "absent=$d"
    fi
  done
} > "$META"

(
  cd "$OUT"
  if command -v zip >/dev/null 2>&1; then
    zip -q -9 -r /sdcard/Download/ivanna-pal-preload-evidence.zip init_boot${SLOT}.img device_metadata.txt android.hardware.audio.service_64.rc
  elif command -v tar >/dev/null 2>&1; then
    tar -czf /sdcard/Download/ivanna-pal-preload-evidence.tar.gz init_boot${SLOT}.img device_metadata.txt android.hardware.audio.service_64.rc
  else
    fail "Neither zip nor tar is available; files remain in $OUT"
  fi
)

echo "Evidence collected. No partition was modified."
echo "Output directory: $OUT"
echo "Archive: /sdcard/Download/ivanna-pal-preload-evidence.zip (or .tar.gz)"
