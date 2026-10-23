#!/data/data/com.termux/files/usr/bin/bash
# Read-only diagnostics for IVANNA on Android/Termux. No root, writes, or setting changes.
set -u

PACKAGE="${1:-}"
have() { command -v "$1" >/dev/null 2>&1; }
section() { printf '\n=== %s ===\n' "$1"; }

section "Environment"
printf 'date: '; date -Is 2>/dev/null || date
printf 'device: '; getprop ro.product.manufacturer 2>/dev/null; getprop ro.product.model 2>/dev/null
printf 'android: '; getprop ro.build.version.release 2>/dev/null
printf 'sdk: '; getprop ro.build.version.sdk 2>/dev/null
printf 'abi: '; getprop ro.product.cpu.abi 2>/dev/null

section "Package discovery"
if [ -n "$PACKAGE" ]; then
  if have pm; then pm path "$PACKAGE" 2>&1 || true; pm dump "$PACKAGE" 2>&1 | grep -E 'versionName=|versionCode=|targetSdk=|firstInstallTime=|lastUpdateTime=' || true; fi
else
  if have pm; then
    pm list packages 2>/dev/null | grep -i 'ivanna\|omega' || true
  else
    echo "pm is unavailable in this Termux context; run through adb shell or pass the package from an authorized ADB session."
  fi
fi

section "Audio outputs / policy (read-only)"
if have dumpsys; then
  dumpsys audio 2>&1 | grep -Ei 'Bluetooth|USB|AUDIO_DEVICE_OUT|speaker|sample rate|sampling|AudioTrack|device' | head -100 || true
else
  echo "dumpsys unavailable in this Termux context."
fi

section "Recent IVANNA native/app logs"
if have logcat; then
  logcat -d -t 1500 2>/dev/null | grep -Ei 'IvannaOmegaEffect|IvannaBridgePlayer|DSPBridge|RirConvolver|RirDataset|HRTF|SOFA|SafetyLimiter|AudioTrackBufferPll|StereoAudioResampler|FotoHolo' | tail -160 || true
else
  echo "logcat unavailable. Use adb logcat from an authorized host/ADB session."
fi

section "Interpretation"
echo "This report is read-only. Package presence and system audio routes do not prove all six DSP stages processed samples."
echo "For stage proof, require explicit per-stage counters/logs or instrumented DSP tests, then compare bypass/active output on-device."
