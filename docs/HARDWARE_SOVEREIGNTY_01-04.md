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

**Status: NOT APPROVED — production ASRC path is not closed.** The HAL clock
snapshot reader/tracker in `omega_effect.cpp` currently feeds clock diagnostics;
`VariableAsrc::setTargetRatio()` has no production call site. The daemon's
`HalClockProducer` deliberately has no owner-side source (`ensureSource()`
returns `kNoSource` unless a test source is injected). Therefore there is no
verified PAL-owned correlated frame/timestamp publisher and no demonstrated
ASRC actuator in the live audio route. Keep ASRC ratio at unity/bypass until
that owner-side callback and the complete consumer path are implemented and
probed. No device-clock or Bluetooth/PCM timing capability is claimed here.

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

**Status: NOT APPROVED.** The Pi-LSTM bridge is called from the native audio
processing path and advances RK4 state there; JNI setters and telemetry getters
also access that state from the Java/control side without a demonstrated
single-owner or synchronization contract. Treat this as a data-race risk until
ownership is serialized or parameter/state publication is made RT-safe and
covered by concurrency tests. TinyML/classifier presence is not proof of a
connected low-level inference stage. INT8 inference, deterministic execution,
NEON equivalence, preallocation, callback-budget and audio-thread isolation
require measured functional evidence before approval.

## Release policy

Production publication is fail-closed by `scripts/check_hws_release_approval.py`.
The machine-readable `docs/hardware_sovereignty_release_approval.json` manifest
must mark **all four phases APPROVED** and include non-empty evidence for each.
Any `NOT_APPROVED`, `PARTIAL`, `IN_PROGRESS`, `BLOCKED`, missing status,
missing evidence, or malformed manifest blocks `publish-release`.

The staging/build-validation jobs remain available so engineering can produce
and inspect candidate APK/Magisk artifacts without claiming production approval.
Do not bypass the gate by changing only this document: phase status changes
require independent evidence (including Moto G85 runtime evidence where stated).
Preserve OEM/Dolby paths and transparent bypass.
