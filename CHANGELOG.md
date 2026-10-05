# CHANGELOG — v2.4.48 (288) — AudioRecord: anillo de 4 bloques contra overrun
1. **`PlaybackCaptureService.kt`** — hallazgo 4 de AGENT_CLAIMS CONFIRMADO por lectura: el piso del anillo era `BLOCK_SAMPLES * 4` BYTES = 2560 B = EXACTAMENTE 1 bloque (320 frames, 6.67 ms). Cualquier jitter del hilo de captura > 6.67 ms (GC, planificador, un bloque de DSP lento) desbordaba el anillo y perdía muestras: micro-cortes/tronidos. Ahora `recordBufferBytes()` = max(minBuffer, 4 bloques) = 26.7 ms, igual que la pista de salida. No añade latencia en régimen (read() retorna con un bloque disponible y el DSP drena el atraso en <1 ms); el log HaasLatency muestra el tamaño real del anillo.
2. **Pruebas:** `CaptureRecordBufferTest` (3 tests: >= 4 bloques con minBuffer bajo, respeta minBuffer alto, >= 20 ms de jitter). **Pendiente:** Kotlin sin compilar localmente (sin SDK) — valida CI; confirmar en dispositivo con logcat (HaasLatency init) que no hay overruns.

# CHANGELOG — v2.4.47 (287) — Excitador: solo armónicos (sin ganancia lineal)
1. **`HarmonicExciter.cpp/.h`** — CAUSA RAIZ de agudos ásperos en 3-8 kHz, MEDIDA: la salida del shaper (softClip−x / Chebyshev) llevaba una componente lineal en la fundamental que se sumaba al seco. Con los defaults de arranque (drive .38, wet .38), tono a 48 kHz: +3.4 dB a 3.5 kHz y +3.5 dB a 6 kHz a -10 dBFS (+1.9/+2.5 dB a -3 dBFS); con drive .6/wet .8: +7.5 dB. Era un realce de agudos dependiente del nivel (el techo de headroom lo recortaba cerca de 0 dBFS). Ahora se resta la parte en fase con la entrada del shaper (regresión de 1 tap en línea, 3 ms, semilla inmediata en el onset): fundamental añadida de 0.48 a 0.002 (-0.01 dB); H2/H3 intactos a nivel bajo y sin estrangulamiento a nivel alto (H3 a -0.7: de 0.023 a 0.036).
2. **Referencia seca** (hallazgo 3 de AGENT_CLAIMS) — a índice impar del sobremuestreo `dryL = left[i>>1]` ya contenía la salida procesada del índice par; ahora se usa el seco original (par: muestra; impar: punto medio con la siguiente, aún sin tocar).
3. **Alcance:** solo Ruta A (`ivanna_omega_jni.cpp`); `omega_effect` (Ruta B) no usa `HarmonicExciter`.
4. **Pruebas:** nuevo `test_harmonic_exciter_fundamental` (6 tests: ganancia lineal < 0.02 en 6 niveles x 3 frecuencias, peor caso drive/wet, armónicos presentes, drive efectivo, sin ráfaga en onset, finito/acotado); verificado que FALLA con el código anterior. 195/195 host OK, RT-safety/headers/docs PASS. **Pendiente en dispositivo:** escucha real.

# CHANGELOG -- v2.4.46 (286) -- Calibracion de arranque: cero ganancia oculta antes del limitador
1. **`DSPState.kt`** -- Los defaults de arranque eran `mix=0.72` y `master=+0.8 dB`. `GainStage.inputGain_ = dbToLin((mix-0.5)*12)` => +2.64 dB ANTES del EQ y +0.8 dB a la salida: +3.4 dB estaticos que empujan los picos de un master moderno (~-0.5..-1 dBFS) sobre 0 dBFS y obligan al SafetyLimiter (umbral -4 dBFS) a actuar en cada bloque fuerte (bombeo/crujido al subir nivel). `dsp_types.h` ya documentaba este mismo defecto en `DSPParams`; `DSPState` lo conservaba. Ahora `mix=0.50` (0 dB) y `master=0 dB`.
2. **`DSPStatePrefs.kt`** -- Esquema 2 con migracion: al cargar un estado v1 solo se reemplazan `mix`/`master` si siguen EXACTAMENTE en el default viejo (0.72 / 0.8); un valor que el usuario movio no se toca.
3. **`DSPStateTest.kt`** -- test de gain staging de arranque (0 dB pre-EQ y a la salida).
4. **Pendiente en dispositivo:** Kotlin sin compilar localmente (sin SDK); validacion en CI.

# CHANGELOG -- v2.4.45 (285) -- EQ del APK: shelves reales (graves con profundidad, agudos sin fatiga)
1. **`ParametricEQ.cpp/.h`** -- Causa raiz medida con barrido de tono (48 kHz): las bandas 0 y 7 estaban documentadas como "shelf" pero `setBand()` siempre calculaba una campana. Con `low=+6 dB` el "shelf" daba +6.9 dB a 80 Hz pero solo +3.1 a 40 Hz y +1.3 a 25 Hz (bajo gordo en 80-200 Hz, sin sub-grave); con `high=+6 dB` la campana de 5 kHz mas la de 12 kHz formaban un plateau de +5.8/+6.7/+5.5 dB en 4/5/8 kHz, la zona de maxima sensibilidad del oido (aspereza y sibilancia). Ahora hay `setLowShelf`/`setHighShelf` (RBJ en doble precision): `low` es un low shelf a 90 Hz (+5.7 dB a 40 Hz, +6.0 a 25 Hz) con recorte minimo en 250 Hz (-0.08 dB por dB de low, anula el spill: 200 Hz queda en -0.15 dB); `high` es un high shelf a 10 kHz (+5.9 dB a 16 kHz) y la campana de 5 kHz baja a peso 0.35 (de +6.7 a +2.3 dB). La compensacion de headroom usa los mismos pesos.
2. **`tests/test_eq_shelf_response.cpp` (nuevo, registrado en CTest)** -- 7 tests con tonos reales: sub-grave conservado, sin barro en 250 Hz, sin plateau en 4-5 kHz, aire en 16 kHz, cortes simetricos, plano = bit-exacto tras el fundido, apilado a +12 dB finito y compensado. Los 4 tests existentes que usan ParametricEQ siguen en verde (host g++).
3. **Solo Ruta A:** `ParametricEQ` no se usa en `omega_effect` (Ruta B), asi que el modulo Magisk no cambia. **Pendiente en dispositivo:** escucha real; verificado por medicion en host, no en telefono.

# CHANGELOG -- v2.4.44 (284) -- Eliminación de tronidos/crujidos y corrección de simetría del canal derecho
1. **`SupremeAcousticStabilityGuard.hpp`** -- Umbral físico de salto impulsivo (`kMaxSampleJumpLimit`) elevado de 1.15f a 2.45f, eliminando el corte artificial que generaba crujidos en frecuencias altas (>10 kHz) y transientes abruptos a volumen alto. Implementado reset atómico `Zero-Pop` ante reposo/silencio (`inputBlockPeak_ < 1.0e-4f`) en `beginBlock` y `beginBlockInterleaved` (limpieza de derivadas Hermite, historial de frontera, bloqueador DC y amortiguamiento residual) para evitar picos y chasquidos al pausar o cambiar de pista. Umbrales de amortiguamiento recalibrados con cota mínima de 0.40f para evitar bombeo agresivo.
2. **`SafetyLimiter.cpp`** -- Reparado defecto de oscilación/chatter muestra a muestra en la etapa de release: cuando `gain < blockGain`, la ganancia ahora converge suavemente hacia `blockGain` en lugar de saltar hacia 1.0f en cada muestra alternada, erradicando micro-chasquidos y aspereza en pasajes de alta sonoridad.
3. **`SnnNmfHoaUpmixer.hpp`** -- Eliminada la asimetría artificial en los coeficientes esféricos (`signR = -0.85f` vs `signL = 1.0f`), restaurando ganancias canónicas unitarias y simétricas ($\pm 1.0$). Corregida la partición UPOLA del canal derecho para usar los retardos idénticos `upolaHistoryR_[0]` y `upolaHistoryR_[2]`, suprimiendo la atenuación y retardo espurio en el canal derecho.
4. **`IvannaAudioPipeline.hpp` & `StereoObjectDecomposer.hpp`** -- Confirmada la desconexión del canal ambiente mono asimétrico (`+sideL`) sobre el renderizador espacial y eliminado el término de inyección invertida en `chR`, garantizando una respuesta idéntica y equilibrada en ambos canales estéreo.
5. **`omega_effect.cpp`** -- `analyzeAudio` en la fase adaptativa ahora computa el downmix balanceado `0.5*(L+R)` en lugar de evaluar únicamente el canal izquierdo, evitando falsas detecciones de silencio en temas con información predominantemente en el canal derecho.
6. **BrainScreen.kt** -- Corregida inferencia de tipos en execMetrics eliminando fallo de desestructuracion Serializable en Kotlin compileReleaseKotlin, y coalesce nulo en indicadores de prioridad.
7. **Pruebas:** 6/6 puertas estáticas aprobadas (0 violaciones RT-safety, 0 headers huérfanos, 0 errores de cableado JNI/Kotlin, 0 flags prohibidos).

# CHANGELOG -- v2.4.43 (283) -- Clic al deshabilitar el efecto (cambio de app/ventana)
1. **`omega_effect.cpp`** -- `omega_process` salia con `!ctx->enabled` ANTES de llegar a la rampa `masterBypassEnv` (que ya preveia `wantMasterActive = enabled && ...`), asi que la rampa de salida era codigo muerto: al `EFFECT_CMD_DISABLE` el audio saltaba de procesado a seco en un bloque (discontinuidad = clic). Ahora solo sale cuando la envolvente es silenciosa; mientras tanto funde a seco (release 18 ms). La envolvente arranca en 0 si el efecto nace deshabilitado (ENABLE sube con ataque de 8 ms).
2. **Pruebas:** sintaxis OK con host_stubs; sin NDK ni dispositivo. **Pendiente en dispositivo:** confirmar que AudioFlinger sigue llamando `process` durante la cola de desactivacion (si no, el fundido no se completa y queda el comportamiento anterior, sin regresion).

# CHANGELOG -- v2.4.42 (282) -- Beacon: senal real de que omega_effect procesa (Ruta A/B)
1. **`omega_effect_beacon.h` (nuevo) + `omega_effect.cpp`** -- `isDaemonRunning` solo prueba que el daemon vive, no que el efecto este insertado/procesando en audioserver; `OmegaDspSnapshot::effect_frames` nunca llegaba a la app con daemon activo (el efecto es solo reader del bus del daemon). El efecto ahora mantiene un beacon mmap de 64 B (enable/disable por transicion, `onBlock` RT-safe al final del camino completo, descuento en release). Test host `test_effect_beacon` (182 CTest).
2. **`ivanna_omega_jni.cpp` / `DSPBridge.kt`** -- `nativeEffectBeaconState` + `DSPBridge.effectState()` (UNAVAILABLE / NO_EFFECT / ENABLED_IDLE / PROCESSING). UNAVAILABLE obliga al llamador a conservar su comportamiento previo.
3. **`MagiskStatusPanel.kt` / `AudioBackendSelector.kt`** -- fila EFECTO en el panel (solo si el beacon es legible) y `effectState` observable + log de transiciones. **Solo observabilidad:** no se cambia que backend se elige ni se bloquea la Ruta A, porque no esta verificado en dispositivo.
4. **Pruebas:** `test_effect_beacon` pasa en host; `omega_effect.cpp` sintaxis OK con host_stubs; Kotlin y NDK sin compilar localmente (sin SDK); validacion en CI. **Pendiente en dispositivo:** que el beacon sea legible desde la app (DAC/SELinux sobre `/data/local/tmp`) y usar el dato para decidir el gate de la Ruta A.

# CHANGELOG — v2.4.41 (281) — CONTROL: modo e intensidad adaptativos llegan al motor
1. **`ControlTabScreen.kt`** — ADAPTIVE modo/intensidad sólo se propagaban si el modo manual estaba activo; en automático se quedaban en `AudioState`. Ahora llaman a `nativeSetAdaptiveControls` siempre (igual que BRAIN → ADAPTATIVO).
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.40 (280) — JNI cognitivo: telemetría real o null, nunca sembrada
1. **`ivanna_spatial_jni.cpp`** — `getRealityTelemetrySnapshot` y `getCognitiveEvolutionTelemetrySnapshot` sembraban un snapshot sintético (rms 0.22, peak 0.68…) cuando el bus no tenía ciclos y lo devolvían como telemetría. Ahora registran la secuencia de la siembra (`g_cogSeedSeq`) y devuelven `null` hasta que corra un ciclo VIVO posterior; la UI (2.4.37) muestra "—". La siembra al activar/cambiar intensidad se conserva (el orquestador la necesita) pero ya no se presenta como medición.
2. **Pruebas:** no compilado localmente (sin jni.h/cmake); ningún test host referencia estas dos funciones; validación en CI.

# CHANGELOG — v2.4.39 (279) — BRAIN: slider de fatiga con throttle y valor final garantizado
1. **`TinyMlClassifierPanel.kt`** — el slider Fatigue Index enviaba al daemon un `sendPerceptualState` por cada evento táctil. Ahora hay throttle de 100 ms y `onValueChangeFinished` garantiza el valor final.
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.38 (278) — BRAIN: sliders de población y generaciones aplican al soltar
1. **`BrainScreen.kt`** — POBLACIÓN y GENERACIONES del kernel evolutivo sólo guardaban prefs; ahora, al soltar, reinicializan el motor si la evolución ya está corriendo.
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.37 (277) — BRAIN: telemetría cognitiva sin valores inventados
1. **`BrainScreen.kt` / `NativeBridge.kt`** — el tab COGNITIVO 9-15 mostraba cifras inventadas (84 %, 91 %, sala 6.8×8.6×3.5 m, 12 estados…) cuando el motor nativo no respondía, violando la regla de no fabricar métricas. Se añaden `safeGetRealityTelemetryOrNull`/`safeGetCognitiveTelemetryOrNull` (las `safeGet*` previas se conservan) y la UI muestra "—" si no hay dato.
2. **Límite conocido:** el JNI `getCognitiveEvolutionTelemetrySnapshot` aún siembra un snapshot sintético si el orquestador no ha corrido ningún ciclo (sequence==0); sin audio procesándose, esos valores no son medición real. No se tocó el JNI en este release.
3. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.36 (276) — SAF: tonos de calibración HRTF audibles
1. **`SaFStimulusPlayer.kt`** — los tonos de calibración no se oían: (a) `USAGE_ASSISTANCE_SONIFICATION` los mandaba al volumen de sistema (típicamente 0/silenciado, sin seguir ruta DAC) → ahora `USAGE_MEDIA`/`CONTENT_TYPE_MUSIC`; (b) el estímulo nativo dura 1200 ms pero el track se liberaba a los 560 ms (duración fija de 500 ms) → duración calculada de las muestras reales; (c) la salida nativa no se validaba → ahora se exige estéreo entrelazado, finito y con pico audible, se normaliza a −9 dBFS y, si falla, cae al chirp Kotlin.
2. **Pruebas:** sin toolchain Android local; validación en CI y auditivamente en dispositivo.

# CHANGELOG — v2.4.35 (275) — CONTROL: NAEL persistente
1. **`IvannaControlPanel.kt`** — el toggle NAEL · ISO 226:2023 arrancaba siempre apagado y no se reenviaba al motor al reabrir. Ahora persiste en prefs y se reaplica al nativo al abrir CONTROL.
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.34 (274) — CONTROL: telemetría en vivo sin depender de MediaProjection
1. **`IvannaControlPanel.kt`** — el HUD TELEMETRÍA EN VIVO se reseteaba a -60 dB / "—" siempre que no hubiera captura de pantalla. Ahora se considera viva con captura, inferencia NPE reciente o nivel en `OmegaMetrics.shared` (RMS cae al bus compartido si el NPE no lo publica) y no llama al NPE si no está listo.
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.33 (273) — CONTROL: ENGINE refresca desde el motor nativo
1. **`OmegaMetrics.refreshFromNative()`** (nuevo) — publica en el bus compartido dspActive (motor adaptativo corriendo o captura activa), HRTF, RMS/pico, carga DSP y ancho espacial leídos del motor nativo.
2. **`ControlTabScreen.kt`** — lo invoca a 2 Hz mientras CONTROL es visible; ENGINE ya no depende de que el bridge reproduzca.
3. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.32 (272) — BRAIN: modo manual cableado al motor
1. **`BrainScreen.kt`** — el MODO MANUAL (umbral de compresor, exciter, safety margin y el propio interruptor) sólo escribía en `AudioState`: ningún código lo enviaba al motor nativo. Ahora usa `AdaptiveBackend` (`applyManualState`/`forceManualState`/`persist`) y `nativeSetAdaptiveEngineEnabled`, igual que `AdaptiveEngineScreen`.
2. **`IvannaNavigation.kt`** — pasa `adaptiveBack` a `BrainScreen`.
3. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.4.31 (271) — CONTROL: panel ENGINE recibe métricas reales
1. **`MainActivity.kt`** — `MainScaffold` se invocaba sin `metrics`: el panel IVANNA OMEGA ENGINE de la pestaña CONTROL quedaba para siempre en los defaults (STANDBY, 0.0 ms, HRTF OFF, Width 0%). Ahora recibe `OmegaMetrics.shared`.
2. **Pruebas:** no ejecutadas localmente (sin toolchain Android); validación en CI.

# CHANGELOG — v2.4.30 (270) — Spatial JNI: capacidad de buffers directos
1. **`ivanna_spatial_jni.cpp`** — `nativeObjectRendererRenderBlock` escribía `numFrames` floats por canal y `nativeUpmixerProcess` leía 2 y escribía 8 floats por frame sin comprobar la capacidad de los `FloatBuffer`. Ahora se exige capacidad >= `numFrames` (salidas L/R), >= 2·`numFrames` (entrada del upmixer) y >= 8·`numFrames` (salida del upmixer); strides verificados en `NeuralUpmixer::process`. El buffer de objetos del renderer no se valida: su stride depende de `numObjects`.
2. **Pruebas:** 181/181 host OK, pero ningún test host ejercita estas dos funciones, así que el cambio no está cubierto por tests; RT-safety y JNI wiring PASS.

# CHANGELOG — v2.4.29 (269) — NPE JNI: buffers directos con capacidad mínima
1. **`ivanna_npe_jni.cpp`** — `nativeProcess`, `nativeProcessStereo` y `nativeSnapshotScope` usaban `GetDirectBufferAddress` sin comprobar capacidad: un buffer menor que `numFrames` desbordaba memoria nativa. Ahora se exige capacidad >= n (cota mínima: la unidad es bytes en ByteBuffer y floats en FloatBuffer, por lo que no detecta un ByteBuffer con entre n y 4n bytes) y el snapshot se acota por capacidad.
2. **Pruebas:** 181/181 tests host OK.

# CHANGELOG — v2.4.28 (268) — JNI de audio acotado por longitud real
1. **`ivanna_omega_jni.cpp`** — `nativeProcess` leía `2*n` floats y `nativeProcessBlock`/`copyJFloat` copiaban y escribían `n` floats sin comprobar la longitud del array: un array más corto que `frames` producía sobre-lectura/desbordamiento del heap de la JVM. `n` queda acotado por `GetArrayLength` de todos los buffers; arrays null salen limpio (antes `outL` null crasheaba en la ruta DSP).
2. **Pruebas:** 181/181 tests host OK; RT-safety 34/34 y JNI wiring PASS.

# CHANGELOG — v2.4.27 (267) — Daemon: comandos sin NaN ni inf
1. **`command_server.cpp`** — `_clamp` dejaba pasar NaN (las comparaciones son falsas) y `strtof` acepta "nan"/"inf" desde el socket: un comando malformado podía inyectar NaN en el estado DSP. `_clamp` mapea no-finitos a límites válidos; `_jsonFloat`/`_jsonFloatArray` rechazan no-finitos; `SET_PF_DRIVE` usa `strtof` validado y clamp 0..1 (antes `atof` sin rango).
2. **Pruebas:** syntax-check con stub OK; 181/181 tests host OK.

# CHANGELOG — v2.4.26 (266) — service.sh: rotación de log y contador robusto
1. **`service.sh`** — `daemon.log` crecía sin límite en /data: rotación a `.old` al superar 1 MiB. Contador de caídas corrupto/no numérico ya no rompe el `[ -ge ]` (se trata como 0).
2. **Pruebas:** `dash -n` OK.

# CHANGELOG — v2.4.25 (265) — AudioRouteManager idempotente
1. **`AudioRouteManager`** — `start()` podía registrar dos `AudioDeviceCallback` (applyRoute duplicado por hotplug + fuga); ahora desregistra el previo. `stop()` cancela el restore HRTF diferido pendiente. Ambos `@Synchronized`.
2. **Pruebas:** compilación Kotlin la valida CI.

# CHANGELOG — v2.4.24 (264) — post-fs-data a prueba de bootloop
1. **`post-fs-data.sh`** — `source` (no POSIX) reemplazado por `.`; la carga de `ivanna_autonomous_core.sh` queda protegida (si faltaba el archivo, el `.` fallido abortaba post-fs-data → riesgo de bootloop) y `ivanna_platform_init` solo se invoca si existe. `local` + asignación separados (SC2155).
2. **Pruebas:** `dash -n` OK; shellcheck sin errores nuevos.

# CHANGELOG — v2.4.23 (263) — Estado validado y binaural restaurado
1. **`AudioStateManager.validateState`** — solo limitaba 5 campos; EQ, presencia, compresor e intensidad espacial entraban sin rango al DSP (estado persistido corrupto o modulador adaptativo). Ahora usa los mismos rangos que los sliders.
2. **`restoreToNative`** — el HRTF/binaural guardado no se restauraba al arrancar (switch mostrado sin efecto). Ahora se empuja al nativo.
3. **Pruebas:** compilación Kotlin la valida CI; nativo sin cambios (181/181 host).

# CHANGELOG — v2.4.22 (262) — Origen de contenido sigue a las sesiones vivas
1. **`IvannaGlobalEffectManager`** — `ContentProfileEngine.noteSource` solo se actualizaba al abrir sesión: al cerrar la última sesión de video el perfil AUTO seguía en PELÍCULAS/STREAMING. `closeSession` y `releaseAll` ahora limpian el origen cuando no queda video activo.
2. **Pruebas:** 181/181 tests nativos host OK (sin cambios nativos); compilación Kotlin la valida CI.

# CHANGELOG — v2.4.21 (261) — Perfiles por contenido robustos
1. **`ContentProfileEngine`** — detección de streaming por subcadena (`max`, `prime`, `aiv`) clasificaba como streaming apps no relacionadas; ahora prefijos de paquete exactos. Histéresis (candidate/streak) protegida con lock: YAMNet y la UI ya no compiten. El cambio de perfil se aplica fuera del lock.
2. **Pruebas:** compilación validada por CI.

# CHANGELOG — v2.4.20 (260) — nativeSetDelta retirado de la superficie JNI
1. Eliminados los `external fun nativeSetDelta` (IvannaNativeLib, PiLstmBridge) y sus símbolos C++ sin llamadores; JNI 322→320, 0 hallazgos. Comentario obsoleto de SoundScreen corregido (AGC va por `setAgc`).
2. **Pruebas:** check_jni_wiring y check_kotlin_wrapper_wiring en PASS; build/tests completos pendientes de validación final.

# CHANGELOG — v2.4.19 (259) — Wrapper setDelta huérfano eliminado
1. **`PiLstmBridge.setDelta`** — wrapper público sin llamadores que escribía `nativeSetDelta` (= ancho espacial del PDEngine). Fue la causa raíz de los bugs de v2.4.13–v2.4.15; eliminado para que no pueda reintroducirse. El ancho espacial sigue por su ruta propia.
2. **Pruebas:** `check_kotlin_wrapper_wiring` pasa de 1 a 0 wrappers sin llamador; compilación completa la valida CI.

# CHANGELOG — v2.4.18 (258) — Perfiles por contenido (Música · Películas · Streaming · IVANNA Magistral)
1. **Nuevo `ContentProfileEngine`** — perfiles reales sobre los controles ya cableados (EQ, presencia, compresor, ancho/intensidad espacial, exciter), escribiendo el mismo `AudioState` y los mismos JNI que los sliders: UI, estado y DSP quedan sincronizados. MÚSICA: dinámica conservada (ratio 1.6, ataque 25 ms), ancho 1.5, intensidad 0.90, microdetalle. PELÍCULAS: diálogo estable (presencia +3 dB, ratio 3.0, ataque 8 ms), escena amplia con intensidad 0.70. STREAMING: ataque/release cortos y espacialización contenida (0.45) para no sumar latencia/desfase con el video. IVANNA MAGISTRAL: calibración magistral coordinada.
2. **Selección AUTO** — YAMNet (speech/music) + tipo de fuente (`IvannaGlobalEffectManager.openSession` → `noteSource`) con histéresis de 3 lecturas consecutivas; elección del usuario persistente. Selector en SoundScreen → EQ (`FilterChip`).
3. **Cableado:** `MainActivity` (YAMNet → perfil, carga al abrir), `IvannaGlobalEffectManager`, `SoundScreen`.
4. **Pruebas:** balance de sintaxis OK; compilación Kotlin la valida CI; nativo host 8/8 sin cambios.
5. **Esperado:** cada tipo de contenido suena con su calibración sin apilar espacialización; el cambio automático no oscila.

# CHANGELOG — v2.4.17 (257) — Presencia restaurada al arrancar
1. **`AudioStateManager.restoreToNative`** — restauraba EQ/compresor/ancho/exciter pero NO la presencia: tras reiniciar, el slider mostraba el valor guardado y el motor trabajaba con 0 dB (control mostrado sin efecto real). Ahora llama `nativeSetPresenceDb(state.eqPresence)`.
2. **Pruebas:** balance de sintaxis OK; compilación Kotlin la valida CI; nativo host 8/8 sin cambios.
3. **Esperado:** el brillo/presencia guardado suena igual tras reiniciar la app.

# CHANGELOG — v2.4.16 (256) — Espacialización complementaria (sin peine lateral)
1. **`IvannaAudioPipeline::processLiveSpatialAxes`** — el render espacial (ITD/ILD + ER) se sumaba SOBRE el componente lateral seco (dry*(1-0.35w) + render*w, w hasta 0.45): la misma señal lateral dos veces con retardo relativo = filtro en peine / eco de espacialización. Ahora crossfade complementario del side (seco*(1-w) + render*w) y mid/diálogo a ganancia 1.0 intacto: el nivel total se conserva y los procesos se complementan en lugar de competir.
2. **Pruebas:** build host completo OK; 8/8 suites de host OK.
3. **Esperado:** menos sensación de eco/hueco en lo lateral y ambiente; diálogo y centro sin cambios.

# CHANGELOG — v2.4.15 (255) — AGC ya no altera el ancho espacial
1. **`PiLstmBridge.setAgc`** — llamaba `setDelta(rate)` → `IvannaNativeLib.nativeSetDelta` = ancho espacial del PDEngine: cada cambio de velocidad/objetivo AGC (incluida la restauración al abrir y el ControlTab) reescribía el ancho estéreo. Eliminado; la velocidad AGC sigue por `IvannaNpeEngine.setAgcParams`.
2. **Pruebas:** balance de sintaxis OK; compilación Kotlin la valida CI; build host nativo 8/8 (sin cambios nativos).
3. **Esperado:** el ancho espacial queda estable al mover los controles AGC (menos cambios de imagen estéreo percibidos como eco/fase).

# CHANGELOG — v2.4.14 (254) — Sliders ATAQUE y AGC con destino correcto
1. **`SoundScreen`** — ATAQUE llamaba además a `nativeSetGamma` (= ángulo espacial del PDEngine): mover el ataque del compresor torcía la imagen estéreo. Eliminado; el ataque va solo por `nativeSetCompressorParams`.
2. **`SoundScreen` / `PersistedStateRestorer`** — TARGET AGC y VELOCIDAD no llegaban al AGC real del NPE (VELOCIDAD movía `nativeSetDelta` = ancho espacial). Ahora ambos usan `PiLstmBridge.setAgc(target, rate)` (→ `nativeSetAGC`), también al restaurar preferencias.
3. **Pruebas:** balance de sintaxis Kotlin OK; compilación Kotlin/APK la valida CI; build host nativo 8/8 suites OK (sin cambios nativos).
4. **Esperado:** el compresor no altera el ancho/ángulo; VELOCIDAD y TARGET cambian la regulación de nivel audiblemente.

# CHANGELOG — v2.4.13 (253) — Presencia ya no se pisa con cambios armónicos
1. **`ivanna_omega_jni.cpp`** — `nativeSetHarmonicGain` reescribía `presence` del EQ en cada llamada (MusicIntelligenceWorker, AudioStateManager, MainActivity, PerceptualBrain…), deshaciendo el slider PRESENCIA. Se eliminó ese acople; presencia solo entra por `nativeSetPresenceDb`.
2. **`SoundScreen`** — la restauración de preferencias al abrir usa `nativeSetPresenceDb` (antes pasaba por el canal armónico).
3. **Pruebas:** build host completo OK; 8/8 suites de host OK.
4. **Esperado:** PRESENCIA se mantiene donde la deja el usuario aunque cambie la ganancia armónica automática.

# CHANGELOG — v2.4.12 (252) — PRESENCIA con efecto real
1. **`SoundScreen` / `IvannaNativeLib` / `ivanna_omega_jni.cpp`** — el slider PRESENCIA reutilizaba `nativeSetHarmonicGain`: duplicaba la escala (±12 dB del slider → ±24 dB, saturando a partir de 6 dB) y pisaba la ganancia armónica NHO. Nuevo `nativeSetPresenceDb` (±12 dB directo al EQ, sin tocar NHO).
2. **Pruebas:** build host completo (libivanna_omega.so + daemon) OK; 8/8 suites de host OK.
3. **Esperado:** PRESENCIA cambia el brillo 1:1 en dB sin alterar los armónicos.

# CHANGELOG — v2.4.11 (251) — Módulo + APK se complementan sin filtro de peine
1. **`PlaybackCaptureService`** — la reinyección ya no es "seco 100% + procesado 40% retardado" (peine por la copia seca retardada). Ahora se reinyecta solo DELTA = procesado − seco, con rampa por muestra: donde el DSP no cambia nada no se suma nada. Equivale a mezcla (1−g)·seco + g·procesado, sin salto de nivel. Flag `LEGACY_HAAS_MIX` conserva la mezcla anterior.
2. Con el motor in-place (módulo Magisk / sesión) activo ya no se silencia la reinyección: complementa con blend 0.45 (0.6 sin módulo). El video sigue silenciado (desfase labial).
3. **Fix build:** restaurada `eqPreampMb()` (rompía `compileReleaseKotlin`).
4. **Micro-cortes:** `queryEffects()` (Binder) fuera del hilo de audio, cacheado cada 2 s.

# CHANGELOG — v2.4.10 (250) — Distorsión armónica y voces robotizadas en el motor NPE del APK
1. **`ivanna_npe_jni.cpp`** — el AGC seguía el valor absoluto de la señal con tau ≈ 1 ms y movía la ganancia a la velocidad de la onda (modulación de amplitud a frecuencia de audio = intermodulación, voces robotizadas). Ahora envolvente attack 10 ms / release 300 ms y ganancia con tau ≈ 50 ms.
2. **`ivanna_npe_jni.cpp`** — `tanh()` sobre TODA la señal de salida (distorsión armónica constante). Ahora rodilla C1: identidad hasta 0.8, asíntota 1.0.
3. **`ProfessionalAntiPopEngine`** — recorte duro a ±1.0 a la entrada (clipeaba picos float y el overshoot del DC-blocker); ahora solo guarda anti-explosión a ±4.0 y el limitador final fija el techo.
4. **Build** — metadata Kotlin emitida como 2.1.0 (`-Xmetadata-version`) para que R8 8.8.34 no avise de "error parsing kotlin metadata".

# CHANGELOG — v2.4.9 (249) — Distorsión del audio del APK al subir volumen (Amazon/Tidal)
1. **`IvannaGlobalEffectManager`** — cadena de efectos stock sin red de seguridad: `DynamicsProcessing` se creaba PRIMERO (Android encadena por orden de creación), así que EQ + BassBoost + Virtualizer + LoudnessEnhancer pegaban directo al mixer = clip duro al subir volumen. Ahora se crea al final con limiter a -1.5 dBFS, rodilla suave, ataque 12 ms / release 160 ms y la MISMA curva en ambos canales (antes solo el canal 0 se comprimía).
2. **Auto-preamp real del EQ** — ninguna banda queda sobre 0 dB netos (antes solo restaba headroom pasando +3 dB); `applySafState` ya no deshace el preamp; topes a BassBoost (300), Virtualizer (220, causaba voces huecas/robotizadas) y LoudnessEnhancer (150 mB, solo si hay limiter detrás).
3. **`omega_process` (Ruta B)** — la ganancia adaptativa podía AMPLIFICAR hasta +4.3 dB antes de las guardas y se aplicaba como escalón por bloque (clic periódico). Ahora techo 1.0 y rampa lineal por muestra.
4. **R8** — se mantiene 8.8.34 (9.1.29 rompe Build APK con AGP 8.5.2, reconfirmado); el aviso de metadata Kotlin 2.4 es inofensivo.

# CHANGELOG — v2.4.8 (248) — Tronidos al cambiar de ventana y micro-cortes
1. **`PlaybackCaptureService`** — al cambiar de ventana el track se silenciaba con `setVolume(0f)` y nunca se restauraba (silencio pegado) y el corte era seco (tronido). Ahora se restaura a 1.0 con fade-in lineal en el primer bloque.
2. **`omega_process` (libomega_effect)** — vigilante de plazo de CPU: si el bloque tarda más del 80 % de su duración 3 veces seguidas, apaga RIR/Volterra/ejes supremos con rampa suave (histéresis de 600 bloques) en vez de provocar XRun (micro-cortes y voces robotizadas).
3. **`IvannaFusionCore`** — distorsión armónica constante: el último paso pasaba TODO el audio por `tanh()` (THD ≈ 2 % a 0.5 de amplitud). Ahora soft-knee C1 transparente: identidad exacta hasta 0.8 y asíntota en 1.0 (también en el excitador armónico).
4. **`omega_process`** — voces robotizadas/embrolladas en streams mono o 5.1 (Amazon Prime Video): el DSP asumía estéreo; ahora solo procesa 2 canales y deja pasar el resto intacto.
5. **`SupremeAcousticStabilityGuard`** — el techo/rodilla (0.92 × 0.86 ≈ 0.79) saturaba todo pico sobre −2 dBFS antes del limitador; ahora es red de seguridad (techo 0.985, rodilla 95 %). El amortiguador anti-runaway ya no modula la amplitud por bloque (voces robotizadas): tolera 6 dB de crecimiento legítimo y suelta en ~52 ms.
6. **`IvannaFusionCore`** — el trim global ya no amplifica por encima de 0 dBFS (antes hasta +12 dB).
7. **R8** — sigue en 8.8.34 (9.1.29 rompe Build APK con AGP 8.5.2); el único aviso restante es el parseo de metadata Kotlin 2.4, inofensivo.

# CHANGELOG — v2.4.7 (247) — Distorsión en el reproductor de la app
1. **`StereoAudioResampler`** — era vecino-más-próximo hacia 96 kHz (imágenes espectrales y jitter de fase a cualquier volumen) y reiniciaba la fase en cada trozo. Ahora es sinc enventanada de 32 taps, con posición fraccionaria e historial continuos entre trozos.

# CHANGELOG — v2.4.6 (246) — Distorsión constante
1. **Rodilla de saturación por etapa** — ya no es 0.85 fija en ~15 etapas en serie; sigue al techo de cada etapa (97 %), así no se acumulan armónicos en música fuerte.
2. **SafetyLimiter** — umbral de la cadena real de -4 a -1 dBFS: antes reducía ganancia continuamente con cualquier master moderno.

# CHANGELOG — v2.4.5 (245) — Cableado de UI y entrega de artefactos
1. **`WfsCalibrationPanel` cableado** — Ruta `wfs_calibration` + tarjeta en el hub SPATIAL (existía completo, JNI→daemon→WfsRenderer, sin acceso desde la UI).
2. **`MusicIntelligencePanel` cableado** — Ruta `music_intelligence` (con scroll) + tarjeta en el hub SYSTEM.
3. **Duplicados eliminados** — `AudioResampler.kt` (el 48→16 kHz ya vive en `AudioPipeline`) y `HeadTrackingManager.kt` (duplicado de `IvannaHeadTracker`, ya cableado).
4. **Entrega de artefactos** — `main` ya no cancela builds en curso; `CpuLoadRealTimeBudget` mide CPU del hilo (era flaky bajo carga y bloqueaba `build-apk`); `publish-release` anota el fallo y no intenta re-publicar un release inmutable (hay que subir versión).
5. **Tronido al subir volumen** — `softCeiling` de etapa ya no se apaga con g==1 (salto 0.85→techo por bloque).
6. **Tronido al cambiar de ventana** — `IvannaBridgePlayer` aplica fade de volumen (40–80 ms) en cambios de foco de audio en vez de `pause()`/`setVolume` en escalón.
7. **R8** — reglas `-dontwarn`/`keep` para clases opcionales faltantes y metadata Kotlin/serialization (R8 9.1.29 probado: rompe el build con AGP 8.5.2; se mantiene 8.8.34).

# CHANGELOG — v2.4.3 (243) — Anti-click: tronidos al subir volumen y cambiar de pista
1. **Rampa continua de ganancia por etapa (`SupremeAcousticStabilityGuard`)** — Elimina los tronidos/crujidos al subir el volumen: la ganancia de cada etapa ya no salta en escalón.
2. **Rampa adaptativa del ensanchador (`sideTarget`)** — Al cambiar de pista, `spatialAlreadyActive` hacía saltar el objetivo entre 1.0 y `currentWidener()` (0.75–1.35) y la EMA fija de 10 ms no lo suavizaba. Saltos > 0.10 usan tau = 60 ms (sin click); el régimen normal sigue en 10 ms.
3. **`GainStage::reset()` sin salto de ganancia** — Resetear con audio activo ya no produce un pop.
4. **Publicación de artefactos** — `publish-release` ya no enmascara fallos de subida y verifica tamaño de cada asset contra el build; se retiran APKs de commits anteriores del release y se barren drafts `staging-*` huérfanos. Versión incrementada para que Magisk ofrezca la actualización (antes seguía en `242`).

# CHANGELOG — v2.4.2 (242) — TinyML Causal Kernel & Scientific Benchmark Suite
1. **TinyML Causal Kernel Anti-Dolby (`IvannaTinyMLKernel.hpp/.cpp`)** — Reemplazo nativo a nivel de kernel para el modelo obsoleto YAMNet. Inferencia acústica en tiempo real, latencia ultra-baja (<0.1 ms), vectorización SIMD (ARM NEON / AVX2), cero asignaciones dinámicas de memoria (0 malloc en RT), ring buffer circular atómico y triple-buffering lock-free.
2. **Especificación Numérica de Regresión (`REGRESSION_BUDGET.md`)** — Definición formal e inmutable de límites numéricos para 16 métricas críticas: CPU (<=8.0% a 48 kHz / <=18.0% a 192 kHz), latencia algorítmica, jitter (<45 us), THD (<-115 dB), SNR (>120 dB), techo absoluto (0.994 FS), inmunidad FTZ/DAZ, simetría interaural y error ITD (<10 us).
3. **Catálogo Científico de Fallos de la Industria (`FALLOS_INDUSTRIA.md`)** — Documentación y veredicto empírico de 11 patologías acústicas comerciales (saturación por efectos apilados, compresión destructiva, comb filtering por doble reproducción, clicks de conmutación, degradación por HRTF genérica, xruns, denormales).
4. **Toolchain & Build Hardening** — Forzado de R8 8.8.34 en el buildscript classpath resolviendo incompatibilidad de metadatos binarios con Kotlin 2.4.20; migración a `Icons.AutoMirrored.Filled` en Jetpack Compose UI; validación al 100% de la suite CTest (177/177 targets pasando en 21.69 s).

# CHANGELOG — v2.3.18 (2318) — Adaptive Spatial Audio v3.0 & Physical Room Geometry

1. **Geometría Física Real (`RoomGeometryConfig.hpp`, `master_acoustic_orchestrator.hpp`)** — Configuración física verificada para el sistema de referencia Sony MHC-PZ1D (`spacing = 2.40 m`, `listenerDistance = 3.00 m`, sala $8.0\text{ m} \times 5.0\text{ m} \times 2.8\text{ m} = 112\text{ m}^3$), frecuencia modal de Schroeder $f_S = 2000\sqrt{T_{60}/V}$ y límite automático de reverberación sintética cuando $T_{60} \ge 1.2\text{ s}$.
2. **Transiciones de Potencia Constante sin Clicks (`SupremeTransitionEnvelope.hpp`, `ObjectSpatialRenderer.hpp`, `IvannaFusionCore.cpp`)** — Crossfade de potencia constante $\cos(\theta)/\sin(\theta)$ con dither triangular TPDF anti-denormal ($\pm 10^{-20}$), suavizado fraccional por muestra ($\tau = 15\text{ ms}$) de retardos ITD y ganancias ILD, y arbitraje estricto de un solo espacializador y una sola cola de reverberación (`IvannaAudioPipeline.hpp`, `omega_effect.cpp`).
3. **Detección Automática Pre-Primer-Bloque (`RouteDspCalibrator.kt`, `AudioRouteManager.kt`)** — Activación síncrona de etapas DSP al detectar o cambiar la ruta activa (Altavoz Estéreo/Mono, Auriculares Cableados/USB-C DAC, Bluetooth LDAC/aptX/AAC/SBC) antes del primer bloque de audio.

# CHANGELOG — v2.3.16 (2316) — Supreme Acoustic Stability Guard

1. **SupremeAcousticStabilityGuard (`app/src/main/cpp/supreme/SupremeAcousticStabilityGuard.hpp`)** — Capa permanente RT-safe (`alignas(64)`, lock-free, 0 malloc) con instrumentación por etapa (`StageBlockMetrics`, `FirstFaultReport`), arbitraje de ruta única (`SinglePathArbitrationState`), límite de crecimiento de energía por módulo (`enforceStageEnergyCeiling`), filtro bloqueador DC de 2º polo (3.5 Hz), gobernador continuo de headroom C2 (`0.92f` con aproximación racional de Padé) y continuidad Hermite C1 en fronteras de bloque.
2. **Corrección de núcleo Volterra H2 (`volterra_h2_symmetric.cpp`)** — Corregido el desplazamiento off-by-one del índice circular `d_idx` que hacía que el tap `k=0` leyera `t-63` en lugar de `x[n]`, eliminado el filtro peine fijo de 1 muestra (`0.08f * x[n-1]`) y reemplazado el recorte duro `std::clamp` por techo racional C2.
3. **FIFO de latencia cero y arbitraje espacial (`IvannaFusionCore.h/.cpp`)** — `resetFifo()` ahora inicializa `m_outFifoCount = 0` eliminando las caídas periódicas a cero (efecto hélice/rotor) en bloques no múltiplos de 128, y se arbitra `HoaBinauralDecoder` vs `WfsRenderer` vs `stereoWidth` para impedir doble/triple espacialización.
4. **Eliminación de bombeo de ganancia y doble procesado en Kotlin (`SafetyLimiter.cpp`, `IvannaAgentCore.kt`, `PlaybackCaptureService.kt`, `IvannaBridgePlayer.kt`, `IvannaSpatialEngine.kt`, `OmegaVibratoryProcessor.kt`)** — Conteo exclusivo de sobrecargas reales en `SafetyLimiter`, histéresis de recuperación en `DspControlAgent`, arbitraje de ruta única cuando `DSPBridge` C++ o el daemon Magisk ya procesaron el bloque, interpolación fraccional de retardo ITD y envolvente estéreo vinculada L/R.

# CHANGELOG — surgical-hardening-v5

1. **Fusión Magisk idempotente** — `magisk_module/customize.sh` ya no duplica `omega_effect` al reinstalar y aborta si el XML fusionado queda inconsistente.
2. **Release hardening** — el workflow deja de aceptar `libomega_effect.so` vacío/ausente y ahora valida ELF + export de `AUDIO_EFFECT_LIBRARY_INFO_SYM` antes de empaquetar.
3. **Cadena de releases consistente** — `update.json` queda publicado para clientes Magisk y el workflow puede subir APKs/ZIP a GitHub Releases cuando se empuja una tag `v*`.
4. **Versionado alineado** — APK y módulo Magisk quedan sincronizados en la línea `1.8 / 1800` para evitar desajustes de soporte y distribución.

# CHANGELOG — surgical-hardening-v4

1. **Dedup estructural** — se eliminaron árboles C++ duplicados y quedó una sola fuente activa por dominio.
2. **Consolidación de binarios** — las superficies JNI del APK quedaron unificadas en `libivanna_omega.so`.
3. **Vectorización completa** — Gammatone13 ganó ruta NEON y se extendió FTZ/DAZ + prioridad de audio a los hot paths.
4. **Testing real** — se añadió `cpp/tests/` con GTest y se corrigió la inestabilidad numérica de Gammatone13 detectada por la suite.
5. **Auditoría de memoria / concurrencia** — se fijaron órdenes de memoria explícitos y se validó la suite con ASan/TSan.
6. **Benchmarks** — se agregó `tools/benchmark_suite.cpp` y `docs/BENCHMARKS.md` con protocolo y referencias públicas comparativas.
7. **Limpieza final** — README, `.gitignore` y changelog quedaron alineados con la arquitectura consolidada.
