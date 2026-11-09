# v2.5.370 (661) - 2026-11-09

- **Ruta B — causa raíz encontrada y corregida: `libomega_effect.so` ya no enlaza `libandroid.so`.** Diagnóstico en vivo en el Moto G85 (2026-11-09 01:07 CST): el XML fusiona omega en las 3 configs y la librería está en `/vendor/lib64/soundfx/` (1.7 MB), pero el efecto nunca se creaba. Log definitivo: `EffectsFactoryConfigLoader: loadLibrary Could not dlopen library /vendor/lib64/soundfx/libomega_effect.so: dlopen failed: library "libandroid.so" not found: needed by libomega_effect.so in namespace (default)` → `EffectFactoryHAL: Error querying effect descriptor for 4956414e-...: No such file or directory` → AudioFlinger `createEffect() error -2` → app `AudioEffect initCheck failed -3`. Causa: `app/src/main/cpp/CMakeLists.txt` enlazaba `${android-lib}` en el target `omega_effect`, pero el HAL corre en el namespace de linker vendor donde `libandroid.so` NO existe. El código solo usa `<android/log.h>` (`__android_log_print`, 4 llamadas), que vive en `liblog.so` (ya enlazado); no usa ninguna otra API de libandroid. Fix: `${android-lib}` eliminado del target `omega_effect` (1 línea). El contexto SELinux `system_file` vs `vendor_file` era un arenque rojo (se corrigió en el dispositivo y no cambió nada; el dlopen fallaba antes por libandroid). Pendiente verificar en el G85 que el efecto se crea y `dsp_frames` crece.
- **Predicción 4D — el toggle ya no se apaga solo.** Causa: `FourDControlSurface::status()` devolvía `FourDStatus{}` con `enabled=false` cuando la cadena DSP aún no estaba activa, y la UI leía `enabled=false` cada 400 ms y apagaba el switch aunque el usuario lo hubiera encendido. Fix: `status()` ahora reporta `st.enabled = g_enabled` (el flag guardado por `setEnabled`), aunque la cadena no esté activa; la telemetría (anticipación, confianza) sigue en 0 sin audio, que es lo correcto. Tests host `test_4d_control_surface` 16/16.
- **Control loop detenido — arranque a nivel proceso.** El screenshot del usuario mostraba "IVANNA CONTROL LOOP: Loop detenido". Causa: el loop es de proceso pero solo se arrancaba en `MainActivity.onCreate` (dentro de un launch IO condicional); si ese arranque se saltaba, quedaba detenido aunque la app siguiera viva. Fix: `IvannaControlLoop.start()` también se arranca en `IVANNAApplication.onCreate` (tras el `IntegrityGate.verify`, solo si no está bloqueado). `start()` es idempotente.
- **Atlas/4D en 0% con música — la captura Ruta A excluía a YouTube Music y Amazon Music.** Causa: en `PlaybackCaptureService.setupHardware()`, la lista `videoKeywords` usa substring `"youtube"` y `"amazon"` para excluir apps de video del loopback, pero `"youtube"` casa también con YouTube Music (`com.google.android.apps.youtube.music`) y `"amazon"` con Amazon Music (`com.amazon.mp3`). Al excluirlas, el `AudioRecord` leía silencio aunque sonara música: el DSP se veía "ACTIVO" (procesando ceros, 53% de carga) pero el Atlas mostraba NEUTRAL/SILENCIO 0% y la 4D 0% siempre. Fix: las apps cuyo paquete contiene `"music"` (o Amazon Music, `com.amazon.mp3`) nunca se excluyen del loopback; las de video siguen excluidas. Verificación en dispositivo tras la 370: con música en YouTube Music, el Atlas debe identificar escena (Conf > 0%) y la 4D mostrar anticipación > 0%.
- **IntegrityGate bloqueaba falsamente TODA instalación release — causa raíz de todo-en-cero.** Defecto doble en el paso 4 (hash de la .so): (A) `generateSoHashes` hasheaba la .so de CMake SIN strippear, pero AGP la strippea al empaquetar (quita .symtab) — verificado en el APK 369 real: asset `40c16e7c…` vs .so del APK `dc5dc3d8…`, difieren; (B) `checkSoHash()` hasheaba `nativeLibraryDir/libivanna_omega.so`, pero con `useLegacyPackaging=false` la .so NUNCA se extrae (carga desde base.apk) — verificado en vivo: `lib/arm64/` vacío. Con `isLocked=true`, `nativeInit` nunca corre, el ADE no arranca, el control loop tampoco, y todo queda en 0/SILENCIO (el audio que se oye es passthrough). **Sin este fix, la 370 NO arregla el todo-en-cero.** Fix: `build.gradle.kts` hashea la .so strippeada empaquetada (en release falla si no hay .so strippeada); `IntegrityGate.kt` nuevo `hashInstalledSo()` (nativeLibraryDir o entrada `lib/<abi>/` del APK vía ZipFile); `build.yml` compara hash del asset con hash de la .so empaquetada (guard de regresión). Hallazgos secundarios: el badge "DSP ACTIVE" es engañoso (solo dice que la .so cargó, no que el DSP inicializó); el "DSP Load 53%" era CPU, no DSP.
- **Persistencia `noatsecure` verificada (2026-11-09 01:04 CST).** Tras el reinicio con luz verde, la sonda PAL se carga SOLA al arrancar (probe_mapped=3 en el HAL, sin aplicar la regla en vivo): el `post-fs-data.sh` del módulo `ivanna_audio_probe` funciona. La interposición LD_PRELOAD persiste entre reinicios (contador 32749 → 34124 tras el reinicio).

# v2.5.369 (660) - 2026-11-08

- **Hiper-Operador de Estado Φ (`spatial/HyperStateOperator.hpp`).** Un solo operador muestra a muestra, sin FFT por bloques, que fusiona NPE on-device + EMF (solo medición real) + campo medido por micrófono en una sola ganancia unificada: 24 bandas ERB (60 Hz–16 kHz), modos MEDIDO (SensorFrame fresco <500 ms; Γ por error_db del micrófono ±9 dB/banda ponderado por coherencia) y APROXIMADO (sin sensor; corrección de balance solo-corte [−3, 0] dB, documentada como aproximación, NO medición). `amount=0` = identidad bit-exacta; fail-closed (NaN → identidad, nunca silencia, nunca sube nivel). Cableado en Ruta A (tras HOLOS/ORBE, antes del limiter, con guarda anti-doble-corrección) y Ruta B (`m_hyper` en FusionCore); UI CONTROL con tarjeta HYPER · ESTADO Φ. **ACTIVO por defecto (`amount = 0.5`, Harman 2018): suena desde la primera canción.**
- **Sensor acústico real (`spatial/RealAcousticSensor.hpp`).** Micrófono real (AudioRecord UNPROCESSED con fallback MIC, 48 kHz mono PCM_FLOAT, hilo no-RT), FFT 8192 + hop 4096, 24 bandas ERB, publica `SensorFrame` (magnitude_db, error_db, coherence, level_dbfs) lock-free; bucle de Ruta A = `RoomCorrectionEq` última etapa, bit-exacto si apagado. **Arranca por defecto con la app** (para que Φ corra en MEDIDO; sin permiso RECORD_AUDIO o si el mic falla, Φ cae a APROXIMADO). La corrección de sala en vivo sigue apagada por defecto y requiere acción explícita.
- **Volterra Genezi MHC-PZ1D (kernel medido del PDF).** Kernel H1/H2 del Sony Genezi MHC-PZ1D (ESS Farina) en formato IVK1 (`kernels/genezy_mhc_pz1d.ivk1`, 8616 B, SHA-256 `a6883e5f…`): h1 normalizada por L1 (DC 3.07 → 0.699, −3.1 dB), puerta de producción aceptada en Python y C++ (límites NO tocados). Cableado en Ruta A (JNI + Kotlin, asset → `files/ivanna_omega/kernels/`) y Ruta B (omega_effect, init junto al HRTF). **ACTIVO por defecto:** el kernel se carga al arrancar y Volterra queda encendido salvo que el usuario lo apague (persistido). Honesto: kernel tabulado del PDF, NO re-medición en vivo.
- **Volterra PZ1D REFINADO — kernel DEFAULT (cadena G85 + DAC + Genezi).** Caracterización electroacústica integral del Moto G85 5G + DAC USB-C + Sony Genezi MHC-PZ1D (`kernels/moto_g85_pz1d.ivk1`, 8616 B, SHA-256 `0fc035c1…`, generador `tools/volterra_h2/build_moto_g85_pz1d_ivk1.py`, doc `docs/MOTO_G85_PZ1D_KERNEL.md`): h1 re-tabulada (L1 5.00097, DC 3.76669; normalizada ÷L1 → DC 0.753, −2.46 dB), h2 **solo diagonal** (16 coef, sin sub-diagonal). Device `MOTO_G85_PZ1D` autorizado **explícitamente** en la puerta de producción junto a `MOTO_G85_GENEZY` (no es comodín; un tercero sigue rechazado). El Genezi anterior queda como alternativo. **Motor optimizado (`VolterraH2Symmetric`):** H2 disperso (solo coeficientes no nulos: 16 MACs/muestra en vez de 2080), H1 con NEON en ARM (dot product en bloques de 4, guardado, fallback escalar sin módulos en host), línea de retardo circular O(1) (ya lo era; el desplazamiento O(K) del mensaje NO se copió). El formato repo no empaqueta scale_factor/soft_ceiling del mensaje: el saturador del motor (rodilla 0.90 / techo 0.995) + envelope ya dan la seguridad fail-closed. **Throughput host con este kernel: 1.5× RT → ~68× RT (~45×).** Tests nuevos `test_volterra_moto_g85_pz1d` 10/10 (puerta C++, paridad optimizado-vs-escalar con ruido determinista, throughput). La CPU/latencia en dispositivo sigue sin medirse.
- **Curvas élite (`spatial/EliteTargetCurves.hpp`).** Harman AE/OE 2018 como default (única curva con respaldo experimental sólido: Olive/Welti/Khonsaripour, AES 2018), variante élite-lean, campo difuso H&M y altavoces en sala; control de graves ±3 dB.
- **ORBE ACTIVO por defecto (`amount = 1.0`).** Limpio (THD+N −139.33 dB encendido @1 kHz, 23× RT), sin retuneo. ORBE y HOLOS ya no son excluyentes: corren en serie (ORBE primero, HOLOS después) en las tres rutas de proceso (`nativeProcess`, `nativeProcessBlock`, `IvannaFusionEngine::process`), cada uno guardado por su propio `if (amount > 0)`. Defaults en `ParameterStore` + UI CONTROL.
- **HOLOS ACTIVO por defecto — CON RETUNEO MEDIDO (`amount = 0.5`, `harmonics = 0.0`).** Los defaults viejos distorsionaban (−31.5 dB THD+N, IMD −28.2 dB) y encenderlo a lo bruto habría sido activar distorsión audible. Barrido host en rejilla amount × harmonics (test nuevo `test_holos_retune_sweep.cpp`): el término K domina la distorsión; cualquier harmonics > 0 incumple −70 dB. Solo harmonics = 0 cumple. Default elegido: amount 0.5, harmonics 0.0 → THD+N **−94.0 dB @1 kHz, −79.6 dB @100 Hz**, IMD **−159.5 dB**; el efecto espacial sigue siendo medible (diferencia vs bypass, latencia compensada: −44.0 dB). Throughput sigue 3.5× RT (el retuneo no lo cambia) — riesgo a medir CPU/XRuns en el G85.
- **Paridad Ruta A (auditoría v2.5.369, `docs/ROOT_VS_NOROOT_CAPABILITIES.md`).** La cadena DSP completa ya corría en Ruta A y la mayoría de los parámetros ya tenían espejo local verificado: WFS/crosstalk/upmixing/4D/ejes supremos (módulos en `nativeProcessBlock`, gobernados por flags locales del restorer), estado perceptual (setters JNI locales), pinna (matching de sujeto HRTF en proceso), perfil por ruta (`nativeSetRouteProfile` → control frame), estado adaptativo + YAMNet (motor adaptativo local + `control_set_yamnet_scores` → `control_apply_frame`). `RouteCapabilities.daemonOnlyFeatures` reducida a 2 ítems que de verdad solo existen en el daemon: **intensidad global del motor** (sin equivalente local; exige decisión de diseño) y **procesado de todas las apps** (límite inherente a la captura).
- **Ruta B / daemon PERFECTO (`docs/RUTA_B_DAEMON_FIX.md`).** El módulo Magisk fusiona omega en TODAS las configuraciones de efectos (genérica de vendor + SKU blair/holi + `/system/etc`), no solo en la primera: el HAL ya no puede leer una config sin el UUID. La app reintenta crear el efecto en cualquier sesión (incluidas las ya abiertas) con backoff exponencial (2 s→30 s, sin rendirse) y desenmascara la causa real del fallo del constructor (antes solo "null"). Handshake sólido: `ROOT_DAEMON` exige efecto confirmado (beacon ENABLED_IDLE/PROCESSING); con daemon vivo pero sin efecto, el modo es `ROOT_NO_DAEMON` y el fallback local sigue procesando (fail-closed). Banner honesto según daemon vivo o caído. Tests JVM nuevos `RouteBRecoveryTest` (10 tests). Pendiente verificar en el G85 que el efecto se crea y `dsp_frames` crece.
- **Refactor visual de la UI (Aurora Obsidiana).** Pies de ORBE/HOLOS corregidos con los hechos (activos por defecto, con sus THD+N medidos) en vez de la mentira de la puerta GREEN. El distintivo TinyML ahora depende de si la biblioteca nativa está cargada. Nueva línea de estado Volterra en vivo (kernel PZ1D cargado/activo, sin kernel, apagado). El sensor acústico gana superficie: estado MEDIDO/APROXIMADO en vivo en la tarjeta HYPER (con nº de frame como evidencia) + botón de activación/permiso de micrófono. El héroe muestra píldoras ORBE/HOLOS/Φ/VOLTERRA solo cuando suenan de verdad. Banner de ruta reescrito en la paleta del tema (sin colores Material genéricos). Los hubs SPATIAL/SYSTEM ganan un icono real por tarjeta (antes todas con el mismo punto). Panel de motor adaptativo reescrito en constantes del tema. Sin selector de kernel PZ1D/Genezi (deliberado: cambiar de kernel exige vía nativa de reemplazo; pendiente).
- **Toda la interfaz en una sola paleta.** El resto de la app vivía en una paleta paralela slate/tailwind (165 colores hardcodeados fuera del tema). Ahora hay 0: todos los literales viven en `IvannaTheme.kt`. Nuevos componentes compartidos `IvannaScreenHeader` e `IvannaTabRow` (SONIDO/SISTEMA/CEREBRO con su acento cada uno). Migración mecánica slate→Aurora Obsidiana en 25 archivos (paneles de inteligencia musical y 4D, reproductor, layout de altavoces, foto holo, perfil Genezi, excitador —con nueva constante `ExciterOrange`—, lab, experiencia auditiva, dashboard cognitivo, calibrador ISO 226, cerebro perceptual, asistente, panel espacial, dashboards, OEM). Tiers térmicos sin emoji (el color ya comunica el estado). Sin cambio de lógica ni bindings.
- **Kernel evolutivo `EvolutionaryEQ` REPARADO y refinado (`docs/EVOLUTIONARY_EQ.md`).** Estaba roto en 5 frentes medidos: (1) calibración NO determinista (dos calibraciones daban FIR distinto, max|ΔFIR| 0.016; Ruta A ≠ Ruta B); (2) objetivo incoherente — curva paramétrica "presencia" que contradecía Harman 2018 (Δ 8.5 dB a 3.67 kHz) y no medía nada < 458 Hz; (3) convergencia pobre (fitness −20.25 → −4.20 en 48 generaciones, sin converger); (4) calibración lenta (~967 ms, bloqueaba `initSpatial` de Ruta B ~1 s); (5) normalización por pico (+3 dB) incompatible con el pico Harman (+8.6 dB). Reparado: RNG determinista (semilla 1337, genoma neutral; max|ΔFIR| = 0.000000, idempotente), objetivo = **Harman 2018 ×0.5** (13 anclajes de `EliteTargetCurves`, única fuente de verdad; complementa el amount 0.5 de Φ → Harman completo en defaults), convergencia real (adaptación de paso por éxito, softmax T=0.5, λ=16, 150 generaciones; fitness −61.8 → −3.64, error máx 0.84 dB), fitness rápido por matriz precomputada (**calibración 967 ms → 39 ms**, 25×), normalización por ancla de 1 kHz (|H(1 kHz)| = 1.0, referencia Harman). Piso honesto −2.0 dB a 16 kHz (límite del FIR 256 taps; el objetivo −4.15 dB era inalcanzable). Ruta B: `setEvoEqEnabled()` (antes el toggle mentía en Ruta B). Ruta A: `resetHistory()` al reactivar (el historial FIR obsoleto convolvía con audio viejo). THD+N ~−120 dB @1 kHz. Tests nuevos 6/6 (determinismo, convergencia Harman ±1 dB, calibración <500 ms).
- **Fix CI: validación Gradle 8.9 sobre `generateSoHashes` (CI run 42567801291).** El fix Kotlin UI SÍ funcionó (`compileReleaseKotlin` pasó, 0 errores de UI, nativo OK) y el build avanzó hasta Lint, donde falló con `WorkValidationException`: la tarea `generateSoHashesRelease` escribe `ivanna_so_hashes.txt` y los Lint (`lintVitalAnalyzeRelease`, `generateReleaseLintVitalReportModel`) lo leen sin declarar dependencia. Fix: en `app/build.gradle.kts`, dentro de `onVariants` junto a la regla existente de `merge<Variant>Assets`, se declaran las dependencias de ambos Lint sobre `genTask` con el mismo patrón perezoso `tasks.matching { }.configureEach { }` (las tareas Lint pueden no existir aún en `onVariants`). Nota: las anotaciones solo mostraban errores de metadata Kotlin (compilador 2.0.0 vs deps 2.2.0–2.4.0); esos NO causaron el fallo (`compileReleaseKotlin` pasó), pero es un desajuste a vigilar.
- **Limpieza (NO era la causa de la CI): VLA en `room_model.cpp` eliminado.** El diagnóstico inicial apuntó al VLA (`alignas(16) float fbuf_in[samples]`) por las anotaciones del check-run, pero era un señuelo: el VLA es idéntico en la 368 (que pasó), es solo un warning (CMake solo tiene `-Werror=return-type`), y no es el error. Aun así se eliminó (C++ estándar, procesado en chunks de 1024 con buffer fijo alineado a 16 bytes; salida bit-exacta vs la original). El fallo de "Tests host" es el preexistente cppcheck+gitleaks, no bloquea el release.
- **Protección anti-piratería fase 1 y 2.** Integridad (SHA-256, pin de certificado, vinculación ANDROID_ID, self-check), endurecimiento de build, R8 dirigido; ofuscación nativa (XorString, predicados opacos, control-flow flattening), anti-debug/anti-tamper/anti-hook, integridad en runtime (hash de .so vs asset de build, verificación del módulo Magisk por el daemon), ofuscación del dex. Límite honesto: la imposibilidad absoluta no existe en un dispositivo que el atacante controla; el objetivo es que sea prácticamente inviable.
- **Verificación host:** CTest 839/839 (100%, 0 fallos) — operador 20/20, curvas 8/8, sensor 10/10, Genezi 7/7, PZ1D 10/10, retuneo HOLOS 2/2, seguridad 15/15 + 14/14. Throughput host Φ 41–42× RT; Volterra con kernel PZ1D ~68× RT (antes 1.5× RT con el bucle denso). README actualizado a v2.5.369.
- **Pendiente en dispositivo:** medir CPU/XRuns/temperatura con Φ 0.5 + Volterra PZ1D + ORBE 1.0 + HOLOS 0.5 en el G85 (throughput HOLOS 3.5× RT es riesgo) y puerta auditiva ABX. El combinado todavía no se ha escuchado en el teléfono.

# v2.5.368 (659) - 2026-11-08

- **Pilares pendientes — lo posible, construido (`docs/PILARES_PENDIENTES.md`).** (1) **Volterra K=4096, resuelto** con `neuromorphic/diagonal_volterra.hpp`: polinomio de memoria (diagonal pura, modelo estándar de DPD; DDR de Ghannouchi/Hammi) — y[n] = Σ h1[k]·x[n-k] + Σ h2d[k]·x[n-k]² — coste O(K): 8,192 coef., ~0.79 GMAC/s en estéreo @48 kHz, frente a 8,390,656 coef. del H2 completo. Junto con `VolterraH2Symmetric` K=64 (cruzados, memoria corta) forma el DDR de 2º orden del repo. Solo coeficientes medidos; `setCoefficients()` rechaza no finitos y |c|>8. (2) **Back-EMF, estimador construido** con `spatial/EmfEstimator.hpp`: Thiele-Small V = Re·I + Le·dI/dt + Bl·v → v y desplazamiento por muestra, verificado con V/I sintéticos (error <5%); la medida requiere hardware con sensado de corriente (nunca el Aux). (3) **Colapso de paredes, formulado:** control de campo multipunto por mínimos cuadrados con regularización, q = (HᴴH + βI)⁻¹Hᴴd, extensión del WFS existente; requiere array + micrófonos, no implementado (módulo MIMO con su protocolo). (4) **Elevación por altavoces, construida** con `spatial/PinnaElevationEq.hpp`: bandas direccionales de Blauert (frente 500 Hz/4 kHz, atrás 1 kHz/10 kHz, arriba 8 kHz), presets Above/Front/Behind/Flat, ±3 dB máx., bypass bit-exacto. (5) **Zero-PCM/DSD, construido** con `spatial/DopPacketizer.hpp`: DoP v1.1 real (marcadores 0x05/0xFA, 16 bits DSD por muestra de 24 bits, portadora DSD/16: 176.4/352.8/705.6 kHz); necesita fuente DSD y DAC DoP; la conversión PCM→DSD (sigma-delta) queda fuera.
- **Verificación host:** tests nuevos `test_pilares_pendientes` 11/11 (ASan+UBSan limpios); fusión 7/7, AUX 6/6, neuro 11/11 sin regresión.
- **Pendiente:** push a main + releases v2.5.365–v2.5.368 por bundle (OAuth roto para el usuario; token expuesto revocar).

# v2.5.367 (658) - 2026-11-08

- **Fusión Homogénea Φ — Hiper-Operador de Estado (`spatial/HomogeneousFusion.hpp`).** Un solo coordinador con un solo estado y un solo `update()` por bloque, en lugar de tres piezas sueltas: Φ = Γ ∘ K ∘ G ∘ XTC ∘ AUX ∘ N (HOLOS Green/Volterra/gain-field + XTC + campo AUX + bucle neuro). `ingestNeuro()` delega al `ClosedLoopController`, `setRoutePreset()` guarda el preset AUX completo, `loadVolterraPackage()` valida con `AuxVolterraGate` y guarda solo el kernel medido, `emfVector()` devuelve siempre identidad (`Z_emf` no disponible: Back-EMF por Aux imposible), y `update()` aplica la ganancia neuro en muestras con `NeuroBandEq` y entrega los objetivos HOLOS/ORBE ajustados sin reescribir la base. `FusionStatus` dice cada bloque qué vectores están vivos. No es un tensor continuo sin discretizar: la señal sigue siendo PCM en bloques de 128 y HOLOS en STFT (10.7 ms); no hay "sin Fourier" ni "sin PCM" en este stack.
- **Integración documentada, no cableada todavía (`docs/HOMOGENEOUS_FUSION.md` §3):** el módulo sustituye `updateNeuroLoop()` + `setAuxGeneziPreset()` en cada ruta con el patrón exacto (1 archivo por ruta). No se cableó en v2.5.367 a propósito: duplicar la ganancia neuro (fusión + bucle viejo a la vez) sería un bug; la migración usa los tests de este módulo como red.
- **Los 4 paradigmas siguientes, con investigación real revisada hoy (`docs/HOMOGENEOUS_FUSION.md` §4):** (1) tFUS es neuromodulación médica en ensayos (UCSF MESSI-LEO NCT07710989, diadema wearable, fase 1 oct 2026) y el oCI optogenético es preclínico (Moser/Gotinga, Hernandez 2014, Dieter 2019, Alekseev 2025, f-Chrimson 2026) y exige terapia génica AAV + implante — ninguno es una interfaz de audio que una app pueda manejar. (2) La teleportación perceptual no existe; lo real es HOA remota + binaural. (3) La sonificación de datos es real; "oír 4D/5D" no tiene evidencia. (4) Las interfaces cerebro-cerebro existen en laboratorio (BrainNet 2019); el espacio colectivo sub-milimétrico no existe ni como experimento.
- **Verificación host:** tests nuevos `test_homogeneous_fusion` 7/7 (ASan+UBSan limpios); AUX 6/6 y neuro 11/11 sin regresión.
- **Pendiente:** push a main + releases v2.5.365–v2.5.367 en GitHub (bloqueado: falta auth GitHub del usuario; el token pegado en el chat no se usa ni se guarda — revocar).

# v2.5.366 (657) - 2026-11-08

- **Ruta AUX Genezi / auto — DAC USB-C + Auxiliar 3.5 mm (`spatial/AuxGeneziRoute.hpp`, `IvannaFusionCore.{h,cpp}`, `jni/ivanna_omega_jni.cpp`, `IvannaNativeLib.kt`).** Presets completos cableados en las dos rutas: **GeneziRoom** (Sony Genezi en habitación: XTC span 0.90 m / dist 0.85 m, AUX xover 120 Hz, coherencia 1.0) y **CarCabin** (autoestéreo, asiento del conductor LHD: XTC span 1.40 m / dist 0.80 m / offset −0.30 m, AUX xover 100 Hz, coherencia 1.0). Ruta B: `IvannaFusionEngine::setAuxGeneziPreset(int)` (geometría `m_xtc` + `setAuxField`); Ruta A: `nativeSetAuxGeneziPreset(int)` (geometría a la cadena 4D vía `fourd::setSpeakerGeometry` + atomics AUX + daemon + snapshot, igual que `nativeSetAuxField`). **AuxVolterraGate:** la pre-distorsión Volterra H2 solo acepta kernel medido del sistema real (paquete IVK1, dataset `GENEZY01`, K=64, fs 48 kHz, dispositivo `MOTO_G85_GENEZY`); rechaza dataset público `DATASET0` siempre, sintético sin flag, K distinto, dispositivo distinto, CRC y presupuestos H2. Sin kernel medido, el motor queda en identidad.
- **Veredicto honesto de los 5 pilares (en código y en `docs/AUX_GENEZI_ROUTE.md`, no solo en el doc):** (1) Volterra K=4096 inviable en tiempo real — H2 es O(K²): 8,390,656 coef. (33 MB), ~806 GMAC/s en estéreo @48 kHz; el límite real es K=64. (2) Back-EMF por el cable Aux **imposible**: la salida 3.5 mm es analógica de solo salida, sin camino de retorno, y USB Audio Class no expone sensado de carga; la calibración real es por micrófono (`RoomProbe`, 50 Hz–6 kHz). (3) "Colapso de paredes" no euclidiano **imposible**: dos fuentes no cancelan reflexiones en todas las posiciones; lo real es XTC de diafonía directa + graves coherentes + EQ de sala medido. (4) Elevación por altavoces **solo en parte**: las pistas de pinna existen (HRTF KEMAR/CIPIC), pero por altavoces son débiles y dependientes del oyente; además en Ruta B el wet HRTF se fuerza a 0 con AUX activo (binaural = audífonos), así que el preset no finge un wet que la cadena ignoraría. (5) Zero-PCM/DSD directo **no existe en este stack**: AudioFlinger y USB Audio Class son PCM y el Aux es analógico después del DAC; lo real son filtros de fase mínima y bloques de 128 muestras.
- **Verificación host:** tests nuevos `test_aux_genezi_route` 6/6 (ASan+UBSan limpios); `test_neuro_acoustic_closed_loop` sigue 11/11; `IvannaFusionCore.cpp` sintaxis host limpia; `ivanna_omega_jni.cpp` 18 errores, los mismos 18 que en HEAD (stub host de `jni.h`, preexistente; 0 nuevos, ninguno menciona AuxGenezi). Sin UI todavía (el preset se activa por JNI); sin kernel medido `GENEZY01` cargado; sin medición en el Moto G85 (CI valida Kotlin/NDK).
- **Pendiente:** push a main + release v2.5.366 en GitHub (bloqueado: falta auth GitHub del usuario; el token pegado en el chat no se usa ni se guarda — revocar).

# v2.5.365 (656) - 2026-11-08

- **Interfaz Neuro-Acústica en Bucle Cerrado v2 — cableada a la ruta de audio (`neuromorphic/neuro_acoustic_closed_loop.hpp`, `IvannaFusionCore.{h,cpp}`, `jni/ivanna_omega_jni.cpp`).** El controlador (`ClosedLoopController`, firma por usuario en 8 bandas Greenwood + LIF real del NPE) ya no vive solo: corre 1× por bloque ANTES de HOLOS/ORBE en las dos rutas. **Ruta B:** `IvannaFusionEngine::updateNeuroLoop()` (miembro `m_neuroLoop_` + `m_neuroBandEq_`, preparados en ctor/`initSpatial`) con API `setNeuroLoopEnabled` / `ingestNeuroFrame` / `neuroLoopStatus`. **Ruta A:** estáticos `g_neuroLoopRouteA` / `g_neuroBandEqRouteA` en `ivanna_omega_jni.cpp`, con `neuroLoopRouteAUpdate()` en `nativeProcess` y `nativeProcessBlock`, antes de `g_holosRouteA`/`g_orbeRouteA`. En ambas, la ganancia por banda se aplica en muestras con el nuevo `NeuroBandEq` (8 biquads peaking RBJ, CF Greenwood, Q coclear; bypass bit-exacto si |ganancia| < 0.01 dB) y los parámetros HOLOS/ORBE se modulan con `applyAdjustments()` sobre copias locales `hp`/`op` — los atomics del usuario (`g_holos_*`/`g_orbe_*`) son la base y **nunca se reescriben**.
- **NADA SIMULADO: `SimulatedNeuroSensor` eliminado.** `NeuroSensorKind::Simulated` ya no existe; `ingest()` rechaza cualquier kind fuera de {EEG en auricular (2), ABR (3), fNIRS (4)}. Los tests usan frames explícitos con kind real (fixtures, no un sensor). Reloj único: `monotonicNowUs()` (CLOCK_MONOTONIC); en Kotlin `SystemClock.uptimeMillis()*1000` — un timestamp en otro dominio se ve obsoleto/futuro y el bucle no aprende.
- **JNI + gateway Kotlin (Ruta A):** `nativeSetNeuroLoopEnabled`, `nativeNeuroLoopIngest(kind, timestampUs, response[8], coherence[8], quality, abrMs, fnirsHbO)`, `nativeNeuroLoopStatus()` en `IvannaNativeLib.kt`, y `neuromorphic/NeuroSensorGateway.kt` (registra un `Driver` de hardware real, valida y reenvía; sin driver, `enable()` devuelve false y el bucle sigue apagado). La firma vive solo en RAM, no se persiste; la activación es por acción explícita del usuario.
- **Apagado por defecto y fail-closed:** sin frames frescos (>250 ms), con calidad/coherencia bajas o datos inválidos, los ajustes decaen a identidad (0 dB, 0 rad, wet 0). Slew por bloque (ganancia ≤0.1 dB, fase ≤0.02 rad, wet ≤0.02). Cero malloc en el hot path.
- **Verificación host:** tests del bucle 11/11 (8 bucle + 3 NeuroBandEq, ASan+UBSan limpios); `IvannaFusionCore.cpp` sintaxis host limpia; `ivanna_omega_jni.cpp` 18 errores, los mismos 18 que en HEAD (stub host de `jni.h` incompleto, preexistente; 0 nuevos). Puertas: `check_header_wiring`, `check_rt_safety` (36/36), `check_audio_route_wiring`, `check_build_flags` PASS; `check_jni_wiring` pasa para los 3 símbolos nuevos (su único hallazgo es preexistente: `nativeSetOrbeEnabled` sin llamador).
- **Pendiente honesto (no en v2):** ningún driver de sensor real está enchufado (sin driver no llegan frames y el bucle no se activa); no hay UI (switch) todavía — `check_kotlin_wrapper_wiring` reporta los 4 métodos del gateway sin llamador Kotlin hasta que el driver o la UI existan; el transporte de frames a **Ruta B** queda pendiente (falta el campo neuro en el snapshot del daemon; las JNI alimentan hoy solo la Ruta A). Sin medición/escucha en el Moto G85 todavía; el Kotlin y el NDK los valida CI.

# v2.5.364 (655) - 2026-11-07

- **Nuevo efecto: HOLOS · Operador Único de Campo Acústico (`spatial/HolosOperator.*`, `HolosGreenKernel`, `HolosVolterraKernel`, `HolosGainField`, `HolosStateEstimator`).** Las 12 funciones del hilo de audio no son 12 matemáticas distintas: son 3 operadores escritos 12 veces. HOLOS los evalúa de una vez, por banda ERB (24) en STFT hop 128 / FFT 512, desde un solo vector de estado por banda: `Y_b = G_b(s_b) ⊛ [K_b(s_b) · X_b] · Γ_b(s_b)`. `G_b` es la función de Green unificada (HOA+WFS+RIR+HRTF+ORBE+crossfeed: ITD/ILD esféricos de campo cercano, pinna medida, distancia `(1/r)^γ`, difuso); `K_b` es el kernel de Volterra por banda (Chebyshev T2, GoldenEar `2x²`, Padé/tanh y OHC coclear son el mismo monomio con o sin memoria); `Γ_b` es el campo de ganancia único (EQ, compresor, limiter, M/S, trims, tilts). Cada módulo legacy es un caso degenerado de HOLOS (ORBE *es* HOLOS con K = identidad y Γ = 1). Entra en `FusionCore` en el lugar de ORBE (tras el blend WFS, antes del M/S), con crossfade 15 ms y fail-closed por término.
- **UI: sección "HOLOS · CAMPO UNIFICADO" en CONTROL** tras ORBE: 11 sliders (cantidad, distancia, apertura, proximidad, difuso, armónicos, calidez, dinámica, ancho, divergencia γ, sala RT60) + switch de head-tracking + switch "12 BANDAS (AHORRO CPU)". Los 13 parámetros persisten en `ParameterStore` y se reaplican al abrir (JNI Ruta A + `SET_PERCEPTUAL_STATE` al daemon). El EQ estático de Γ está cableado a JNI/snapshot/daemon/Ruta B con defaults planos, sin controles de UI en v1.
- **Ruta B deja de estar en off por defecto (params):** snapshot ABI v9 -> **v10** (`OmegaDspSnapshot`, 536 -> 616 bytes) con 19 campos `holos_*` (13 params + EQ de Γ); el daemon los publica desde `OmegaDspState`/`SET_PERCEPTUAL_STATE` y `omega_apply_snapshot` los reenvía a `IvannaFusionCore::setHolosParams`/`setHolosEq` (clamp defensivo; snapshot v9 viejo rechazado por versión, fail-closed a defaults). Shed térmico de HOLOS por tiers en Ruta B (FULL/REDUCED -> Normal, LIMITED -> Warm, PROTECTED -> Limited, BYPASS -> Bypass; `cpuShed` fuerza >= Limited). Ruta A no tiene lectura de tier del governor en JNI: su shed sigue siendo manual (documentado).
- **README reescrito completo contra el código** (401 líneas, antes desactualizado desde v2.5.212): cadena de señal verificada, 12 funciones con matemática y estado, ORBE y HOLOS con mediciones reales, Rutas A/B con ABI v10, puerta auditiva e Índice VISCERAL, y límites honestos. Sin copiar `ARCHITECTURE_OMEGA_SUPREME.md` (aspiracional).
- **Apagado por defecto en todo:** UI, ParameterStore, snapshot y Ruta B con amount 0; sin tocarlo no cambia nada de lo que suena. Sin puerta auditiva GREEN ni medición/escucha en el Moto G85 todavía; el Kotlin y el NDK los valida CI (build APK + artefactos).
- **Banderas rojas honestas (medido en host, 48 kHz):** THD+N off −153.5 dB @1 kHz (bit-exacto); on **−31.5 dB @1 kHz** con defaults (el término armónico K aporta ~56 dB de distorsión; sin K −87.9 dB), IMD −28.2 dB — los defaults hay que retunearlos antes de cualquier escucha. Latencia 512 muestras = 10.7 ms. **Throughput host 3.5× RT** (muy por debajo de la estimación de diseño 15–25× RT): la viabilidad en el Moto G85 está en cuestión hasta medir CPU/XRuns en dispositivo. Host: HOLOS 38/38 + ORBE vecinos 12/12 + ABI 4/4 + daemon 32/32 (105 tests, 0 fallos); `IvannaFusionCore.cpp`, `omega_effect.cpp`, `command_server.cpp` sintaxis host limpia (el JNI solo falla en el stub host preexistente).
- **Investigación FRONTERA (fuera del repo, `analysis-ivanna/FRONTERA.md`):** el objetivo visceral definido en 5 dimensiones medibles (Índice VISCERAL) y 3 direcciones con probabilidad real de física nueva — kernel de Volterra por banda medido del Moto G85, ley psicofísica de la envolvencia, modelo cabeza+torso en campo cercano. Nada de eso existe aún como código; es el plan experimental siguiente, con criterios de muerte y ABX ciego con niveles igualados.

# v2.5.363 (654) - 2026-11-07

- **UI: sección "ORBE · CAMPO CERCANO" en CONTROL** para el Binaural Esférico de Campo Cercano de v2.5.362 (rama feature): sliders de cantidad 0-100 % (0 = apagado, bypass bit-exacto) y distancia 0.15-2.0 m como principales, más apertura, proximidad de graves 0-6 dB, difuso→sala y toggle de seguimiento de cabeza. Todo persistente en `ParameterStore` (`orbe_amount`, `orbe_distance_m`, `orbe_spread`, `orbe_proximity_db`, `orbe_diffuse`, `orbe_head_tracking`) y reaplicado al abrir por `PersistedStateRestorer` (JNI + `SET_PERCEPTUAL_STATE` al daemon). Cableado igual que el crossfeed (panel -> ControlTabScreen/MainActivity -> `IvannaNativeLib.nativeSetOrbeParams`), con `runCatching` para builds sin el símbolo.
- **Ruta B deja de estar en off por defecto:** snapshot ABI v8 -> **v9** (`OmegaDspSnapshot`, 512 -> 536 bytes) con campos `orbe_*`; el daemon los publica desde `OmegaDspState`/`SET_PERCEPTUAL_STATE` y `omega_apply_snapshot` los reenvía a `IvannaFusionCore::setOrbeParams` (clamp defensivo; snapshot v8 viejo rechazado por versión, fail-closed a defaults). Tests de ABI actualizados (versión 9, <= 544 B) y aserciones de defaults ORBE en `test_upmixing_control_plane`.
- **Cableado restante:** ORBE también en `nativeProcessBlock` (Ruta A/C, tras WFS, antes de RIR; comparte `g_orbeRouteA` bajo el mutex DSP); shed térmico de ORBE cableado al ThermalGovernor de Ruta B (`tier >= LIMITED` -> `setOrbeThermalShed`, mismo criterio que NPE/Volterra). Ruta A no tiene lectura de tier del governor en JNI: su shed sigue siendo manual (documentado).
- **Apagado por defecto en todo:** UI, ParameterStore, snapshot y Ruta B con amount 0; sin tocarlo no cambia nada de lo que suena. Sin puerta auditiva GREEN ni medición/escucha en el Moto G85 todavía; el Kotlin y el NDK los valida CI (build APK + artefactos). Host: ORBE 10/10 + cadena 2/2, baseline 10/10, `check_audio_route_wiring.py` PASS; `IvannaFusionCore.cpp`, `omega_effect.cpp`, `command_server.cpp` sintaxis host limpia (el JNI solo falla en el stub host preexistente, líneas 3606+).

# v2.5.362 (653) - 2026-11-07

- **UI: control "CRUCE DE GRAVES · AUDIFONOS / DAC" en CONTROL** para el crossfeed con ITD de v2.5.361: slider 0-100 % (0 = apagado, bypass exacto), persistente en `ParameterStore` (`crossfeed_strength`) y reaplicado al nativo por `PersistedStateRestorer` al abrir la app. Cableado igual que Impacto predictivo (panel -> ControlTabScreen/MainActivity -> `IvannaNativeLib.nativeSetCrossfeed`), con `runCatching` para builds sin el simbolo.
- Apagado por defecto: sin tocarlo no cambia nada de lo que suena. Aviso en la tarjeta: pensado para audifonos/DAC, no combinar con altavoces/transaural. Sin escucha ni medicion en el Moto G85 todavia; el Kotlin lo valida CI (Build APK).

# v2.5.361 (652) - 2026-11-07

- **Nuevo efecto: crossfeed de graves con ITD (`dsp/HeadphoneCrossfeed.hpp`, Ruta A).** Con audifonos/DAC cada oido oye solo su canal en graves (separacion infinita), lo que ubica el sonido "dentro de la cabeza". El efecto mezcla los graves hacia el otro oido con retardo de 0,261 ms (ITD de Woodworth a 30 grados, altavoces a +-30) y pasa-bajos de 700 Hz: `out_L = L + g(Rd-Ld)`, `out_R = R - g(Rd-Ld)`. Propiedades verificadas en tests: strength 0 = bypass bit-exacto; L==R sale identico (centro y mono intactos); con un canal solo, el cruce coincide con la prediccion analitica del filtro (g=1/3: -6 dB a 100 Hz, antes separacion infinita); agudos sin tocar; retardo de ~12,5 muestras @48 kHz; estable y acotado de 44,1 a 384 kHz.
- **Estado honesto:** conectado tras el widener en `nativeProcess` y expuesto como `IvannaNativeLib.nativeSetCrossfeed(0..1)`, **apagado por defecto** (no cambia nada de lo que suena hoy) y **sin control en la UI todavia**. No hay medicion ni escucha en el Moto G85; no se activa con HRTF/transaural a la vez sin decision de enrutamiento. 688/688 pruebas nativas de host pasan; el JNI se comprobo sintacticamente en host y lo valida CI arm64.

# v2.5.360 (651) - 2026-11-07

- **Compresor (Ruta A y Ruta B, C++):** el detector era un seguidor de picos de |x| con ataque de 5 ms; su rizado a 2f modulaba la ganancia dentro del propio ciclo y generaba armonicos. Medido con el arnes de Fase 1 (host x86, senoidal a -3 dBFS con 1-3 dB de reduccion): THD+N 150 Hz -61 -> -76 dB, 200 Hz -63 -> -81 dB, 500 Hz -71 -> -96 dB, 1 kHz -76 -> -108 dB, 5 kHz -90 -> -132 dB. La reduccion estatica de ganancia es identica (-1,92 dB a 1 kHz) y la liberacion no cambia; el ataque efectivo suma 3 ms (segundo one-pole de 3 ms sobre la envolvente).
- Cobertura: `test_measure_baseline` fija suelos de THD+N por frecuencia; 681/681 pruebas nativas de host pasan. No hay medicion en el Moto G85: el efecto en escucha real queda por comprobar.

# v2.5.359 (650) - 2026-11-06

- **Volterra H2 — puerta de carga de kernels en el motor (C++):** nuevo `neuromorphic/volterra_kernel_package.hpp` valida el MISMO contenedor IVK1 que produce `tools/volterra_h2/kernel_loader.py` (magic, versión, CRC32 de `device_id`+coeficientes, tamaño exacto, fs, K, `device_id`, finitud, amplitud, l1(h1)≤1 que acota |H1|, presupuesto de H2). Devuelve un código de estado por causa (`VolterraLoadStatus`) en lugar del rechazo silencioso de `updateKernels()`.
- **Procedencia:** el dataset público REW (`DATASET0`) se rechaza SIEMPRE (es de otro sistema); `SYNTH000` solo con `allow_synthetic` (pruebas); `GENEZY01` queda reservado a una medición física futura según `GENEZY_CHARACTERIZATION_PROTOCOL.md`. IVK1 no cubre K, fs ni dataset_id con el CRC (limitación documentada); se compensa con tamaño exacto e igualdad contra la política.
- **Motor:** `VolterraH2Symmetric::updateKernelsChecked()` y `loadKernelPackage()`; `updateKernels()` conserva su semántica exacta (delegando). Ante cualquier error la carga deja el motor en bypass (`setEnabled(false)`) sin tocar los kernels aplicados. Cargar un paquete no activa el motor.
- **Pruebas:** 18 pruebas nuevas (`test_volterra_kernel_package`), incluido un paquete generado por Python aceptado por C++ y byte-idéntico al construido en C++, paridad bit a bit con `updateKernels()`, bypass tras fallo en marcha, buffer no alineado e identidad exacta por defecto; `test_kernel_loader.py` +1 (`dataset_id`).
- **Alcance honesto:** NO existe ningún kernel medido del sistema Moto G85+DAC+Genezy; no hay consumidor de producción cableado (JNI/Kotlin) y la producción sigue en identidad (passthrough exacto). Esta versión no cambia el sonido; deja lista y verificada la ruta de carga. Auditoría: `docs/research/volterra_h2_public_dataset/UPDATEKERNELS_INTEGRATION_AUDIT.md`.

# v2.5.358 (649) - 2026-11-05

- **Ruta A — EQ evolutivo con respuesta espectral efectiva:** se corrige el colapso práctico a identidad. El regularizador de continuidad bajaba el fitness tanto que anulaba el error respecto a la curva objetivo; además, la población no incluía el genoma vigente aunque el comentario afirmaba lo contrario. El peso de suavidad baja de 0.5 a 0.01, el paso de búsqueda sube de 0.1 a 0.5 y el genoma actual se conserva como elite.
- **Impacto esperado y acotado:** la respuesta deja de quedarse en ±0.4 dB y se aproxima al perfil declarado (recorte de graves y presencia/aire), con pico objetivo limitado por la normalización existente. No se aumenta el volumen maestro ni se desactiva el SafetyLimiter.
- **Regresión:** nuevo test nativo mide la respuesta FIR en 458 Hz, 6.9 kHz y 13 kHz; falla si el EQ vuelve a identidad o excede el límite de presencia. CI y validación auditiva en Moto G85 siguen pendientes; el perfil no se considera calibración personalizada de auriculares.

# v2.5.357 (648) - 2026-11-05

- **Ruta B — telemetría app-side restaurada sin tocar el control plane:** `omega_effect` mantiene un SHM local dedicado para RMS/peak, contadores y generación aplicada incluso cuando el daemon está conectado como reader del bus de control. El writer de telemetría es una instancia separada de `effectControlBus()`; nunca abre el SHM root-only del daemon como segundo writer.
- **Seguridad RT:** el SHM se abre durante la creación de la instancia (fuera del callback). Las publicaciones usan un `atomic_flag` try-lock; si dos sesiones coinciden, se descarta una muestra en lugar de bloquear el hilo de audio. La app sigue leyendo por `OMEGA_EFFECT_LOCAL_BUS_PATH`.
- **Validación:** se amplía `check_adaptive_metric_truth.py` para impedir que el reader del daemon y el writer local compartan singleton o que desaparezca el canal de telemetría. CI debe confirmar compilación y contratos. No se declara validación auditiva ni en Moto G85.

## v2.5.356 (647) - 2026-11-06

- **Dataset público REW → infraestructura Volterra H2:** el dataset Zenodo 10.5281/zenodo.20567124 (CC BY 4.0, .mdat de Java Serialization v5) se procesó con herramientas propias de solo lectura: `tools/volterra_h2/mdat_parser.py` (deserializador) y `export_dataset.py` (CSV/JSON/NPY + análisis Fase III). Inventario verificado: 6 barridos (48 kHz, 54 613 puntos, RI de 1 048 576 muestras @96 kHz) y 6 stepped-sine (96 kHz, 31 puntos, −20 dBFS).
- **Identificabilidad H2:** veredicto reproducible (`volterra_h2_identify.py`): H1 identificable desde las RI almacenadas; H2 NO identificable (sin excitación cruda, sin salida cruda, sin fase de armónicos, sin dos tonos, sin serie de niveles). Ningún coeficiente se fabrica; el estimador LS regularizado se valida solo en sistemas Volterra sintéticos etiquetados (residuo 1e-16, err<2e-8, K=8).
- **Puerta de carga de kernels:** `tools/volterra_h2/kernel_loader.py` valida integridad (CRC32), procedencia, dispositivo, dimensiones, fs, finitez, amplitud y estabilidad con 9 pruebas positivas/negativas (`tests/test_kernel_loader.py`). Ningún kernel externo llega al motor de producción: `updateKernels()` sigue sin consumidor de producción (auditoría documentada).
- **Documentación:** `docs/research/volterra_h2_public_dataset/` (procedencia, inventario, análisis, identificabilidad, pipeline, protocolo de caracterización Genezy, informe de validación, hashes).
- **CI:** `run_check kernel_loader` añadido a la puerta host; los tools Python no tocan el DSP de producción (Ruta A/B y GoldenEar sin cambios funcionales).
- **Alcance:** infraestructura y evidencia. No hay kernel medido cargado ni validación acústica en dispositivo; la caracterización Genezy requiere el protocolo de Fase VII.

## v2.5.355 (646)

- En Ruta B, GoldenEar reclama el único slot no lineal antes de ejecutar `processStereo()` y conserva la reserva mientras su envolvente mantiene cola audible.
- Volterra solo inyecta PCM cuando obtiene el slot. Si pierde la prioridad y todavía tiene cola, avanza su release sobre scratch descartable; no mezcla esa cola en la salida. El historial FIR sigue actualizado cuando el kernel es identidad/inactivo.
- `omega_effect` deja de usar `setVolterraEnabled()` como autoridad (se conserva la API JNI histórica por compatibilidad); el gate se aplica directamente al motor real `ctx->volterraEngine` que consume PCM.
- Añade regresiones del árbitro para prioridad GoldenEar y adquisición de Volterra cuando GoldenEar no está activo.
- Alcance: arbitraje GoldenEar/Volterra en Ruta B. No declara resuelta toda la jerarquía no lineal (NPE/exciters), paridad total de Ruta A ni validación acústica en dispositivo.

## v2.5.354 (645) - 2026-11-05

- **Volterra H2 — bypass matemático real:** kernel inicial h1=[1,0,...], h2=0 reconocido como identidad; passthrough exacto sin convolución FIR ni bucles cuadráticos. Las actualizaciones detectan H1 identidad y H2 nulo por separado.
- **Actualización segura:** se rechazan coeficientes NaN/Inf, longitudes incorrectas y escrituras mientras el motor está habilitado o el envelope no está silencioso. Puerta atómica + contador de callbacks evitan escrituras concurrentes sin bloquear el hilo RT. El bypass identidad mantiene el historial circular caliente para activar una calibración sin memoria vacía.
- **OOM fail-closed y memoria perezosa:** se comprueban todas las asignaciones; cualquier fallo libera asignaciones parciales y deja passthrough seguro. El constructor no reserva H2: el kernel triangular O(K²) se asigna en hilo de control solo cuando llegan coeficientes H2 no nulos. Un motor de 8192 taps deja de reservar ~134 MiB solo para ceros. El destructor tolera inicialización incompleta.
- **Thread ownership:** setEnabled solo publica intención; el envelope se modifica desde DSP. Estado silencioso y ganancia de transición se publican mediante atomics, evitando lecturas concurrentes del envelope desde el hilo de control. Corregido stride interleaved cuando la entrada contiene más canales que el motor.
- **Pruebas:** identidad exacta, H1/H2 nulos, rechazo de actualización habilitada/NaN, FIR lineal y kernel cuadrático no nulo con asignación diferida.
- **Alcance:** robustez y reducción de cómputo en kernel no calibrado. No se carga kernel medido ni se afirma mejora perceptual o validación física.

## v2.5.353 (644) - 2026-11-05

- **Ruta A — HRTF sin asignaciones en el callback:** `PDEngine` reemplaza los dos `std::vector` de scratch HRTF por buffers `std::array` fijos de 2048 frames, coherentes con el límite de `nativeProcess` y los buffers `AudioThreadState`. Se elimina `resize()` del camino de audio.
- **Degradación segura:** si un caller excede 2048 frames, HRTF se omite y la rampa binaural retorna suavemente a Spatial; no se indexa scratch fuera de rango.
- **Prueba de tiempo real:** `test_rt_no_alloc` ejercita PDEngine/HRTF dentro del scope de callback durante 10 000 bloques, moviendo azimut desde el hilo de control. El gate exige cero asignaciones dinámicas.
- **Paridad HRTF corregida:** HRTF real existe en Ruta A (`PDEngine::hrtf/HRTFConvolver`) y Ruta B (`HrtfManager`). Son implementaciones distintas; no se declara equivalencia perceptual ni se duplica el motor.
- **Evidencia:** cambios de código y contratos sujetos al CI de este commit. No se afirma escucha, medición acústica ni validación física en el Moto G85.

## v2.5.352 (643) - 2026-11-05

- **Ruta A — estado real del EQ evolutivo:** la UI consume el read-back JNI cada 500 ms y muestra por separado si el filtro está calibrado/validado y activo, si sigue calibrando/verificando, si el usuario lo desactivó o si la biblioteca nativa no está disponible. El interruptor continúa representando exclusivamente la intención persistida del usuario.
- **Auditoría JNI:** se conecta el getter nativo que estaba declarado e implementado pero sin consumidor Kotlin. Se añade contrato estructural para impedir que vuelva a quedar huérfano.
- **Evidencia:** CTest previo pasó 656/656; el workflow host separado falló en la auditoría JNI por este getter sin llamador. El cambio queda sujeto a la nueva ejecución CI y compilación Android. No implica validación en dispositivo ni paridad completa Ruta A/B.

## v2.5.351 (642) - 2026-11-05

- **Ruta A — recuperación del worker de EvolutionaryEQ:** si la asignación del filtro fallaba o la calibración lanzaba una excepción/no superaba la auto-verificación, el flag `g_evoEqACalibrating` quedaba permanentemente activo y bloqueaba cualquier reintento en ese proceso. Ahora el worker POSIX libera el guard en todos los caminos, descarta filtros no validados y comprueba los códigos de retorno de creación/configuración del hilo. Es compatible con `-fno-exceptions`. El filtro solo se publica tras `isReady()`; la salida permanece en identidad si no hay filtro válido. Sin cambios al callback DSP ni a la curva calibrada.
- **Prueba:** se amplía `tests/test_route_a_evoeq_contract.py` para fijar limpieza del guard y protección de errores.
- **Alcance:** cambio de robustez por lectura de código + contrato estructural. La compilación NDK, ejecución CI y validación auditiva/dispositivo quedan pendientes de sus logs; no se declara paridad funcional completa.

## v2.5.350 (641) - 2026-11-05

- **EQ evolutivo de Ruta A — interruptor fiel y persistente:** el interruptor se pintaba con el estado "calibrado y activo", falso durante los primeros segundos (la calibración corre en su hilo), y apagarlo no sobrevivía a un reinicio: el nativo vuelve a ON y el usuario lo oía sin saberlo. Ahora la elección se guarda (`EvolutionaryEqPrefs`, `ivanna_ui_prefs`/`evo_eq_enabled`), se reaplica al arrancar en `PersistedStateRestorer` y el panel la lee de ahí. Contrato en `tests/test_route_a_evoeq_contract.py` (mutación comprobada).
- **Auditoría de paridad A/B corregida (`docs/ROUTE_A_EFFECT_PARITY.md`):** Atlas **sí** existe en Ruta A (`SceneTargetBus` desde `ivanna_ime_jni.cpp`; solo los bits del snapshot son de Ruta B). `AdaptiveEngineV2` queda sin portar a propósito: sería un segundo controlador sobre los mismos actuadores que ya mueve el ADE. Se documenta la respuesta REAL medida en host del EQ evolutivo calibrado: ±0.4 dB (−0.38 dB a 60 Hz … +0.24 dB a 13 kHz) con 127 muestras de retardo; la curva objetivo del módulo (hasta +3 dB) no se alcanza. Es paridad, no un efecto de gran impacto.
- **Alcance:** Kotlin y contratos estructurales; no se compiló con el SDK de Android ni se escuchó en el dispositivo.

## v2.5.349 (640) - 2026-11-05

- **Ruta A — NPE neuromorfico:** con el motor nativo activo, `PlaybackCaptureService` no ejecuta `IvannaNpeEngine` (Kotlin) para no apilar etapas, así que el interruptor y los parámetros del NPE no tenían efecto en la ruta que de verdad intercepta el audio (solo llegaban al módulo, Ruta B). Ahora `NpeStereoCore` (la misma cadena por muestra que usa Ruta B: NHO, pool LIF, inhibición lateral, compresión OHC, AGC) corre en `nativeProcess` y `nativeProcessBlock` justo antes del punch y el limitador final.
- **Control:** `PiLstmBridge` aplica el estado a la cadena nativa (`nativeSetNpeLocal`) antes de enviarlo al daemon, sin depender de que el módulo exista. Apagado y con fade 0 es bit-exacto; apagado por defecto hasta que la app lo publique, como en Ruta B. Se prepara en `nativeInit`, nunca en el callback.
- **Pruebas:** contrato estructural (9 en `tests/test_route_a_evoeq_contract.py`) y `test_npe_core` 5/5 (el núcleo no cambió).
- **Alcance:** solo lectura de código, comprobación de sintaxis de la JNI con stubs y pruebas host del núcleo. **No** hay compilación NDK ni escucha en el Moto G85; el NPE añade armónicos y ensanche, y su efecto audible en Ruta A no está medido. El mapeo de parámetros (alpha = OHC, beta = inhibición lateral, armónico = ganancia no lineal) sigue el de Ruta B.
- **Sigue solo en Ruta B:** escena binaural de `HrtfManager`, GoldenEar GAN, `AdaptiveEngineV2`, bits `ATLAS_*`.

## v2.5.348 (639) - 2026-11-05

- **Ruta A (captura de reproducción) — EQ evolutivo de 512 bandas:** hasta ahora `EvolutionaryEQ` solo corría en el efecto del sistema (Ruta B); la ruta que realmente intercepta el audio no lo tenía. Ahora `nativeProcess` y `nativeProcessBlock` lo aplican justo tras el EQ paramétrico. Es el mismo FIR de 256 taps que usa Ruta B.
- **Seguridad de tiempo real:** la calibración (LM-CMA-ES acotada, ~0,6 s en host) corre en un hilo propio desde `nativeInit` y solo se publica, por atómico, si la auto-verificación pasa (FIR finito, pico ≤ 1); hasta entonces es identidad bit-exacta. El hilo de audio no asigna ni calibra. Entra y sale con un crossfade de un bloque (sin click). Por defecto activo, como en Ruta B; toggle «EQ EVOLUTIVO 512» en el panel de control (`nativeSetEvolutionaryEqEnabled`).
- **`EvolutionaryEQ::processPlanar`:** admite bloques de longitud arbitraria (Ruta A los entrega variables); con n = 128 es bit-exacto con `processNEON`, y partir un bloque en trozos no cambia la salida. Dos pruebas nuevas (8/8 en `test_evolutionary_eq`) y un contrato estructural (`tests/test_route_a_evoeq_contract.py`).
- **Alcance:** verificado en host (DSP y contrato de lectura; comprobación de sintaxis de la unidad JNI con stubs). **No** se compiló la biblioteca JNI con el NDK ni se escuchó en el dispositivo: el cambio de timbre de este filtro no está medido en el teléfono. La calibración depende de un RNG `thread_local` con semilla fija que avanza entre instancias: el FIR no es idéntico al de Ruta B en el audioserver.
- **Sigue solo en Ruta B:** escena binaural de `HrtfManager`, GoldenEar GAN, `AdaptiveEngineV2` y los bits `ATLAS_*` (ver `docs/ROUTE_A_EFFECT_PARITY.md`).

## v2.5.347 (638) - 2026-11-05

- **Diagnóstico `ivanna_control.sh preload reach` (solo lectura, root):** para cada proceso del HAL de audio informa regiones `.so` cargadas desde un dispositivo distinto al vendor original, si el `mountinfo` del HAL contiene la sonda y el dominio SELinux. Veredictos: `HAL_NOT_RUNNING`, `NEEDS_ROOT`, `NO_MODULE_CODE_MAPPED`, `MODULE_FILES_NOT_VISIBLE`, `VISIBLE_NOT_MAPPED`, `MODULE_CODE_MAPPED`. Sin `mountinfo` legible no afirma que el HAL no ve el archivo. 7 pruebas nuevas (29 en `test_preload_link.py`).
- **Captura real del HAL analizada** (`hal_integration/moto_g85/AUDIOSERVER_CAPTURE_2026-11-05.md`): las 461 regiones `.so` del proceso `android.hardware.audio.service_64` son del vendor original; ningún código de módulo (ni `libomega_effect.so`, ni las sondas) está cargado; `wrap.*` definido pero sin efecto; `init_boot` sin relación con audio. Aplicando `reach` a esa captura: `NO_MODULE_CODE_MAPPED`.
- **Alcance:** **el puente PAL/ASRC sigue sin estar conectado.** Falta un productor dentro del HAL (frames+timestamp pareados de PAL) y un actuador de tasa verificado; no se parchea `audio.primary.blair.so`. Verificado en host con árbol `/proc` falso y con la captura real; no ejecutado en el teléfono.

## Unreleased

- **Moto G85 / Qualcomm AGM:** se documenta el inventario de símbolos dinámicos de `libagm.so` suministrado y se amplía `hook_pal_agm.js` con observación pasiva de escritura/lectura AGM, ciclo de vida de sesión/grafo/dispositivo y consultas de timestamp. Los argumentos se registran como valores opacos: no se asume ABI ni se escriben parámetros. No habilita todavía inyección HAL, interceptación PCM ni ASRC físico; el firmware Android 14 debe confirmar identidad ELF, correlación frames/timestamp y actuador real.

## v2.5.346 (637) - 2026-11-04

- **Transaural en Ruta A (captura de reproducción):** la geometría de sala 3.83 × 2.83 × 7.00 m del cancelador de diafonía solo se aplicaba en `omega_effect.cpp` (Ruta B). El cancelador de Ruta A es el de la cadena 4D y seguía con la geometría de escritorio (0.90 m / 0.85 m). Ahora se deriva de FL/FR del layout del WFS de Ruta A con la misma función que Ruta B, al preparar el motor y al cambiar el layout.
- **Auditoría de paridad A/B:** `docs/ROUTE_A_EFFECT_PARITY.md`. `IvannaFusionEngine` solo se compila en `libomega_effect.so`; los efectos sin equivalente en Ruta A son EvolutionaryEQ, la escena binaural de `HrtfManager`, GoldenEar GAN, `AdaptiveEngineV2` y los bits `ATLAS_*` (el offload FastRPC no es trasladable). **No se movió ningún efecto de B a A en esta versión**: hacerlo en bloque duplicaría etapas que Ruta A ya tiene y no hay forma de comprobar el sonido aquí.
- **Alcance:** verificado con prueba estructural y compilación aislada del helper; la biblioteca JNI completa (NDK) y el dispositivo quedan sin comprobar.

## v2.5.345 (636) - 2026-11-04

- **Activación transaural por ruta:** XTC se habilita solo en USB/AUX cuando el usuario declara una línea estéreo externa. Si no existe preferencia XTC guardada, la declaración la habilita por defecto; un OFF guardado prevalece. Bluetooth, altavoz interno, rutas desconocidas y salidas USB/AUX sin declaración quedan en bypass. Si se usa el mismo DAC con audífonos, el usuario debe apagar la declaración: Android no identifica la carga analógica.
- **Independiente del maestro 4D:** la cancelación funciona aunque anticipación/háptica 4D esté apagada; análisis 4D, háptica y dosímetro conservan su propio interruptor. Los cambios de ruta, restauración y parada sincronizan Ruta A y Ruta B; un fallo de evaluación fuerza bypass.
- **Sin XTC falso en RIR:** el crossmix experimental de `RirConvolver` queda en cero en runtime. RT60 y arquetipo Bark ya no pueden activarlo ni apilarlo con `CrosstalkCanceller`.
- **Release seguro:** el ZIP contiene un manifiesto candidato alineado con `module.prop`; el manifiesto público solo se actualiza después de subir y verificar ZIP, APK y checksums, sin anunciar un ZIP 404 ni retroceder un versionCode más nuevo.
- **Hereda v2.5.343 MAGISTRAL:** perfil nominal de sala 3.83 × 2.83 × 7.00 m y geometría aplicada a los canceladores; conserva el HPF corregido y el límite de realce mono de +4 dB de v2.5.342.
- **Límite acústico:** no se afirma matriz 2×2 medida altavoz→oído, calibración binaural personalizada ni validación física. El perfil de sala es nominal y requiere medición en la posición real de escucha. La declaración de línea externa es necesaria porque Android no distingue un DAC conectado a un amplificador del mismo DAC conectado a audífonos.

## v2.5.344 (635) - 2026-11-04

- **Transaural — carrera de datos corregida:** `CrosstalkCanceller` guardaba la geometría, ρ, el retardo y el coeficiente del paso-alto como `float` planos que escribían los setters (hilo de control: geometría de sala, rastreador de cabeza) mientras `processBlock()` los leía en el hilo de audio. Ahora todo es `std::atomic<float>` (relaxed), incluido `listenerOffsetM` de v2.5.343. Sin cambio de sonido ni de API.
- **Pruebas:** `GeometryRoundTripKeepsListenerOffset` y `ConcurrentGeometryUpdatesNeverProduceNonFiniteOutput` (2000 bloques con la geometría cambiando en paralelo). `Crosstalk*` 10/10 en host y sin avisos con `g++ -fsanitize=thread`.
- **Alcance:** verificado en host; no en dispositivo. No cambia el estado de LD_PRELOAD (sigue bloqueado por init_boot) ni se modifica `audio.primary.blair.so`.

## v2.5.343 (634) - 2026-11-04

- **Transaural MAGISTRAL (sala del propietario):** perfil único 3.83 × 2.83 × 7.00 m — FL/FR a 1.50 m y 2.70 m de separación, SL/SR en las paredes laterales a 3.50 m del frente y 1.80 m de altura, subwoofer en el suelo. Se aplica igual en `RoomGeometryConfig`, el snapshot por defecto, el estado del daemon y la UI (`WfsCalibrationManager`); TL/TR bajan de 3.00 m (sobre el techo) a 2.83 m. `tests/test_magistral_defaults.py` impide que las cuatro copias diverjan.
- **Geometría del cancelador conectada a la ruta real:** ningún camino fijaba `CrosstalkCanceller::Geometry`, así que ambos cancelladores corrían con la de escritorio (0.90 m / 0.85 m). Ahora separación, distancia y corrimiento del oyente se derivan de FL/FR del snapshot (`RoomGeometryConfig::crosstalkPlacement`) y se aplican al cancelador del motor de fusión y al de la cadena 4D (que ahora guarda la geometría hasta que la cadena exista). ITD del cruce: 183 µs (antes 238 µs); ρ no cambia (manda el tope de +4 dB). Nuevo `Geometry::listenerOffsetM`.
- **Pruebas:** `test_magistral_room_xtc` (10 casos: coordenadas, ITD frente a Woodworth, cancelación ≥ 5 dB de 500 Hz a 4 kHz, mejora frente a la geometría de escritorio, tope de realce y sub intacto). Las pruebas de WFS, snapshot y `test_4d_ultra_chain` existentes siguen en verde en host.
- **Alcance:** verificado en host con el modelo del propio módulo; **no** hay medición binaural ni prueba en el dispositivo. Supuestos del propietario no dados (oyente centrado a 3.50 m con oídos a 1.20 m, TL/TR en el techo, subwoofer x/z sin cambiar) en `docs/TRANSAURAL_MAGISTRAL.md`. No se declara "transaural validado".

## v2.5.342 (633) - 2026-11-04

- **Transaural (CrosstalkCanceller, altavoces/AUX) — defecto real corregido:** el "paso-alto de 120 Hz" del término cruzado era `hp += a*(x - xprev); cf = x - hp`, que no es un paso-alto: dejaba el término cruzado en (1-a)·x ≈ −36 dB a **todas** las frecuencias. Con el cancelador activado la separación interaural mejoraba **0.1 dB** (−4.7 → −4.8 dB) y las pruebas de estabilidad pasaban porque un cancelador que no hace nada es estable. Ahora usa un paso-alto de un polo estándar.
- **Realce con contenido centrado, acotado:** al hacerlo cancelar de verdad apareció el efecto del shuffler recursivo con voces/bajo centrados: hasta 1/(1−ρ), **+7.4 dB a 2.1 kHz** sin tope, la banda más áspera. Nuevo `setMaxMonoBoostDb` (defecto **+4 dB**, rango 0.5–9) acota ρ, y `worstMonoBoostDb()` lo expone como telemetría.
- **Resultado medido** contra la acústica que el propio módulo documenta (ρ=0.58, ITD 238 µs): fuga contralateral **−4.7 dB (OFF) → −11.7 / −14.6 / −11.4 / −12.1 / −14.2 dB** a 500 / 1k / 2k / 4k / 8k Hz; realce mono máximo **+3.9 dB** @ 2.1 kHz; por debajo de ~120 Hz el cancelador no actúa (graves intactos). Sin tope (+9 dB) se alcanzan −26…−41 dB pero con +7.4 dB de realce; el valor por defecto prioriza no empeorar el timbre ante una geometría de oyente sin medir.
- **Pruebas:** `test_4d_ultra_chain` 22/22. Cuatro nuevas (reducción real de fuga > 5 dB, realce mono ≤ tope, tope por defecto y telemetría, graves intactos), verificadas por mutación: restaurar el filtro defectuoso y quitar el tope las hacen fallar.
- **Alcance — lo que NO está probado:** las cifras validan el álgebra del cancelador contra su propio modelo de cabeza/altavoces. La geometría real del oyente (separación, distancia, posición) **no está medida**: sin micrófono binaural en el Moto G85 no se puede afirmar la cancelación percibida. Por diseño solo actúa por AUX/altavoces y sigue siendo opcional (`Cancelación de diafonía (por altavoces)`, apagado por defecto); `ExternalStereoPolicy` no cambia.
- **Hallazgo pendiente (no tocado):** el "XTC tipo BACCH" dentro de `RirConvolver` resta ~5–12 % de la cola húmeda con un solo tap y su intensidad (0.16 en Ruta A, 0.38 en Ruta B) se decide por el arquetipo de reverberación, no por si la salida es altavoz o audífono. No es una cancelación real y puede apilarse con este cancelador; requiere decisión de producto antes de cambiarlo.

## v2.5.341 (632) - 2026-11-03

- **Módulo Magisk — sonda LD_PRELOAD PAL empaquetada:** el módulo principal ahora genera `libivanna_pal_presence_probe.so` (en `system/vendor/lib64/`). El CI la compila desde `hal_integration/moto_g85/pal_preload_probe/` con `-Wall -Wextra -Werror`, `-fvisibility=hidden`, relro/now y `max-page-size=16384`, y la verifica: AArch64, sin `libc++_shared`, ningún símbolo `pal_*`/`agm_*` definido (sería interposición) ni requerido, segmentos LOAD alineados a 16 KB. El ZIP debe contener exactamente la librería compilada; `BUILD_INFO` registra `preload_probe_sha256` y `verify-release` lo contrasta contra la librería extraída del ZIP publicado.
- **Nuevo `ivanna_control.sh preload [status|report]`:** lee `/proc/<pid>/maps` y `/proc/<pid>/environ` de los procesos `android.hardware.audio.service*` (solo lectura, requiere root) y devuelve `verdict=` `LIB_NOT_INSTALLED`, `HAL_NOT_RUNNING`, `NEEDS_ROOT`, `NOT_LOADED` o `LOADED`. Es la comprobación que cierra el gate 6 de `hal_integration/moto_g85/pal_preload/README.md` cuando exista inyección.
- **Instalación:** `customize.sh` etiqueta la librería como `vendor_file` (un fallo no bloquea la instalación) y avisa que LD_PRELOAD NO queda activo.
- **Alcance — lo que NO es esta versión:** **no hay inyección automática de LD_PRELOAD**, ni hook de PAL, ni ASRC por esta vía. init crea `vendor.audio-hal` antes de que corra `service.sh` y el rc del vendor es EROFS de solo lectura; ningún script del módulo exporta `LD_PRELOAD`, escribe `setenv` ni reinicia el HAL (lo fija `tests/test_preload_link.py`). Con el módulo recién instalado el veredicto esperado es `NOT_LOADED`. Desbloquear requiere la imagen `init_boot` activa y el rc exacto del servicio (`collect_init_boot_evidence.sh`).

**Validation boundary:** `preload_link.sh` se ejecutó contra un árbol `/proc` falso (16 casos) y el contrato de empaquetado se fijó con 5 pruebas más; las pruebas se verificaron por mutación. Las compuertas de símbolos se probaron en host con la fuente real (sonda limpia pasa; variantes que exportan o requieren `pal_stream_*` quedan atrapadas). La compilación NDK, el hash en el ZIP y la alineación 16 KB los confirma el CI. Sin validación en el Moto G85: etiqueta SELinux de la librería, carga real en el HAL y permiso de escritura del reporte requieren prueba en el dispositivo.

## v2.5.340 (631) - 2026-11-03

- **Módulo Magisk — canal ASRC/reloj HAL:** el módulo nunca creaba `/data/vendor/ivanna_audio` (solo lo hacía una plantilla `init.rc` que no se instala), de modo que `ivanna_daemon` no podía abrir `asrc_state` ni `clock_snapshot.v2` con `open(O_CREAT)`. Nuevo `asrc_link.sh` (cargado por `service.sh` antes de lanzar el daemon) crea el directorio (0775, `adb_data_file`) y, cuando el daemon crea los archivos con 0660 root:root, les abre lectura (0664) para la app. `sepolicy.rule` v3.5 permite a la app atravesar `/data/vendor`. Nuevo comando `ivanna_control.sh asrc-link [repair]` y prueba `tests/test_asrc_link.py` (7 casos, mutación verificada).
- **Alcance:** corrige la publicación/lectura del estado del servo ASRC software. No añade actuador ASRC físico ni un productor de reloj PAL: eso sigue bloqueado como documenta `docs/ASRC_SOFTWARE_CLOCK_WIRING.md`. **No es la integración completa del ASRC:** sigue sin existir un productor de reloj propietario del stream (el daemon permanece en `BLOCKED:no-writable-correlated-route`, ratio 1.0). El desensamblado de `audio.primary.blair.so` muestra que el par frames/tiempo del HAL es software-estampado (`CLOCK_MONOTONIC` tras `pal_get_timestamp`) y cuantizado a 1 ms; la sonda `hal_integration/tools/ivanna_pal_ts_probe.js` queda lista para medir si `absolute_time`/`timestamp` sirven como fuente. Sin validación en dispositivo.

## v2.5.339 (630) - 2026-11-03

- **WFS en Ruta A:** conecta `RouteAWfsEngine` y el `WfsRenderer` existente al procesamiento JNI, después de PDEngine y antes de RIR/protección final.
- **Arbitraje AUX/WFS:** AUX tiene prioridad; WFS queda excluido mientras AUX está activo y la transición entre modos conserva el fundido gradual.
- **Robustez DSP:** bypass mantiene alimentadas las líneas de retardo; se sanea audio no finito, se unifica el filtro seco con la frecuencia activa y se comparte el corte efectivo del alias guard.
- **Geometría y frecuencia:** historial ITD ampliado a 256 muestras para cubrir retardos de hasta 384 kHz; pruebas de regresión y contratos de cableado actualizados.

**Validación:** CI de esta versión debe completar compilación Android/ARM64, CTest, JVM y verificación de integridad APK/ZIP. Las pruebas host no sustituyen la validación acústica en el Moto G85; no se afirma certificación física ni activación de hardware WFS multicanal.
## v2.5.338 (629) - 2026-11-03

CodeRabbit follow-ups, each verified against current `main` before applying (findings already fixed were skipped).

- **Adaptive engine join-state test coverage:** added `AdaptiveDecisionEngine::controlThreadPendingJoin()`, a test-only accessor that reads the real join state under `controlLifecycleMutex_` (pthread branch on Linux/Android, `std::thread` branch elsewhere). `testControlThreadLifecycle` and `testConcurrentControlThreadLifecycle` now assert that `stop()` joins the control thread; `running()` alone could not, because `stop()` clears it before the join. Verified by mutation: with the join block removed the new assertions fail.
- **Adaptive engine start from `nativeInitDSP`:** `nativeInitDSP` (the path `MainActivity` uses) and the "already initialized at the same sample rate" early returns now call `ensureAdaptiveEngineStarted()`, so an ON preference stored before DSP init no longer waits for `nativeCreateAdaptiveEngine` or the watchdog. The helper is idempotent and still respects OFF/manual mode.
- **FotoHolo state retry:** the retry now runs before the idle-loop `continue` (it stayed frozen with FotoHolo off, in bypass or without signal) and reads the desired state inside the serial executor, so it can no longer overwrite a newer UI intent. "Reapplication needed" is only reported after a Route-B width was actually pushed.
- **Static contracts:** the owned-player ASRC guard now requires `setClockCorrectionPpm(correctionPpm)` (a constant no longer satisfies it) and new guards cover the FotoHolo retry ordering and the `nativeInitDSP` ADE start. `check_adaptive_metric_truth.py` accepts an ADE start before `g_initialized.store(true)` only inside the already-initialized branch.
- **Docs:** the route-wiring audit separates the MediaCodec player (uses `StereoAudioResampler`) from `PlaybackCaptureService` (`AudioRecord` at 48 kHz, no software ASRC).

**Validation boundary:** native host tests and static contracts were run locally; the Kotlin changes (`FotoHoloEngine`, `FotoHoloRouteB` and its unit test) could not be compiled without the Android SDK and rely on CI. No on-device validation: thread lifecycle, FotoHolo recovery after daemon restarts and ADE start order still require Moto G85 5G testing.

## v2.5.337 (628) - 2026-11-03

- Corrige el orden de inicialización de `LocalSocket`: `connect()` se ejecuta antes de configurar `soTimeout` en `MagiskBridge.sendCommand`, `isOmegaSocketAvailable` y `ShmManager.mapFromDaemon`. Evita `IOException("socket not created")` por descriptor aún no creado.
- Conserva los cambios ya integrados en `main`, incluidos el ciclo de vida del motor adaptativo y los reintentos de estado FotoHolo.
- Validación: pendiente de CI, verificación de APK/ZIP y prueba en Moto G85. No se declara validación física por esta corrección.

## v2.5.236 (527) - 2026-11-02

- **FotoHolo Ruta B: "demonio no disponible" congelado.** Si `SET_FOTOHOLO_STATE` fallaba una vez (p. ej. la app abría antes de que el daemon estuviera listo), el texto del fallo permanecía indefinidamente cuando la ruta activa era ROUTE_A: el bloque de reintento/refresco de `FotoHoloEngine` solo corre con ROUTE_B. Además `shouldRetryCommand` comparaba con `==` un texto que lleva el prefijo `FotoHolo Ruta B: `, así que tampoco lo reconocía en ROUTE_B.
- **Cambio:** `FotoHoloRouteB` recuerda el último estado FotoHolo pedido y si su publicación falló por transporte; `retryStatePublishIfNeeded()` lo reenvía (los rechazos explícitos del daemon no se reintentan); `FotoHoloEngine` lo invoca cada 1.5 s con cualquier ruta activa; `shouldRetryCommand` tolera el prefijo. Test JVM nuevo para el prefijo.
- **Sin cambios** en el daemon, `sepolicy.rule` ni en el procesamiento de audio.

**Validation boundary:** corrección de lógica de estado/reintento en la app; verificada por CI (compilación + tests JVM). Que el estado se recupere en el dispositivo tras abrir la app con el daemon ya activo requiere comprobación en el Moto G85 5G.

## v2.5.235 (526) - 2026-11-02

- **omega_effect arm64 build fix:** Route-B adaptive engine start retries in `omega_effect.cpp` report failures through `LOGE`, which that file did not define (only `LOGI` and `LOGW`). The macro is now defined with the same `#ifndef` pattern as `LOGW`. Found by syntax-checking every native source touched by PR #17 with `-fno-exceptions -fno-rtti` before the arm64 runner reached the `omega_effect` target.

**Validation boundary:** compilation fix only; no change to audio processing. Runtime behavior of the start-retry path still requires Moto G85 5G device validation.

## v2.5.234 (525) - 2026-11-02

- **arm64 native build fix:** the adaptive engine lifecycle work (PR #17) added `try/catch` blocks in `adaptive_decision_engine.cpp` and in the JNI snapshot-thread start. `ivanna_omega` and `omega_effect` are compiled with `-fno-exceptions` on arm64-v8a, so Build APK failed with "cannot use 'try' with exceptions disabled". The blocks are now guarded with `#if defined(__EXCEPTIONS)`, the same pattern already used in `evolutionary_kernel_jni.cpp`; host tests keep the exception-safe path and the original code is preserved.
- **Behavior on device:** with exceptions disabled, a failed `std::thread` creation aborts instead of being recovered. This matches the existing `g_bridgeThread` start path.

**Validation boundary:** this release fixes compilation only. CI confirms the build; audible behavior, latency and thread-start robustness under memory pressure still require Moto G85 5G device validation.

## v2.5.233 (524) - 2026-11-02

- **Unified route-aware HRTF gate:** Sound, SpatialControlPanel, OEM controls and NPE callbacks now use the same policy as `RouteDspCalibrator`. A saved HRTF request cannot re-enable binaural processing on the built-in speaker or an explicitly declared external line-out.
- **Context-safe NPE callback:** NPE HRTF callbacks now receive the application context and apply the centralized route policy before publishing a partial Route-B update.
- **Release contents:** includes v2.5.227–v2.5.232: AudioFlinger HRTF/adaptive actuators, effect-generation acknowledgement, measured-vs-synthetic evidence, adaptive block telemetry and unity-return ramp, non-aliasing FastRPC staging with CPU fallback, transactional daemon publishing and SHM isolation.

**Validation boundary:** route policy behavior, audible quality, latency, thermal stability and HAL routing still require a Moto G85 5G device run. No physical measurement or hardware ASRC activation is asserted by host CI.

## v2.5.232 (523) - 2026-11-02

- **Route-aware HRTF publication:** opening or changing the Sound screen now applies the same output-route policy as `RouteDspCalibrator`. HRTF remains off for the built-in speaker and unknown route, and is enabled only for headphone-appropriate routes; declared external line-out policy is respected.
- **SHM isolation:** feature-only Route-B publications no longer rewrite unrelated gain/compressor/spatial-width shared atomics.
- **Transactional controls and honest evidence:** failed HRTF/adaptive publishes roll back; mixed CPU/Hexagon HRTF activity is counted correctly.
- **Release contents:** includes v2.5.227–v2.5.231: system-wide HRTF/adaptive actuators and generation acknowledgements, evidence v3 and accurate UI labels, adaptive unity-return ramp, non-aliasing FastRPC input and CPU fallback.

**Validation boundary:** output-route decisions and the complete audio route still need on-device validation on the rooted Moto G85 5G. No claim of audible improvement, stable latency or hardware ASRC activation is made without device evidence.

## v2.5.231 (522) - 2026-11-02

- **SHM isolation for feature switches:** HRTF/adaptive-only snapshot publications no longer rewrite unrelated shared gain, compressor, spatial width or route-mix atomics. This prevents feature-toggle failures from leaking partial unrelated values.
- **Transactional Route-B controls:** failed publications restore the prior in-memory HRTF/adaptive state and leave the previous generation/evidence baseline intact.
- **Evidence correctness:** Hexagon HRTF blocks are reported even when CPU HRTF blocks occur in the same evidence window.
- **Release contents:** includes v2.5.227–v2.5.230: system-wide HRTF/adaptive controls, effect-consumed generation acknowledgement, evidence v3, evidence-based UI labels, adaptive unity-return ramp, non-aliasing FastRPC staging and CPU fallback.

**Validation boundary:** real-device audio quality, latency, thermal limits, HAL paths and Hexagon availability require Moto G85 5G validation; this changelog does not claim those measurements are complete.

## v2.5.230 (521) - 2026-11-02

- **Transactional Route-B controls:** when publishing an HRTF/adaptive snapshot fails, the daemon rolls back the desired feature state and acknowledgement generation. A later unrelated publish can no longer activate a request that was reported as failed.
- **Mixed CPU/Hexagon evidence:** HRTF DSP delegation is reported whenever the DSP block counter advances, even if CPU HRTF blocks also occurred during the same evidence interval.
- **Release contents:** includes v2.5.227–v2.5.229 fixes: AudioFlinger HRTF/adaptive actuators with generation acknowledgement, evidence v3, measured-vs-synthetic processing status, adaptive block counts, smooth unity-return bypass, non-aliasing FastRPC buffers and CPU fallback on RPC failure.

**Validation boundary:** device-side audible quality, latency, thermal limits, real HAL routes and Hexagon DSP availability remain unverified until exercised on the rooted Moto G85 5G. Failed DSP delegation deliberately falls back to CPU.

## v2.5.229 (520) - 2026-11-02

- **FastRPC HRTF buffer safety:** Hexagon convolution now receives separate, preallocated dry input buffers and writes into the output buffers. No input/output aliasing is assumed by the QAIC wrapper.
- **Fail-safe DSP delegation:** HRTF DSP activity is counted only after `delegateBinauralConvolution` returns success. On RPC rejection/failure the original dry samples are restored and processing falls through to the CPU HRTF path in the same block.
- **Release contents:** includes v2.5.227–v2.5.228 work: generation-acknowledged system-wide HRTF/adaptive control, evidence v3, measured-vs-synthetic HRTF status, evidence-gated adaptive activity, reversible bounded adaptive gain ramp, UI status refresh and cross-panel route consistency.

**Validation boundary:** actual Hexagon execution, audible quality, latency, thermal stability and HAL behavior remain subject to on-device Moto G85 5G validation. Failed FastRPC calls intentionally use CPU fallback; this release does not claim that Hexagon is active on every device.

## v2.5.228 (519) - 2026-11-02

- **Route-B acknowledgement refresh:** after the processor evidence sampling window, the sound screen now performs a final status read so a late AudioFlinger callback can confirm the requested generation without leaving a stale pending label.
- **Evidence-gated adaptive reporting:** the UI reports new adaptive block activity only when both before/after evidence reads are valid; missing or incompatible evidence is shown as unavailable rather than inferred from a lifetime counter.
- **Release contents:** includes all v2.5.227 changes: system-wide HRTF/adaptive daemon actuators, generation/evidence acknowledgement, measured-vs-synthetic HRTF distinctions, adaptive block counter, and reversible bounded gain ramp with host regression tests.

**Validation boundary:** no Moto G85 audible, latency, thermal or HAL acceptance claim is made until the packaged build is exercised on that rooted device. Hardware ASRC remains separately gated and is not declared enabled by this release.

## v2.5.227 (518) - 2026-11-02

- **Route-B actuators:** the HRTF and adaptive switches publish through the daemon to the system-wide AudioFlinger effect. A command is shown as applied only after the effect publishes a newer evidence sequence with the requested snapshot generation and matching actuator states.
- **Measured HRTF status:** the sound panel distinguishes measured HRIR data being loaded from HRTF processing actually occurring in the evidence window; analytic fallback is labelled separately and no longer appears as measured HRIR processing.
- **Adaptive activity evidence:** effect evidence v3 records `adaptiveApplyBlocks` only when `AdaptiveEngineV2` processes a real Route-B audio chunk. The on-device evidence reader rejects incompatible older layouts and reports activity from measured counter deltas.
- **Reversible adaptive bypass:** Route-B adaptive gain uses a bounded, per-sample stereo ramp. Disabling adaptation returns the gain smoothly to unity rather than retaining the last attenuating gain. No adaptive gain stage can boost above unity.
- **Cross-panel route consistency:** HRTF and adaptive updates from the spatial panel, output-route calibrator, OEM/neuromorphic controls and manual-mode switches are merged as partial commands so concurrent toggles do not overwrite each other.
- **Regression coverage:** host tests cover snapshot propagation, invalid-command non-publication, reversible Route-B switches and bounded stereo gain ramps.

**Validation boundary:** physical audio processing, audible benefit, HAL routing and long-duration stability still require measurement on the rooted Moto G85 5G. This release does not mark device validation or hardware ASRC certification as complete.

## v2.5.226 (517) - 2026-11-02

- **Android lifecycle:** `MainActivity` recreated itself on every rotation/resize/keyboard change, and the UI keeps no `rememberSaveable` state, so rotating dropped the current screen, reloaded the whole FotoHolo WebGL scene and re-ran `onCreate`. It now declares `configChanges` (orientation, screen size/layout, keyboard, navigation); Compose adapts in place. `uiMode` is intentionally not included so theme changes still recreate.
- **Genezy audit (no change):** the AUX route is a bounded 1.04 differential-width policy ahead of the Sony's own upmix; it claims no discrete channels. Wired headphones share the same route class (`WIRED_AUX`), so they also receive it; separating them needs the physical AUX-vs-headphone signal, which Android does not distinguish.
- **Validation boundary:** rotate the device on the main panel and the FotoHolo tab: state and the 3D scene should persist.

## v2.5.225 (516) - 2026-11-02

- **HRTF dataset truthfulness:** the evidence (effect file, daemon `GET_PROCESSOR_EVIDENCE` → `hrtfDataset`, and `read_processor_evidence.py`) now says whether the HRTF is running on measured HRIRs (IHR1/SOFA) or on the analytic Rayleigh synthesis fallback. Without a dataset in `/data/adb/ivanna_omega/` the binaural path is the fallback, which a plain "applying" flag would have hidden.

## v2.5.224 (515) - 2026-11-02

- **FotoHolo WebView crash-proofing:** `onRenderProcessGone` was unhandled, so when Android killed the WebView renderer under memory pressure the whole app died (taking the audio session with it). The panel now logs it, returns handled and rebuilds the WebView.
- **Lifecycle:** WebGL/timers keep animating while the app is backgrounded, competing with the audio thread. The WebView is now paused on ON_PAUSE and resumed on ON_RESUME.
- **ASRC audit (no change):** measured the 64-tap Blackman-Harris resampler offline: 44.1→48 kHz is −0.26 dB at 20 kHz and −111 dB alias rejection at 30 kHz (96→48). Longer Kaiser designs would gain <0.3 dB at 20 kHz for 1.5–2× CPU, so it is left as is; the PLL already slew-limits the ppm correction.
- **Validation boundary:** compile/JVM gate in CI; renderer-kill recovery needs a device check (`adb shell am kill` of the WebView sandbox process or low-memory stress).

## v2.5.223 (514) - 2026-11-02

- **Daemon `GET_PROCESSOR_EVIDENCE`:** the root daemon reads the effect's evidence file (not readable by the app under SELinux) and answers with per-processor verdicts computed from the delta since the previous query: `hrtfApplying`, `hrtfDspDelegated`, `rirApplying` (block counters moved AND output energy differs from input), `safReachedHrtf`, `safReachedRir`, plus the baked SAF coefficients. First query returns `comparable:false` (no baseline).
- **CLI:** `ivanna_control.sh evidence` queries twice, 3 s apart, so the second reply is a real measurement.
- **Validation boundary:** needs the module rebuilt by CI and audio playing; silence yields no verdict by design.

## v2.5.222 (513) - 2026-11-02

- **Daemon supervision (Magisk):** `service.sh` started `ivanna_daemon` once and exited, so a later crash or lost socket left the app without a control plane until reboot. New `supervisor.sh` (single instance, verified by cmdline) checks every 5 s for the process AND the abstract socket, relaunches through `service.sh` with exponential backoff (5 → 60 s) and stops after 8 relaunches in an hour or when `.safe_mode`/`disable`/`remove` appears. Logged to `daemon.log` ("supervisor: ...").
- **Validation boundary:** shell syntax checked only; confirm on device with `kill -9 $(pidof ivanna_daemon)` and watch `daemon.log` for the relaunch within ~10 s.

## v2.5.221 (512) - 2026-11-02

- **Recovery after audioserver/HAL death (root and non-root):** when audioserver restarts, every `AudioEffect` of the process becomes a dead object while `IvannaGlobalEffectManager` kept believing the session was active (`openSession` returned early), so audio played without DSP until the app was restarted. A daemon watchdog now probes each session every 3 s (`setEnabled(true)` returns the real binder status); a dead effect is released and re-created with the same source package, and the EQ is republished to the daemon.
- **Tests:** `EffectLivenessTest` (JVM) pins the liveness criterion. The probe/recreate path needs the device: confirm with `logcat -s IvannaNPE.GlobalFX` after `killall audioserver` (look for "efecto muerto ... recreando").

## v2.5.220 (511) - 2026-11-02

- **HRTF:** SAF q[1] azimuth bias now reaches the analytic (no-dataset) synthesis path, where it was ignored; SAF changes mark the bank dirty and re-synthesize it from the audio thread after any running crossfade, without waiting for a new head pose.
- **RIR:** `wetScale` and `rt60Delta` from `computeSofaRirCoupling` were computed and discarded. `wetScale` now scales the wet path; `rt60Delta` reshapes the decay slope (baked into the IR). The raw IR is kept so a live SAF change re-bakes it on the worker thread (`couplingStale()` / `relinkFromRaw()`), not only on a room change.
- **Evidence (measured, not UI flags):** HRTF and RIR count blocks and in/out energy in their own audio paths; `omega_effect` publishes them to `/data/local/tmp/omega_effect_evidence` (seqlock mmap). `scripts/read_processor_evidence.py` compares two reads and reports per processor whether it applies. Hexagon-delegated HRTF is reported as not verifiable from the host side.
- **Tests:** `test_processor_evidence` links the real HrtfManager and RirConvolver: bit-exact wet=0 bypass, effect at wet=1, pose and SAF sensitivity, wetScale/rt60Delta applied, live re-coupling, plus negative controls.
- **Validation boundary:** host DSP evidence only; audible improvement and Moto G85 behaviour still need the on-device reader and A/B listening.

## v2.5.219 (510) - 2026-11-01

- **Evolutionary kernel:** worker advances only on valid non-silent post-DSP cues; population is re-scored when live signal resumes so elites evaluated against silence cannot dominate.
- **Worker lifecycle:** explicit UI Start works independently of renderer mode; Stop/mode exit joins the worker; convergence releases the evolved override instead of leaving a latched control state.
- **Concurrency:** best genome bytes, fitness and generation are published through atomic state for lock-free audio-thread readers.
- **FotoHolo Nanotomix:** UI distinguishes native DSP controls (spatial width and rotation) from visual-only depth/elevation/inclination. SAF listener calibration and SAF Room MAP diagnostics are reported separately.
- **Sony Genezy MHCPZ1:** UI records the physical topology (front L/R, satellite L/R, subwoofer) while accurately describing stereo AUX transport and the Genezy amplifier's internal speaker distribution; no fictitious five-channel PCM or IVANNA crossover is claimed.
- **Validation boundary:** CI and Moto G85/AUX listening tests are required before declaring acoustic improvement. Asset presence, control acceptance and scene rendering are not proof of audible application.

## v2.5.218 (509) - 2026-11-01

- **Build gate:** OFF-mode regression test now requires identity bypass; JVM updater tests verify that DSP state remains deferred when the JNI library is absent.
- **DSP acknowledgement:** the updater records a state as applied only after native setters succeed; partial/empty writes force a complete retry instead of suppressing future deltas.
- **Evolutionary genome ABI:** define a canonical 256-byte genome ABI: genes 0–3 drive a four-band EQ, 4–5 drive/resonance, 6–9 NHO, 10–11 spatial; genes 12–255 are the spectral phenotype. All 12 control genes are decoded and published through the lock-free control plane. The old genome-average-to-master-gain shortcut is removed.
- **Safe bypass:** downstream DSP consumes only a completely published genome and invalidates it when the manifold route is disabled.
- Fitness now uses the post-DSP loudness, transient and stereo cues plus spectral, resonance, dynamic, tonal, EQ-smoothness and spatial objectives. Convergence remains a measured optimizer metric, not a fabricated 1.0 target; audible improvement requires Moto G85 playback and loopback/A-B verification.

## v2.5.217 (508) - 2026-11-01

- Adaptive modulator: OFF/0% is a true bypass.
- DSP updater: do not cache a state before native library availability; force the first complete parameter write.
- Control loop: start after native initialization and keep it at application audio-process scope; expose tick/frame counters and errors.
- Device playback and audible A/B verification remain required; CI is not hardware certification.

## v2.5.216 (507) — 2026-11-02

- **Acoustic Reality continuo en Ruta B:** el pipeline ya no limita el orquestador a la primera secuencia de la sesión; recalcula el estado con PCM vivo en cada bloque de audio.
- **Regresión de actualización en vivo:** test nativo verifica que la secuencia acústica avanza durante bloques sucesivos y que la salida permanece finita.
- **HRTF honesto:** se elimina el log que afirmaba que el manifold SOFA+SAF de 255 sujetos estaba activo cuando no se había cargado un HRTF medido; ahora se informa explícitamente del fallback y del estado de la tabla medida.
- **Acoustic Reality por daemon:** conserva el envío global `SET_REALITY_RECONSTRUCTION` y la verificación de generación consumida por `omega_effect` incorporados en v2.5.215.
- **Validación pendiente en hardware:** la CI no certifica escucha; la salida real del Moto G85 requiere instalación y medición A/B.

## v2.5.215 (506) — 2026-11-01

- **Acoustic Reality Route B corregido:** los controles de activación e intensidad dejan de escribir solo en el singleton JNI del proceso de la app y publican el estado mediante `SET_REALITY_RECONSTRUCTION` al daemon.
- **Evidencia real de aplicación:** la interfaz verifica el read-back del daemon y la generación consumida por `omega_effect`; diferencia control aceptado de bloque DSP aplicado y no afirma mejora audible sin A/B nivelado.
- **Regresión UI:** pruebas cubren estados pendientes y generación DSP confirmada.
- **Límite de validación:** requiere CI, instalación y reproducción real en el Moto G85; HRTF/RIR/SAF y respuesta acústica siguen sujetos a sus propias verificaciones.

## v2.5.214 (505) — 2026-11-01

- **FotoHolo A/B conectado de extremo a extremo:** Ruta A conserva el análisis Bark de PlaybackCaptureService y su StereoWidener local; Ruta B analiza el PCM seco real de AudioFlinger dentro de `omega_effect`, sin depender de la captura A.
- **Control Route B:** interruptor e intensidad se transportan por `SET_FOTOHOLO_STATE`, daemon y `OmegaDspSnapshot`; el DSP modula al propietario espacial activo siguiendo WFS → HOA → M/S y confirma consumo mediante la generación aplicada por bloque.
- **DSP en tiempo real:** observador de 64 bandas sin asignaciones ni bloqueos en callback, anti-alias antes de decimar rutas de alta frecuencia, reset seguro, límites de ±0.25 y rampa temporal acotada.
- **Aislamiento de rutas e interfaz:** SAFMUS evita doble procesamiento A/B; la tarjeta distingue las bandas visualizadas de A de la modulación nativa B y ofrece verificación de evidencia B en lugar de un falso botón de sincronización.
- **Robustez nativa:** construcción/destrucción correcta del contexto C++ que contiene los observadores DSP; pruebas del códec temporal y del observador Bark, más correcciones de fixtures HAL.
- **Límite de validación:** la publicación de CI no equivale a certificación acústica del Moto G85. La confirmación física de Ruta B requiere instalar este artefacto y verificar AudioFlinger/efecto activo con reproducción real en el dispositivo.

## v2.5.213 (504) — 2026-10-31

- **FotoHolo Route B: robustez JVM:** el parser de ACK ya no depende de org.json Android stubs; interpreta únicamente escalares JSON permitidos y conserva la validación estricta de ok, applied, status y consumer.
- **Regresión corregida:** las pruebas JVM que fallaban por el runtime JSON quedan cubiertas por el build verificado.
- **Validación CI:** compilación APK y binarios nativos, pruebas JVM, CTest, integridad del módulo y validación de artefactos.
- **Alcance:** este release no declara calibración acústica física ni aprobación OEM/HWS de fases que requieran mediciones en hardware real.

## v2.5.212 (503) — 2026-10-31

- **Integración completa de evidencia:** el panel Magisk presenta generación publicada/aplicada, frames y estado de AudioEffect; FotoHoloRouteB reconoce la confirmación `dsp_block_applied`.
- **Negociación probada:** test nativo confirma HELLO proto 2 (`compatible=true`, `ready`).
- **Release:** v2.5.212 / versionCode 503. Incluye el read-back de generación DSP, API Kotlin, panel de evidencia y estado FotoHolo. Su publicación depende de CI y verificación de artefactos.

## v2.5.211 (502) — 2026-10-31

- **Read-back real del efecto:** `omega_effect` amplía el beacon de 64 a 72 bytes y publica `applied_control_generation` únicamente al finalizar un bloque DSP. Se mantienen los contadores de frames y el estado enabled/processing.
- **Daemon sin falsas confirmaciones:** `hasActiveConsumer()` consulta el beacon del efecto; ya no confunde el writer del daemon con un consumidor AudioEffect activo.
- **Telemetría de extremo a extremo:** `GET_STATUS` y las respuestas ricas exponen generación publicada, generación consumida por un bloque DSP, frames procesados y estado del beacon. `applied=true` exige read-back de una generación vigente.
- **API Kotlin:** `OmegaEngineBridge.requestControlPlaneEvidence()` entrega un modelo tipado de evidencia para la app/dashboard.
- **Panel y FotoHolo:** `MagiskStatusPanel` muestra las generaciones y frames confirmados; `FotoHoloRouteB` reconoce `dsp_block_applied` sin confundir publicación con procesamiento.
- **Handshake:** corregido el desacuerdo app/daemon (la app exigía proto 3 y el daemon ofrecía proto 2); HELLO ahora envía la versión negociada.
- **Regresión nativa:** el test del beacon verifica que el read-back de generación avanza únicamente con la finalización de bloques.
- **Negociación de protocolo:** test nativo nuevo comprueba que HELLO proto 2 devuelve `compatible=true` y `ready`.
- **Alcance:** el read-back acredita consumo del snapshot por el efecto y procesamiento de bloques; no acredita respuesta acústica, precisión del DAC ni mejora perceptual. Pendiente CI y validación en Moto G85.

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

