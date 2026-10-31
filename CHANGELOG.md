## v2.5.210 (501) — 2026-10-31

- **Ω-ATOMIC Genezi AUX policy:** añade una solución minimax determinista que equilibra el objetivo de foco (multiplicador 1.00) y la referencia de inmersión (1.08). El compromiso exacto es 1.04 y se publica desde `AudioRouteManager` hacia las rutas de control existentes.
- **Pruebas JVM:** verifica el minimax frente a una barrida del intervalo, el peor error, la proyección a límites y el fail-closed ante NaN/intervalos inválidos. La prueba de perfil AUX queda alineada a 1.04.
- **HRTF en line-out externo:** si el usuario declara `externalStereoLineOut` y mantiene HRTF solicitado, AUX/USB conserva el motor activo con wet 0.15; auriculares conservan 1.0. El HRTF de line-out es coloración espacial de baja mezcla, no cancelación transaural. SAF binaural personal, RIR sintético y WFS multialtavoz siguen sin inyectarse en una salida L/R que no puede justificar esas transferencias.
- **Cadena Genezi sincronizada:** `docs/AUDIO_CHAIN_MOTO_G85_STEREO.md` ahora refleja el nuevo wet HRTF externo, el multiplicador minimax 1.04 y los límites reales de AUX L/R frente a WFS multicanal.
- **Límite explícito:** 1.04 es un multiplicador de intensidad espacial del plano de control, no el ancho acústico medido ni una inversa de sala. No cambia el DSP nativo ni certifica HRTF transaural, WFS físico, respuesta del Genezi o teletransportación perceptual. La validación auditiva/ABX y la medición física siguen pendientes.
- **Updater corregido:** `module.prop` y `update.json` apuntan al repositorio real `IVANNA-OMEGA-SUPREME`; el release espera el asset canónico `ivanna_omega_supreme_v2.5.210.zip`.
- **Release:** v2.5.210 / versionCode 501. Publicación condicionada a CI, pruebas JVM/nativas, compilación Android, integridad APK/ZIP y verificación posterior de los assets.

## v2.5.209 (500) — 2026-10-31

- **Tipo de sincronizacion del DAC USB medido:** `/proc/asound/card*/stream0` del "JieLi Technology USB Audio" (full speed) muestra el endpoint de reproduccion 0x03 OUT como `(SYNC)`: sincrono, sin endpoint de retroalimentacion. Cierra la pregunta pendiente de `docs/ASRC_SOFTWARE_CLOCK_WIRING.md` (no era asincrono ni adaptativo como se suponia).
- **Consecuencia (solo para este DAC):** un DAC sincrono toma su reloj del SOF del host, asi que no hay discrepancia DAC-contra-host que un ASRC pueda quitar; el servo de `AudioTrackBufferPll` solo ve el residuo dentro del telefono, que la medicion del mismo dia deja a nivel de ruido (47999.5..48001.6 Hz) y se espera que permanezca en ~0 ppm por la banda muerta. El servo se conserva como lazo acotado y seguro. No se afirma nada de otros DAC ni de Bluetooth. Solo documentacion; nada cambia en codigo.

## v2.5.208 (499) — 2026-10-31

- **Primera medicion real de la ruta USB-C (Moto G85):** captura pasiva con `dumpsys media.audio_flinger`, musica sonando por `AUDIO_DEVICE_OUT_USB_HEADSET`. El HAL devuelve pares (frames, tiempo) validos y correlacionados en hilos MIXER (decenas de miles de timestamps, `err`/`disc` bajos), con `localSR` entre 47999.5 y 48001.6 Hz. Es la ruta de posicion de presentacion del HAL, no MMAP: la sonda MMAP no hace falta para esta ruta. Tabla y limites en `hal_integration/MOTO_G85_PAL_CLOCK_INTEGRATION.md`.
- **Lo que no se afirma:** no demuestra deriva del reloj del DAC (el estimador es contra `CLOCK_MONOTONIC` y no distingue oscilador del DAC de temporizacion del host USB), ni un actuador ASRC, ni un punto de lectura propio para IVANNA. Correccion deshabilitada. Solo documentacion.

## v2.5.207 (498) — 2026-10-31

- **Correccion de la v2.5.206:** la hipotesis de que el hook sobre el thunk de 4 bytes tumbo el HAL de audio queda retirada. El tombstone del Moto G85 muestra `SIGABRT` con `Abort message: 'Unable to locate the libc; please file a bug'` y todos los frames dentro de `frida-agent-64.so`: es un fallo del agente de Frida 17.25.0 en esta build (Android 15, V1UOS35H.74-16-4-16), no de la sonda. Cada caida reinicia el HAL y audioserver. No inyectar Frida 17.25.0 en el HAL de audio en esta build.
- **Sin cambios de comportamiento:** la sonda sigue enganchando una funcion grande y rechazando objetivos < 32 bytes, ahora solo como precaucion. Sigue sin haber medicion de la ruta USB-C; enlace fisico y actuador ASRC sin verificar; correccion deshabilitada. Solo documentacion y comentario.

## v2.5.206 (497) — 2026-10-31

- **Sonda MMAP: se dejo de enganchar un thunk de 4 bytes.** Con la v2.5.204, `frida-inject` en el HAL de audio (pid 1433, Moto G85) termino con `The connection is closed` y el proceso desaparecio. La causa no esta confirmada; la hipotesis es que el trampolin de Frida sobre `pal_stream_get_mmap_position` (4 bytes segun la auditoria) pisa codigo vecino. `ivanna_hal_mmap_bounded.js` ahora engancha `StreamOutPrimary::GetMmapPosition` (244 bytes), se niega a enganchar objetivos de menos de 32 bytes, lee la estructura Android `audio_mmap_position` y se limita a 300 retornos.
- **Pendiente:** el log/tombstone del cierre del HAL para confirmar la causa. No hay medicion de la ruta USB-C todavia; enlace fisico y actuador ASRC siguen sin verificar y la correccion sigue deshabilitada. Solo comprobado por sintaxis, no en dispositivo.

## v2.5.205 (496) — 2026-10-31

- **Sondas de shell del Moto G85 incorporadas:** `hal_integration/tools/ivanna_hal_mmap_shell_probe.js` (simbolos, runtime de AudioOut_D/AudioOut_15 y logcat) y `ivanna_hal_usb_probe.js` (muestras de la salida USB-C, handle 21). Son de solo lectura.
- **Correccion del criterio:** el texto original decia que `pal_stream_get_mmap_position` con retorno 0 y frames avanzando significaba "no hay mmap valido"; en PAL 0 es exito. Ahora dice: rc==0 con frames que avanzan y tiempo monotonico = candidato a enlace; rc!=0 o sin llamadas = no es MMAP, no cablear.
- **Limite:** estas sondas no llaman ni enganchan `pal_stream_get_mmap_position`, asi que no miden pares frames+timestamp; la medicion real es `ivanna_hal_mmap_bounded.js` (Frida, v2.5.204). No hay resultados de dispositivo todavia; el enlace fisico y el actuador ASRC siguen sin verificar y la correccion sigue deshabilitada.

## v2.5.204 (495) — 2026-10-31

- **Evidencia nueva del Moto G85 (auditorias HAL/PAL del 2026-10-31):** `audio.primary.blair.so` importa `pal_stream_get_mmap_position` y define `StreamOutPrimary::GetMmapPosition` y `GetFramesWritten(timespec*)`; `libar-pal.so` tiene la cadena `GetMmapPosition -> pcm_mmap_get_hw_ptr`. Registrado en `hal_integration/MOTO_G85_PAL_CLOCK_INTEGRATION.md` con lo que prueba y lo que no.
- **Sonda nueva, solo lectura:** `hal_integration/tools/ivanna_hal_mmap_bounded.js` engancha pasivamente `pal_stream_get_mmap_position` (maximo 600 retornos, sin llamar a PAL ni escribir) para saber si el stream de musica activo es MMAP en altavoz, Bluetooth y USB-C.
- **Lo que NO cambia ni se afirma:** el eslabon fisico (llamada dentro del HAL propietario) sigue sin enlazar y no hay actuador ASRC identificado; el daemon sigue en fail-closed y la correccion deshabilitada. Son hechos ELF estaticos: no hay medicion de reproduccion. La sonda solo se comprobo por sintaxis (`node --check`), no en dispositivo.

## v2.5.203 (494) — 2026-10-31

- **Enlace de actualizacion tras el traslado del producto:** `magisk_module/update.json`, `module.prop` (`updateJson`), `CITATION.cff`, la plantilla de issues y el enlace del panel Magisk apuntaban a `IVANNA-OMEGA-SUPREME`; ahora apuntan a `IVANNA-OMGA-SUPREME-`. Sin esto la compuerta de `publish-release` (que exige `zipUrl == github.com/$GITHUB_REPOSITORY/releases/download/<version>/...`) fallaba en este repositorio y Magisk buscaba el zip en el repo anterior.
- **No cambia:** audio, DSP, daemon ni ASRC. Solo metadatos de enlace y version (2.5.202 -> 2.5.203, 493 -> 494).
- **Verificacion:** coherencia module.prop / update.json / version.properties comprobada por script local; la publicacion del release la hace el CI al detectar el bump.

## v2.5.202 (493) — 2026-10-30

- **Control sin efecto ya no se disfraza de funcional:** `RouteModeBanner` (en el panel de control) muestra, segun el modo real de `AudioBackendSelector`, que funciones solo llegan al audio con el daemon Magisk. Sin root y con root sin daemon aparece el aviso; con root+daemon no aparece nada. Lista y logica en `RouteCapabilities` con 4 pruebas JVM; fuente en `docs/ROOT_VS_NOROOT_CAPABILITIES.md`.
- **No se hizo (a proposito):** espejar al motor local las acciones solo-daemon. Cada setter local tiene otra escala/destino (p. ej. armonico 0–0.65 sobre `nho_harmonic` frente a 0–2 sobre `IvannaFusionCore`); enlazarlos por nombre cambiaria el sonido sin aviso. Cada una requiere equivalencia comprobada y prueba.
- **Verificacion:** compilacion y pruebas Kotlin solo las corre CI (sin Android SDK local); no probado en dispositivo.

## v2.5.201 (492) — 2026-10-30

- **Aviso falso del instalador:** "`file` no pudo confirmar AArch64" salia porque el `file` de toybox en Android no imprime la arquitectura. Ahora `customize.sh` lee la cabecera ELF (clase 64 bit y `e_machine` 0xB7) y, si el binario es de otra arquitectura, lo dice como error en vez de avisar de forma ambigua. Probado con el `ivanna_daemon` real (acepta) y con una cabecera x86 (rechaza) en `scripts/check_magisk_install.py` (20 pruebas).
- **Reloj/ASRC visibles desde el modulo:** `ivanna_control.sh clock` devuelve el mismo JSON que usa la app (`GET_CLOCK_TELEMETRY`: callback, reloj HAL y estado del ASRC por software) y `ivanna_control.sh hal-probe` ejecuta la sonda de solo lectura de evidencia PAL/ASRC (`ivanna_hal_probe`, copia de `hal_integration/tools/probe_moto_g85_pal.sh`). El instalador lo anuncia.
- **Lo que NO cambia ni se afirma:** el daemon sigue publicando reloj HAL solo con pares frames+timestamp del propietario y no hay actuador ASRC fisico identificado en el Moto G85 (ver `docs/audio/MOTO_G85_ASRC_ACTUATOR_AUDIT_2026-10-26.md`); no se corrige el reloj del DAC. Verificado por pruebas en host y por lectura; **no en dispositivo**.
- **Descartado:** un productor que leia `/proc/asound` por su cuenta; contradice el diseno de `hal_clock_producer.hpp` (el daemon no observa un PCM paralelo). Queda aparcado en la rama `feat/hal-pcm-clock-producer`, sin fusionar.

## v2.5.200 (491) — 2026-10-30

- **Distorsion armonica al subir el volumen (causa medida):** en `SafetyLimiter::process` el codo de la saturacion final (`softCeil`) estaba al 90 % del techo (0.8897), por debajo de los picos que la curva estatica del limitador deja pasar con los parametros reales de ruta (umbral -1 dBFS, techo -0.1 dBFS: 0.89..0.99). Todo pico estable de un master caliente caia dentro del tramo saturado y su cresta se aplanaba de forma permanente. Medicion en host (48 kHz, bloques de 256, seno de 100 Hz y 1 kHz, THD por minimos cuadrados): **0.87 % a 0 dBFS y 1.98 % a +3/+6 dBFS**. Con el codo al 97 % del techo: **0.00 % a 0 dBFS y 0.50 % a +3/+6 dBFS**, sin cambiar nivel, reduccion de ganancia ni techo (la saturacion sigue acotada por el techo y queda reservada al sobrepico de la rampa de ataque de 1.5 ms).
- **Pruebas:** 2 tests nuevos en `test_limiter_route_params` (THD estable < 0.05 % a 0 dBFS, < 1 % a +3/+6 dBFS). Control negativo: con el codo antiguo ambos fallan. Los 13 tests de limitador existentes (`route_params`, `hires_timing`, `true_peak_limiter`) pasan. Validado solo en host con senos; el audio real en el Moto G85 sigue sin medirse.
- **Alternativa descartada:** subir el umbral del limitador a -2.5 dBFS tambien da 0 % de THD, pero rompe el contrato fijado por `test_limiter_route_params` (no aplastar masters calientes: <= 1 dB a 0 dBFS).
- **Sin cambio de comportamiento hacia arriba:** el techo -0.1 dBFS sigue inviolable.

## v2.5.199 (490) — 2026-10-30

- **Efectos perdidos (causa):** con el efecto nativo `omega_effect` acoplado, los efectos stock de la sesion van en bypass, y elegir un perfil (o mover ISO/SAF/EQ en vivo) solo actualizaba esos efectos stock. El unico escritor de `SET_EQ_BANDS` hacia el daemon era el calibrador ISO, asi que el perfil no cambiaba el audio. `IvannaGlobalEffectManager.republishOmegaEq()` publica ahora, en cada reapply, la misma curva compuesta que recibe el Equalizer stock (`EffectTuningPolicy.curve`, politica de headroom sin banda positiva via `omegaBandsDb`). `Iso226Calibrator.applyAll` republica la curva compuesta tras su envio crudo para no pisar perfil/SAF. Sin daemon conectado no se afirma nada (devuelve false y deja log).
- **Pruebas:** 2 tests JVM de `omegaBandsDb` (forma relativa conservada, sin bandas positivas, curva plana/solo-corte intacta). No pude ejecutar Gradle en este entorno (sin SDK/R8 offline): la verificacion de compilacion es la del CI; la matematica de la curva se replico a mano.
- **Contenido acumulado de esta serie (ya en main):** compuertas de CI reparadas (CRLF de `rir/metadata.csv`, `typeattribute` en sepolicy), benchmark con CPU medida y sin xruns/bateria inventados, panel RIR con la sala asignada (indice, RT60 nominal y medido), presets que avisan cuando no llegan al audio.
- **Limites conocidos, sin cambiar aqui:** (1) `sepolicy.rule` concede lectura/escritura sobre `adb_data_file`/`system_data_file`/`magisk_file` a `untrusted_app*` y marca `adb_data_file` como `mlstrustedobject` (v2.5.196): es una ampliacion amplia de privilegios; quitarla exige probar antes el camino por descriptor `SCM_RIGHTS` en el Moto G85. (2) Con el efecto global activo, SAFMUS bloquea la modulacion Bark de Fotoholo y la Ruta B solo recibe el ancho declarado estatico: no hay captura para analizar. (3) La carga real de `libomega_effect.so` en AudioFlinger y el audio procesado siguen sin validarse en el dispositivo.

## v2.5.198 (489) — 2026-10-30

**Build rojo reparado en su causa raiz.** `:app:compileReleaseKotlin` fallaba en
`spatial/BenchmarkRunner.kt:133`: el mensaje de `@Deprecated` interpolaba `$i` y
`${...}`, y una anotacion exige constante en tiempo de compilacion
(`error: Annotation argument must be a compile-time constant` + `Unresolved reference 'i'`).
Se reescribio el mensaje concatenando literales, sin interpolacion.

**Deuda de la misma funcion.** El generador ABX legacy escribia `\$i` y
`\${Random...}`: el CSV salia con los simbolos literales en vez de los valores, es decir,
un dataset que parecia valido y no lo era. Ahora interpola de verdad. La funcion sigue
`@Deprecated` y sin llamadores desde la UI.

**Sin cambios funcionales en audio.** No se toca DSP, HRTF, WFS, 4D ni el ABI del efecto.

## v2.5.197 (488) — 2026-10-30

**IPC: el descriptor de omega_shm ya no se pierde por una señal.** El receptor
`receiveExactlyOneFd()` (include/omega_control_bus.h) llamaba `recvmsg()` una sola vez;
si Android entregaba SIGCHLD/SIGTERM durante la espera, `errno == EINTR` se convertía en
`kRecvError` y el cliente caía al modo por ruta creyendo que el daemon no entrega el fd.
Ahora reintenta mientras sea EINTR. El resto del contrato ya era estricto: `MSG_CMSG_CLOEXEC`,
rechazo de `MSG_CTRUNC|MSG_TRUNC`, payload de exactamente 1 byte, `SCM_RIGHTS` obligatorio,
exactamente 1 descriptor y cierre de cualquier fd recibido en un mensaje rechazado.

**Cortex: cada fila vacía dice su causa.** Las etiquetas del panel dejan de ser un
"no disponible" mudo y nombran el motivo concreto (sin publicación del daemon, nadie abrió
el PCM, observador sin muestras, AudioTrack sin timestamp). Sigue sin inventarse ningún
valor: si no hay productor, la fila lo dice.

**Sin cambios en ABI del efecto.** El símbolo `AELI`, el tag `0x41454C54` y el vtable de
7 campos ya estaban corregidos y con regresión en host; este release no los toca.

## v2.5.196 — USB clock-drift estimator, 16 KB ABI alignment, measured RIR RT60

- **USB AudioTrack clock servo:** `AudioTrackBufferPll` estimates sink drift with a Theil-Sen median of pair slopes over a ~30 s window (<= 40 sub-sampled points, pre-allocated buffers), a per-sample plausibility gate (0.7x..1.3x nominal) and a feed-forward weight that ramps over the first 20 s. Basis: AudioFlinger dump from the Moto G85 + USB DAC (timestamp jitter sigma ~0.137 ms, spikes to 109 ms). Synthetic model (`tools/safmus/usb_drift_estimator_sim.py`, thresholds fixed before running): previous 8-point OLS std 34-54 ppm / max error 105-368 ppm; current std 1.3-1.9 ppm / max error <= 5 ppm. JVM tests include a negative control (the old estimator misses the 12 ppm bound by 208 ppm). Not validated on a real device over time.
- **ABI:** `libivanna_omega.so`, `libomega_effect.so`, `ivanna_daemon` and `ivanna_client` link with `-Wl,-z,max-page-size=16384` (the v2.5.177 artifacts measured p_align=0x1000). `verify-release` now fails if any LOAD segment of those binaries is below 0x4000; the Release publishes `SHA256SUMS.txt` (ZIP + APK).
- **RIR dataset:** new `RT60_T20_measured_s` column (T20 Schroeder from the 200 WAVs; estimator checked against synthetic decays, error < 1 %). The historical `RT60_s` is nominal: measured/nominal = 2.284 +/- 0.022 (n=200), kept as-is because selection and tuning are calibrated against it; UI says "nominal". `RirDataset` exposes `rt60MeasuredS`; native and Python tests include negative controls.
- **SELinux/MLS:** `typeattribute adb_data_file mlstrustedobject` (policy + `service.sh` live apply) fixes the `omega_shm` write denials from the app domain (231 denials in the captured log tail). Broadens MLS trust for that file type.
- **Clock telemetry:** daemon command `GET_CLOCK_TELEMETRY` relays beacon and ASRC state that the app cannot read under DAC/SELinux; the native ASRC ratio sign is corrected (`rho = 1/(1 + drift*1e-6)`), verified closed-loop (residual <= 1.4 ppm vs 100-300 ppm uncorrected).
- **Diagnostics:** `tools/diag/capture_usb_clock.sh` v2 (the v1 glob matched nothing on this kernel; adds AudioFlinger timestamp stats and filters the `omega_shm` log flood) and `hook_pal_agm.js`.
- Evidence limits: host/synthetic validation plus one captured session; the 16 KB alignment and Android build are verified only by CI. No physical USB-DAC or Bluetooth drift validation. Release metadata advances to v2.5.196 / versionCode 487.

## v2.5.195 — multiline OEM audio-effect registration repair

- Fixes Magisk installation rejecting a valid IVANNA effect when its name, library and UUID span multiple XML lines, including the reported “falta registro UUID de IVANNA validado” failure.
- Builds a comment-free validation view without rewriting the OEM registrations; checks library and UUID independently of attribute order and avoids basic-regex alternation.
- Preserves incompatible-registration rejection and OEM postprocess effects. Adds isolated installer regression checks under POSIX sh and BusyBox when available.
- Release metadata advances together to v2.5.195 / versionCode 486. Host fixtures do not establish HAL acceptance or PCM processing on the physical device.

## v2.5.194 — SAF calibration, SAFMUS and verified audio controls

- Refines all named stock-effect profiles with hardware-frequency EQ mapping and independent profile/ISO/manual/SAF composition, one combined target headroom calculation and usable width control across its full range.
- Persists the base effect profile; startup preserves user choices, restores validated SAF, obeys declared external stereo routing and does not mark factory ISO values as a measured calibration. Adds 15 JVM regression tests (41 selected JVM tests pass locally).

- Bounds the actual SAF natural-gradient step, validates state transactionally and synchronizes restored profiles with the existing native control bus.
- Requires completed calibration stimuli before feedback and separates numerical convergence, transport acknowledgement and physical audio verification.
- Hardens SAFMUS voxel keys and capture-buffer arithmetic; adds explicit USB DAC to external stereo routing and preserves calibration during bypass.
- Repairs ABX trial validity, native control readback, web authentication, reproducible installation and accessible navigation.
- Documents the supplied Sony MHC-PZ1D manual: analog L/R input, 2CH STEREO / 2CH SURR and satellite duplication. No discrete multichannel AUX or guaranteed perceptual result is claimed.
- Local validation: 605 native tests, 26 focused JVM tests, 4 web tests, Android debug packaging and static wiring gates. Physical Moto G85 / DAC / Sony validation remains outstanding.
- Release metadata advances together to v2.5.194 / versionCode 485; see [repair notes](docs/REPAIR_RELEASE_NOTES.md) for remaining integration limits.

## v2.5.193 — verified release trigger and hardened OEM audio-effect merge

- Advances release metadata atomically to v2.5.193 / versionCode 484 so the main-branch version-bump detector actually runs the publish-release job after the green CI build.
- Preserves the OEM `audio_effects.xml` and independently validates IVANNA library/effect registration plus music, movie, and game postprocess attachments.
- Fails closed if a readable, valid OEM configuration cannot be found; it will not overlay the static fallback and risk removing proprietary Qualcomm/Dolby/Dirac effects.
- Release availability is not assumed from a green CI badge: the APK and Magisk ZIP must be uploaded to the non-draft GitHub Release and verified against the build before the release is considered published.
- Runtime HAL acceptance and actual PCM processing still require device-side confirmation.


- Adds IVANNA `omega_effect` to the detected OEM `audio_effects.xml` postprocess chain for music, movie, and game streams during Magisk installation, preserving the existing OEM configuration rather than replacing it when the base XML is readable.
- Retains the static vendor XML as a fallback only when the installer cannot read the device's base configuration; actual loading and processing still require device-side confirmation.
- Builds and release artifacts remain gated on GitHub Actions success and artifact verification.

## v2.5.190 — debt closure: English quick start, device-evidence notes, citation metadata

- Adds `README.en.md`, a one-page English orientation (what it is, Route A vs Route B, install from Releases, running the host test suite, how to file a device report, security reporting, licence status). Linked from `README.md` and `LEAME.md`. Closes #57.
- Preserves three evidence notes for the Moto G85 USB path under `docs/audio/` (PAL clock evidence, ASRC actuator audit, USB route/mixer discrepancy; previously stranded in open PRs #162/#163). They record what was and was not demonstrated; the SAFMUS servo stays blocked and no PAL/ASRC actuator claim is made.
- Fixes `CITATION.cff`, which still declared version 2.5.16 (about 170 releases stale). The fixed `version`/`date-released` fields are removed rather than re-pinned, so they cannot silently rot again; cite the exact release tag.
- Aligns `module.prop`'s description, which still read "v2.5.188" while the module was v2.5.189.
- Not merged on purpose: PR #170 (USB-C route readback) is 181 commits behind and its release-metadata files would roll the version back, so it needs a rebase; Dependabot PRs #164/#165/#168 fail the Android build in CI.
- No native, Kotlin or DSP code changed. Version metadata: v2.5.190 / versionCode 481. Release remains contingent on CI passing and verifying the exact APK and Magisk ZIP.

## v2.5.189 — SAFMUS bidirectional clock-drift polarity proof

- Adds a deterministic JVM regression for a consumer clock running 1,000 ppm slower than nominal, complementing the positive-drift test.
- Verifies signed OLS drift, measured rate, and actuator polarity: a slower consumer clock must not receive the same correction sign as a faster one.
- Advances all release metadata to v2.5.189 / versionCode 480 because v2.5.188 is already immutable and its latest build cannot be attached to that tag.
- Release remains contingent on GitHub Actions passing the Android/JVM/native gates and verifying the exact APK and Magisk ZIP assets; no physical HAL/PAL or Bluetooth clock-control claim is made.

## v2.5.188 — SAFMUS clears stale clock drift when presented frames stall

- Treats an advancing monotonic timestamp with a frozen presented-frame position as a stalled/invalid clock observation instead of retaining an old OLS drift estimate in feed-forward control.
- Clears stale measured rate and drift while retaining the independent buffer-occupancy PI, so queue recovery can continue without trusting a stopped clock.
- Adds a JVM regression for a previously locked +1,000 ppm clock that then stops advancing; verifies drift lock and stale telemetry are cleared and the correction remains bounded.
- Aligns release metadata at v2.5.188 / versionCode 479. CI and device playback measurements remain required; this does not claim physical HAL/PAL or Bluetooth clock control.

## v2.5.187 — SAFMUS anti-windup saturation boundary and drift deadband

- Fixes the PI anti-windup decision to evaluate the current unsaturated actuator demand, allowing the controller to reach the exact ±500 ppm clamp instead of freezing one integral step early.
- Adds a 0.5 ppm feed-forward deadband so numerical OLS noise around a nominal clock cannot create non-zero resampler corrections while buffer occupancy is inside its deadband.
- Keeps the ±500 ppm actuator clamp, bounded slew, clock-estimator validation and release JVM tests enabled.
- Release candidate only: GitHub Actions must pass all JVM/native tests and verify APK/ZIP integrity before v2.5.187 is considered released.

## v2.5.186 — SAFMUS clock reacquisition and release metadata gate

- Includes the clock-regression outlier fix: invalid OLS windows discard stale drift feed-forward and reacquire from a fresh monotonic pair.
- Adds release-time consistency checks across version.properties, module.prop, update.json version/versionCode and the canonical ZIP URL; publication fails closed on mismatch.
- Aligns app, module, update manifest and README at v2.5.186 / versionCode 477.
- CI must pass and publish/verify the exact commit artifacts before this version is considered released.

## v2.5.185 — SAFMUS rejects contaminated clock-regression windows

- When an OLS clock-rate estimate exceeds the validated range, clear the contaminated rolling window and stale drift feed-forward, then reacquire from the newest monotonic frame/timestamp pair.
- Keep the independently measured buffer-occupancy PI active; the rejected clock estimate no longer silently remains in control.
- Add a JVM regression proving one impossible frame/timestamp sample drops drift lock and clears stale rate telemetry.
- Version metadata aligned at v2.5.185 / versionCode 476. CI and device measurements remain the acceptance gates.

## v2.5.184 — SAFMUS buffer deadband and anti-windup on the live AudioTrack servo

- Implements a continuous 2% normalized buffer-occupancy deadband in the existing single production controller, preventing small queue-measurement noise from hunting the resampler.
- Adds conditional integration anti-windup so persistent error cannot wind the integral farther into saturation of the ±500 ppm actuator.
- Adds JVM regressions for deadband quietness and sustained saturation bounds. The live AudioTrackBufferPll → StereoAudioResampler.setClockCorrectionPpm() wiring remains the sole actuator path for IVANNA's owned player.
- No second controller is introduced; daemon/PAL telemetry is not cross-fed into the app-owned AudioTrack. Physical DAC/HAL/PAL or remote Bluetooth clock control remains unclaimed.
- Aligns version metadata at v2.5.184 / versionCode 475; CI publication and device validation remain required.

## v2.5.183 — ASRC telemetry expires when its producer stops

- Adds a monotonic publication timestamp to the existing shared snapshot. Readers reject future or stale samples (default maximum age: 1 second), preventing persisted TRACKING state from masquerading as live control after producer failure.
- Adds a regression test for stale-snapshot rejection with a shorter explicit age budget.
- Aligns version metadata at v2.5.183 / versionCode 474.

## v2.5.182 — ASRC controller rejects untrackable and nonphysical configurations

- The regression estimator has a fixed nine-anchor window. Configurations requesting nine or more intervals could never enter TRACKING; the constructor now rejects them fail-closed.
- Correction and rejection thresholds must remain below 1,000,000 ppm so the documented inverse-ratio mapping cannot reach a zero or negative denominator. Invalid settings publish FAULT/unity instead of silently running an impossible controller.
- Adds regression tests for an oversized observation window and a non-positive-ratio configuration.
- Aligns version metadata at v2.5.182 / versionCode 473.

## v2.5.181 — ASRC shared-state IPC removes the non-atomic reason-string race

- Replaces the shared-memory reason[24] character array with six lock-free atomic 32-bit words. A sequence counter alone does not make concurrent non-atomic string copies race-free.
- Publisher rejects invalid enum states, non-finite drift/target/ratio values, and non-positive ratios. Reader validates state and numeric fields after a stable sequence read, rejecting corrupt telemetry.
- Adds host regression coverage for NaN, infinity, and non-positive ASRC ratios.
- Aligns version.properties, module.prop, Magisk updater metadata and README at v2.5.181 / versionCode 472.
- This hardens software telemetry; it does not claim physical HAL/PAL ASRC actuation or device-level acoustic certification.

## v2.5.180 — Stereo ASRC rejects incomplete interleaved frames safely

- Closes a malformed-input edge case in `StereoAudioResampler.process()`: an odd-length interleaved L/R float array previously passed through unchanged on the unity-rate fast path, while resampling paths silently ignored the final unmatched sample. The two paths therefore had inconsistent frame contracts.
- The resampler now removes only the unmatched trailing sample before either fast-path or FIR processing. This preserves L/R parity, prevents a partial frame from leaking through, and leaves the state/output for valid even-length PCM unchanged.
- Adds JVM regressions for unity-rate transparency with odd-length input and equivalence between malformed input and its complete stereo prefix during resampling.
- Updates README release metadata and aligns `version.properties`, `module.prop`, and Magisk updater metadata at v2.5.180 / versionCode 471.
- Release acceptance remains gated on CI tests, Android build, APK/ZIP integrity and publication. This software fix does not certify physical Bluetooth, DAC, PAL or HAL clock correction.

## v2.5.179 — TinyML ring-buffer overflow is now counted, not silently dropped

- `IvannaTinyMLAudioKernel::ingestPcmBlock()` called `SpscAudioRingBuffer::push()` (declared `[[nodiscard]]` for a reason) and discarded its `bool` result, twice. If the inference worker thread ever fell behind the audio producer (thermal throttling, priority inversion, scheduling starvation) a full ring silently dropped decimated audio feature samples with zero counter, zero log, zero way to observe it from outside. Normal load never hits this (ring holds ~1s of decimated audio against a worker that drains every 2ms), but a silent failure mode in a real-time acoustic-classification path is still a defect in its own right.
- Fix: both `push()` call sites now check the return value and increment a new relaxed atomic `m_ringOverflowCount` on failure — one `fetch_add`, no branch in the common (success) path beyond the existing check, no allocation, no I/O, RT-safe. Exposed via `ringOverflowCount()`.
- Also fixed the matching consumer-side `pop()` in `workerInferenceLoop()`: it is an SPSC invariant that pop() cannot fail right after the `available() >= kHopLengthSamples` check, but the code previously discarded the result anyway. Now guarded: on an (expected-never) pop failure, the loop skips that hop instead of running inference on a stale/partially-shifted frame buffer.
- Two new regression tests in `test_tinyml_kernel.cpp`: `RingOverflowCounterStaysZeroUnderNormalLoad` (realistic load keeps the counter at 0) and `RingOverflowIsCountedNotSilentlyDropped` (a single synchronous call producing ~8x ring capacity in decimated samples must register at least one counted overflow, and the kernel must remain in a valid, crash-free state afterward — confirmed by a follow-up ingest + control read producing finite values).
- Verified: full native CTest suite rebuilt and run clean host-side with clang-18 (598/598, up from 596 before this change), including both new tests actually passing against the real fix, not just compiling.

## v2.5.178 — re-applies two previously-correct dead-code deletions

- Re-deletes `AudioResampler.kt` and `HeadTrackingManager.kt`. Both were already deliberately removed once, with the right justification: `6ed52c42` ("0 referencias; AudioPipeline ya hace el 48→16 kHz inline") and `a901e749` ("0 referencias; duplicado de IvannaHeadTracker, ya cableado vía IvannaSpatialManager"). Both got silently resurrected by an unrelated commit (`88c4bc9d`, the Acoustic Unity Engine feature) that evidently merged from a branch based on an older tree — git history shows the exact same two files being re-added by that single commit alongside otherwise-legitimate new work.
- Re-verified before deleting again, not just trusted the old commit messages: grepped the full `app/src/main` tree for both class names outside their own declaration files — zero hits for either. `StereoAudioResampler` (not `AudioResampler`) is the real, tested, production resampler (`IvannaBridgePlayer.kt`, `AudioTrackBufferPll`, and three JVM test suites). `IvannaHeadTracker` (not `HeadTrackingManager`) is the real, tested, production head tracker, wired in `IvannaSpatialManager.kt` via `nativeObjectRendererSetHeadTracker`.
- No functional/behavioral change: this removes dead weight, it does not touch the live audio or spatial path. Flagging in case any other session's branch still carries the old resurrected copies — merging it back in would undo this a third time.

## v2.5.177 — reroute-vs-drift guard, AutoEQ corpus/Kotlin gate, dead-code removal

- Fixes a real Bluetooth-vs-clock-drift ambiguity in the app-owned AudioTrack path: Android can silently reroute a playing `AudioTrack` to a different output device (A2DP connect/disconnect, output-policy change) without the app recreating the track. That reroute is a one-step latency change, not a sustained clock-rate error, but `AudioTrackBufferPll`'s rolling regression had no way to tell the two apart. Adds `AudioTrackRerouteGuard`, a single pure function wired into `IvannaBridgePlayer`'s existing 250ms clock poll: on a confirmed `routedDevice.id` change it resets the PLL and fails closed to unity correction instead of feeding the discontinuity into the drift estimate.
- Removes `AudioTrackClockDriftServo` and its test: a pre-unification (pre-v2.5.173) drift estimator left with zero production consumers after `AudioTrackBufferPll` became the single controller authority. Confirmed via repo-wide grep before deletion.
- Adds `tests/test_autoeq_kotlin_matches_extractor.py`, wired into CI: re-runs `tools/hpir/extract_hpir_profiles.py` against the real HpIR SOFA corpus and parses `AutoEqManager.kt`'s hardcoded `PROFILES` literal, failing the build if they diverge. Closes the exact drift risk `AutoEqManager.kt` already documented from an earlier incident (2026-09-10) with no automated gate behind it.
- Documents a verified, unresolved data-provenance issue found while building that gate: `Data.IR` in `hpir_AKGK271MKII_nh719.sofa` and `hpir_AKGK272HD_nh719.sofa` is bit-for-bit identical (`np.array_equal` true) despite being distinct SOFA files for distinct headphone models. The "AKG K272 HD" AutoEQ profile is therefore the K271 MKII measurement under another name, not an independent measurement — flagged in code and here rather than presented as real. No fix is possible without a genuine K272 HD measurement, which is not in this repository.
- Host verification performed this change: `test_shm_lifecycle` and the end-to-end SCM_RIGHTS handshake reproduction (49 + 7 checks, unrelated to this change but re-run to confirm a clean baseline), `test_variable_asrc`/`AsrcClockController` (13/13, including the `InvertedSignWouldBeDetected` negative test), `validate_sofa_hdf5.py` against all 216 real `.sofa` assets (0 failures), `check_safmus_transversal_wiring.py` (PASS), `sync_asrc.py --self-test` (PASS), and the new `AudioTrackRerouteGuardTest`/`AudioTrackBufferPllTest` compiled and run standalone with a host `kotlinc` + JUnit (6/6, 2/2). The new `AudioTrackRerouteGuard` wiring inside `IvannaBridgePlayer.kt` itself could **not** be compiled or run in this environment — the Android Gradle plugin's dependencies (`com.android.tools:r8`, etc.) are not reachable through this sandbox's network allowlist. That compile/build/instrumented verification is left to CI and real-device testing; it is not claimed here.

## v2.5.176 — SAFMUS ASRC actuator polarity regression

- Adds an Android JVM regression proving that positive clock-correction ppm produces more output frames than negative ppm in the actual app-owned `StereoAudioResampler` path.
- Exercises the bounded ±500 ppm actuator at 48 kHz with deterministic stereo audio and rejects a reversed correction sign or a correction that has no measurable output-rate effect.
- This verifies the software resampler contract only; it does not certify USB DAC, Bluetooth sink-clock, PAL/HAL, or system-wide clock correction.
- Version metadata: v2.5.176 / versionCode 467. Availability requires the current commit's native tests, JVM tests, Android build, artifact-integrity checks and release publication to pass.

## v2.5.175 — SAFMUS time-evolving acoustic regression and release candidate

- Carries forward the SAFMUS bounded clock/FIFO servo, Q0.32 fractional phase stepping, 64-tap Kaiser ASRC reference, clock-loop feasibility checks, and GCC-PHAT delay-polarity regression gates.
- Includes the latest native WFS time-evolving acoustic-trajectory regression added on main; CI must execute it with the complete native DSP suite.
- Aligns version metadata for a fresh release pipeline run so the verified APK and ARM64 Magisk ZIP are built from the same commit.
- Scope remains explicit: the host ASRC reference is not a PAL/HAL/DAC actuator. Live drift correction claims remain limited to the app-owned AudioTrack route; Bluetooth sink-clock and system-wide correction require separate device evidence.
- Release is considered usable only after the current commit's Android build, JVM tests, APK/ZIP integrity gates and published assets all pass.
- Version metadata: v2.5.175 / versionCode 466.

## v2.5.174 — SAFMUS sync/ASRC mathematical reference and release verification

- Adds a deterministic host reference for the bounded clock/FIFO servo, Q0.32 fractional phase stepping, 64-tap Kaiser ASRC, clock-loop bandwidth feasibility, and GCC-PHAT fractional-delay polarity.
- Adds regression gates for unit-DC phase kernels, unity-ratio frame advance, accumulated fractional phase, infeasible jitter headroom, bounded controller output/covariance, and GCC-PHAT delay sign.
- Makes CI execute the reference self-test and its regression suite alongside the native DSP and Android packaging gates.
- Evidence boundary: the reference model is not a runtime actuator. Live correction remains scoped to IVANNA's app-owned AudioTrack; no system-wide, PAL/HAL, DAC or Bluetooth sink-clock correction is claimed.
- Release acceptance requires the current commit's Android build, staged ZIP/APK integrity checks, and publication workflow to pass.
- Version metadata: v2.5.174 / versionCode 465.

## v2.5.173 — SAFMUS unified app-owned clock servo

- Unifies the app player's clock-drift feed-forward and buffer-occupancy PI into one actuator path; removes the separate diagnostic estimator from the live correction loop.
- Adds centered rolling least-squares estimation over paired AudioTrack presented-frame/timestamp samples, bounded drift filtering, ±500 ppm output limits, and 25 ppm/update slew limiting.
- Preserves fail-closed unity correction on invalid timestamps, regressing frame/time pairs, impossible queue occupancy, or controller reset.
- Adds JVM regression tests for clock-slope polarity, feed-forward correction, bounded control, and timestamp-regression reset.
- Scope is the app-owned AudioTrack only. The daemon's native controller remains separate; no claim of PAL/HAL, physical DAC, Bluetooth sink-clock, or system-wide correction.
- Version metadata: v2.5.173 / versionCode 464. Release requires passing native tests, Android JVM tests, build, and artifact verification.

## v2.5.172 — SAFMUS clock-drift estimator and ASRC ratio contract

- Replaces interval-only drift smoothing with a fixed-size rolling least-squares estimate over paired cumulative frame positions and monotonic timestamps.
- Defines the native ASRC ratio as input frames consumed per output frame; positive source-clock drift advances input consumption rather than increasing output production.
- Adds finite/range validation, timestamp/frame regression rejection, outlier hold, a configurable deadband, bounded correction and slew limiting, with unity-ratio fail-closed snapshots.
- Adds native regression tests for drift estimation, ratio polarity, deadband, invalid-clock behavior, bounds and slew.
- Documents that the native controller is not wired into the live player actuator; the existing player continues to use its AudioTrack buffer PLL and StereoAudioResampler. No claim of PAL/HAL, system-wide, Bluetooth-sink oscillator control, or hardware certification.
- Version metadata: v2.5.172 / versionCode 463. CI and release-artifact verification must pass before the release is considered available.

## v2.5.171 — Resilient adaptive telemetry UI

- Rejects incomplete or non-finite native telemetry snapshots before they can paint plausible-but-invalid meters; shows a clear invalid-data message instead.
- Clamps signal and spectral display ranges and guards meter normalization against NaN, infinity and invalid ranges to prevent broken progress bars.
- Uses `Locale.ROOT` for numeric telemetry so decimal separators remain stable across device locales and comparisons.
- Keeps telemetry read-only; no synthetic fallback values are presented as measured audio.
- Version metadata: v2.5.171 / versionCode 462. CI must pass native tests, Android build, artifact integrity and release publication before the package is considered ready.

## v2.5.170 — Responsive OMNI visual hierarchy

- Replaces the single-line OMNI status-chip row with a responsive FlowRow capped at three chips per line. Route and processing indicators remain visible on narrow screens and under larger font settings instead of competing horizontally with the product title.
- Preserves the existing Aurora Obsidiana palette and real-state labels; no decorative status is presented as proof of active audio processing.
- Updates README release badge and replaces stale v2.5.155/v2.5.150 notes with the current v2.5.170 scope and explicit hardware-validation limits.
- Version metadata: v2.5.170 / versionCode 461.

## v2.5.169 — Limiter telemetry correctness

- Fixes a control-loop blind spot: SafetyLimiter previously reported 0 dB reduction for peaks above its threshold but below its ceiling, despite applying attenuation. Telemetry now derives target gain reduction from the limiter's bounded block gain.
- Adds a regression test for the threshold-to-ceiling region.
- Aligns AdaptiveDecisionEngine's headroom reference and conversion default with the actual SafetyLimiter ceiling (0.98855).
- Version metadata: v2.5.169 / versionCode 460.

## v2.5.168 — Adaptive engine audit and coordination-state repair

- Reconciles the agent coordination ledger with current code: `AdaptiveDecisionEngine` is wired in Ruta A (JNI) and Ruta B (`omega_effect`); the old “not consumed / tests empty” statements are explicitly marked historical.
- Preserves the evidence boundary: CI verifies host tests, Android packaging, staged ZIP/APK integrity and publication; it does not claim physical-device acoustic or real-time jitter certification.
- Release metadata synchronized: `version.properties`, `magisk_module/module.prop`, and `magisk_module/update.json` use v2.5.168 / versionCode 459.

## v2.5.167 — SOFA integrity gate and verified pipeline

- Adds real HDF5 open/parse and dataset-read validation for every repository `.sofa` file, with bounded-memory traversal.
- Adds regression tests for valid SOFA-like HDF5, corrupt files, and invalid convention metadata.
- Makes the native CI gate run SOFA validation, SAFMUS wiring checks, adversarial/synthetic audio tests, auditory-gate policy tests, and the native CTest suite before Android packaging.
- Verifies the downloaded staged APK/ZIP bundle and runs OEM stability/performance and IAEL audio certification gates before publication.
- This release does not claim measured acoustic superiority: real-device loopback, route-specific latency, HRTF perceptual validation, and calibrated listening measurements remain evidence-gated.

## v2.5.166 — Release recovery and Genezi Spatial AUX

- Genezi AUX profile uses the conservative stereo width multiplier (1.08) with bass/dialog boosts disabled in the route profile.
- Documents the ideal Mid/Side model and distinguishes it from the frequency-dependent native DSP implementation.
- CI Android SDK provisioning retries failed package downloads and clears partial SDK download caches after corrupted NDK archives.
- Release remains gated on successful Android build, unit tests, and artifact integrity verification.

 — v2.5.165 (456) — Genezi Spatial publication repair

## Release integrity
- Carries forward the Genezi Spatial AUX beta and canonical Mid/Side model specification.
- Corrects the updater metadata versionCode mismatch and aligns module.prop, update.json, and version.properties at 456.
- Release is considered published only after CI publishes and verifies the versioned Magisk ZIP and APK assets.

