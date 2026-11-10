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

**Status: NOT_APPROVED — punto de inserción IDENTIFICADO en vivo; la
integración productiva (módulo principal) aún no está verificada en
dispositivo.** Ver la actualización 2026-11-10 04:40 CST abajo. El HAL
clock snapshot reader/tracker en `omega_effect.cpp` actualmente alimenta
diagnósticos de reloj; `VariableAsrc::setTargetRatio()` no tiene punto de
llamada en producción. El `HalClockProducer` del daemon deliberadamente no
tiene fuente del lado del propietario (`ensureSource()` devuelve `kNoSource`
salvo que se inyecte una fuente de test). Por tanto no existe un publicador
propietario validado de frames/timestamps pareados del PAL ni un actuador
ASRC demostrado en la ruta de audio viva del código del repo anterior a
esta actualización. Mantener el ratio ASRC en unidad/bypass hasta que el
callback del lado del propietario y el camino completo del consumidor se
implementen y se prueben. No se reclama aquí ninguna capacidad de reloj de
dispositivo ni de timing Bluetooth/PCM.

## Actualización HWS-02 — 2026-11-10 04:40 CST (punto de inserción identificado)

- **Punto de inserción IDENTIFICADO y verificado en vivo en el Moto G85:**
  `pal_stream_write` dentro de `/vendor/bin/hw/android.hardware.audio.service_64`
  (servicio init `vendor.audio-hal`, PID 9123; `libar-pal.so` cargada en ese
  proceso). El probe ASRC v5 lo intercepta EN VIVO: logcat `IVANNA_ASRC`
  `opens=8 ok=8, ratio=1.000058055, drift +58.055 ppm, clock=write-timing`.
  Esto cierra la pregunta abierta del plan de cierre de la auditoría L1
  ("¿dónde se escribe la música?"): se escribe por `pal_stream_write` en
  el proceso del HAL, y la interposición LD_PRELOAD v2 (RTLD_NEXT + dlopen
  sobre libar-pal.so, porque libar-pal.so es RTLD_LOCAL) funciona ahí.
- **Actuador:** sigue sin existir actuador de ratio variable en el PAL
  propietario para música (card 1 Blair: 196 controles, cero de
  reloj/ratio; verificado 2026-11-07). El actuador ES el ASRC de IVANNA en
  el write-stage vía LD_PRELOAD. La ASRC v5 "Élite" está verificada en
  dispositivo: FIR polifásico 287 taps, dither TPDF 2º orden, NEON, servo
  Kalman; deriva −6.250 ppm (frío, 2026-11-10 00:40 CST) / +58.055 ppm
  (2026-11-10 04:40 CST); `sat=0`; dump en
  `/data/vendor/ivanna_audio/pal_asrc_stage.txt` (se reescribe cada 5 s);
  kill switch `touch /data/vendor/ivanna_audio/pal_asrc.off`.
- **Vía productiva (en el working tree, sin commitear — otro agente):**
  la .so v5 se pliega en el módulo principal `ivanna_omega_supreme`:
  `magisk_module/overlay.d/ivanna_audio.rc` (setenv LD_PRELOAD
  `/vendor/lib64/libivanna_asrc_write_stage.so` en el servicio
  `vendor.audio-hal`), `.so` en `magisk_module/system/vendor/lib64/`
  (1,252,048 B, SHA-256 `ac1ef787…` — idéntico al artefacto verificado en
  vivo; `PROVENANCE.txt` con origen y build), regla `noatsecure` en
  `sepolicy.rule` (§v3.6), etiquetado `vendor_file` + desactivación del
  módulo legacy en `customize.sh`, y chequeo de arranque en `service.sh`
  (`ivanna_asrc_preload_status`, solo lectura). El módulo separado
  `ivanna_audio_probe` queda como legacy. **Estado real:** el plegado
  existe en el working tree sin commitear (no en HEAD `8da2e232`); la
  `.so` está en `.gitignore` y el CI debe copiarla desde
  `hal_integration/moto_g85/pal_preload_probe/out/` al empaquetar el zip
  y verificar el SHA. Falta: commit, CI/release 384 y verificación en el
  teléfono DEL MÓDULO PRINCIPAL (no del probe legacy). Riesgo conocido
  del propio `.rc`: `override` sustituye la stanza del fabricante — antes
  de instalar, capturar la stanza exacta del teléfono
  (`cat /vendor/etc/init/android.hardware.audio.service_64.rc`) y
  completarla si trae más opciones.
  **Actualización v2.5.385 (2026-11-10):** la 384 con este `.rc` activo
  brickeó el teléfono DOS veces tras instalar el módulo + reiniciar
  (reset de fábrica en ambas). El `.rc` se mueve a
  `overlay.d.disabled/` (inyección DESACTIVADA por defecto, opt-in con
  rollback a mano); el HAL arranca stock y la .so v5 queda inerte.
- **Visibilidad (este cambio, working tree sin commitear):** el dump del
  write-stage ya es consultable desde la app: el daemon (root) lo lee con
  `include/write_stage_asrc_telemetry.hpp` (solo lectura, fail-closed,
  frescura ≤30 s) y lo releva en `GET_CLOCK_TELEMETRY` (campos
  `writeStage*`); la app lo muestra en el panel Profiler
  (`AudioEngine.getWriteStageAsrcStatic()` + tarjeta "WRITE-STAGE ASRC
  PAL (v5)" en NeonProfilerPanel). Test host nuevo
  `test_write_stage_asrc_telemetry` 11/11. Falta verificación en
  dispositivo con el módulo principal (ver
  `analysis-ivanna/PLAN_VERIFICACION_ASRC_384.md`).
- **Por qué HWS-02 sigue NOT_APPROVED:** el gate exige las 4 fases
  APPROVED con evidencia bajo `evidence/hws-02/` y el manifiesto sigue
  con `evidence: []`. La evidencia en vivo es del probe legacy
  experimental, no del módulo principal plegado, que existe solo en el
  working tree sin commitear y aún no se ha verificado en el teléfono.
  El cierre requiere: (1) el plegado commiteado + CI/release 384, (2)
  verificación en teléfono de la 384 con el módulo principal (dump con
  opens>0, ratio en [0.9990, 1.0010], sat=0, Kalman converge, sonido
  preservado, kill switch), (3) esa evidencia commiteada bajo
  `evidence/hws-02/`. Hasta entonces, no aprobar.
- **Duplicación shim v5 vs camino PR #25/#26:** sigue abierta. El shim v5
  es C standalone y NO reutiliza `PalAsrcBridge`/`VariableAsrc`/
  `HalClockProducer`. La decisión documentada en el manifiesto sigue
  vigente: o el shim es LA ruta ASRC de producción y el camino PR #25
  queda superseded/no usado, o se porta el Kalman+FIR v5 a
  `PalAsrcBridge`. No afirmar que ambas son la ruta de producción.
  (`PalAsrcWriteStage` del repo sigue sin instanciación productiva;
  `VariableAsrc::setTargetRatio()` sigue sin llamador en producción;
  `HalClockProducer` sigue sin fuente propietaria; el servo
  `AsrcClockController`/`appliedRatio` sigue siendo solo telemetría en
  pantalla.)

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
