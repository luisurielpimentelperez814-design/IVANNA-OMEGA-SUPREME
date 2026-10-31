# Hardware Sovereignty 01–04 — evidence ledger

Base audited: `main` at `216be2cfe750cffbf0b0e370c12f150cedd240d2` (2026-10-31).

This ledger distinguishes source presence, automated host/build evidence, and
device/runtime evidence. A successful compile alone never closes a phase.

## HWS-01 — HAL/PAL Sovereignty

**Status: IN PROGRESS — ELF export gate added; runtime loading not yet proven.**

Observed before this change:
- `omega_effect.cpp` defines `AELI` as an `audio_effect_library_t` object
  with C linkage and explicit default visibility.
- `CMakeLists.txt` passes `-Wl,--export-dynamic-symbol=AELI` to
  `omega_effect`.
- Existing tests cover the AOSP effect-library tag, ABI layout, SHM lifecycle,
  and control-plane snapshots.

Added in HWS-01:
- `scripts/check_aeli_elf.py` inspects the built effect's **.dynsym** and
  requires exactly one defined `AELI` entry with OBJECT/GLOBAL/DEFAULT.
- The Android build workflow runs this gate against the staged
  `magisk_module/system/vendor/lib64/soundfx/libomega_effect.so`.

Acceptance still required:
- CI run on this change must pass the ELF gate.
- Moto G85 / Android 15 device probe must confirm the actual loader resolves
  the library and the configured UUID creates/releases an instance.
- IPC descriptor transfer, SHM versioning, peer validation, and applied
  generation must pass their existing tests and device checks.

Known limits:
- Host ELF inspection proves the dynamic-symbol contract only. It does not
  prove Android EffectsFactory policy, XML registration, SELinux access,
  audioserver load, or runtime callback execution.

## HWS-02 — Clock Master

**Status: NOT APPROVED.** Existing HAL clock producer, PAL adapters, hardware
clock estimator, PLL, and variable ASRC are present. Corrections must remain
fail-closed until a stream-owner timestamp/frame-position source is verified.
No device-clock or Bluetooth/PCM timing capability is claimed here.

## HWS-03 — Adaptive SoC Control

**Status: PARTIAL IMPLEMENTATION — NOT APPROVED.** Existing thermal governor
and adaptive engine are present. This change removes the fabricated 35 °C
fallback: missing/unreadable sensors now publish NaN/unavailable and retain
the conservative PROTECTED tier. The governor thread is started from
EFFECT_CMD_SET_CONFIG, not lazily from omega_process, so singleton access in
the audio callback no longer creates a thread. Targeted host tests, thermal
sensor-path validation on Moto G85, callback-budget measurements, and
long-duration throttling tests remain required. No kernel frequency, voltage,
or policy controls are assumed.

## HWS-04 — Neural Low-Level Engine

**Status: NOT APPROVED.** Pi-LSTM and TinyML/classifier code are present.
The separate JNI Pi-LSTM instance requires ownership/consumer reconciliation.
INT8 inference, deterministic execution, NEON equivalence, preallocation, and
audio-thread isolation require measured functional evidence before approval.

## Release policy

Do not merge or publish a production release while any required gate is red,
unknown, or unverified. Preserve OEM/Dolby paths and transparent bypass.
