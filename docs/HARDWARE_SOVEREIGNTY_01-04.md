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

**Status: PARTIAL — NOT APPROVED.** The Pi-LSTM bridge now stages JNI/control
writes in lock-free atomic mailboxes; the audio callback is the sole owner that
applies parameters, advances RK4 state, and updates residual state. JNI
telemetry getters read atomic snapshots rather than the mutable engine. Reset
requests are deferred to the audio owner and clear preallocated state without
rebuilding filter coefficients. This removes the previously identified direct
JNI/audio-thread data race in this bridge's access path.

Acceptance still requires Android/NDK build evidence, ThreadSanitizer or an
equivalent concurrent stress test for the bridge contract, callback-budget
measurements on Moto G85, and an audit of every other Pi-LSTM engine instance
and caller. TinyML/classifier presence is not proof of a connected low-level
inference stage. INT8 inference, deterministic execution, NEON equivalence,
preallocation, callback-budget and audio-thread isolation require measured
functional evidence before approval.

## Auditoría L1 — 2026-11-10 (HEAD `fc988a49`, v2.5.382)

Auditoría de certificación, línea 1. No cambia ningún estado: las cuatro
fases quedan como estaban. Registra lo verificado en esta fecha y el plan
de cierre de HWS-02. El manifiesto
`docs/hardware_sovereignty_release_approval.json` lleva el detalle
fuente por fuente; su `evidence` sigue vacío en las cuatro fases porque
no existe ningún `evidence/hws-NN/` en el repo y el gate exige la
evidencia como archivos bajo ese prefijo.

- **HWS-01 (IN_PROGRESS):** la puerta ELF AELI existe y corre en CI
  (`scripts/check_aeli_elf.py`, workflow `build.yml`). En dispositivo,
  `libomega_effect.so` está cargada en el HAL (PID 1483), no en
  audioserver (PID 1555), y el efecto nunca ha procesado frames
  (beacon 0). La v2.5.382 (auto-enable + init DSP en creación) aún no
  está verificada en el teléfono.
- **HWS-02 (NOT_APPROVED):** el hallazgo de la auditoría externa queda
  CONFIRMADO en código: `HalClockProducer::ensureSource()` devuelve
  `kNoSource` sin fuente inyectada (el daemon no inyecta ninguna);
  `VariableAsrc::setTargetRatio()` solo se llama desde tests; el camino
  PR #25 / PR #26 (`PalAsrcBridge`, `PalAsrcOutputPath`,
  `PalAsrcWriteStage`) tiene cero instanciaciones productivas; no hay
  actuador de ratio variable en el PAL propietario para música. La ASRC
  v5 "Élite" SÍ está verificada en el Moto G85 (2026-11-10 00:40 CST:
  cabecera "v5", `fir_taps=287`, `dither=1`, `neon=1`,
  `kalman_drift=−6.250 ppm`, `sat=0`, 2 streams procesando), pero es
  un shim C standalone experimental vía el módulo `ivanna_audio_probe`
  (LD_PRELOAD / reemplazo de `libaudio_probe.so`), no reutiliza el
  camino PR #25 y no está integrado al módulo principal ni tiene
  persistencia verificada tras reboot. El plan de cierre está en el
  manifiesto (`phases.HWS-02.notes.closure_plan`): productivizar el
  write-stage con pin SHA256 + rollback + kill-switch, autenticar la
  fuente de reloj (mmap `0x2ec180` sigue siendo hipótesis hasta ver
  `probe OK` en logcat), medir deriva/sat/processed/under-overruns
  contra la línea base, y resolver la duplicación entre el shim v5 y
  el camino PR #25. No se inventa ningún actuador que no existe.
- **HWS-03 (PARTIAL):** sin evidencia nueva en esta ventana; faltan
  validación de sensores térmicos en el G85, presupuesto de callback y
  throttling de larga duración.
- **HWS-04 (PARTIAL):** sin evidencia nueva en esta ventana; faltan
  build NDK, TSan/stress concurrente, presupuesto de callback en el
  G85 y auditoría de cada instancia/caller Pi-LSTM.

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
