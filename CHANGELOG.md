# CHANGELOG — v2.5.51 (342) — NPE en el módulo
El motor NPE neuromórfico ahora corre en Ruta B (omega_effect); ABI v7: reinstalar el módulo.

# CHANGELOG — v2.5.50 (341) — EQ real en Ruta B
Los 10 sliders de EQ moldean el espectro en el módulo (antes solo la media como trim).

# CHANGELOG — v2.5.49 (340) — Corrección acústica de sala
Medición con micrófono (27 tonos 1/3 oct, latencia real) y corrección tonal en Ruta A y B; ABI v6 (reinstalar módulo).

# CHANGELOG — v2.5.48 (339) — Cadena 4D en el audio REAL
La v2.5.47 dejó la cadena 4D alcanzable (JNI + panel); esta la conecta al audio que de verdad suena y corrige dos defectos que solo aparecían al conectarla.
1. **Cableada a la ruta de audio (`nativeProcess` y `nativeProcessBlock`):** se alimenta con la señal SECA antes del DSP (la predicción necesita los transitorios sin procesar) y publica la escena anticipada una vez por bloque. Apagada por defecto: retorno inmediato, cero bits alterados.
2. **Diafonía antes del limitador:** `applyOutputPlanar` va justo antes del `SafetyLimiter` en ambas rutas, porque la cancelación puede subir picos y el limitador es el techo final.
3. **Dosímetro sobre la señal FINAL (`meterOutputPlanar`):** la cadena DSP puede subir el nivel respecto a la entrada, así que medir la entrada seca subestimaba la exposición. Si la medición de salida se detiene (cambio de ruta), vuelve sola a medir la entrada tras 64 bloques.
4. **Defecto real corregido (escena sin realimentación):** `publishAnticipatedScene` modulaba sobre su propia salida (el bus ya contenía la escena modulada) y la apertura se acumulaba bloque a bloque hasta saturar. Medido con el defecto reintroducido: el sub subía de 0.10 a **0.85**; corregido, queda acotado en ≤ 0.45. Ahora separa la base ajena de la salida propia.
5. **Defecto real corregido (suavizado del conductor):** `ExcitationConductor::modulate` arrancaba el suavizado desde 0 al activarse; con una base de sub 0.60 la salida caía a **0.10** antes de subir (salto audible). Ahora parte de la escena vigente.
6. **Seguridad de hilos y datos:** guardas no bloqueantes en alimentación, salida y publicación por si dos rutas JNI corren a la vez (init/shutdown esperan a que el audio salga antes de re-preparar o liberar la cadena); un bloque con NaN/Inf se descarta y no envenena el extractor.
7. **Medición del conductor con material realista:** con bombo con cuerpo + hats a 120 BPM la compuerta abre al 100 % (confianza 0.997); con ruido, tono sostenido, silencio y golpes irregulares queda en 0. Un clic aislado no abre la compuerta: es una señal demasiado escasa, no un defecto.
8. **12 pruebas** en la superficie (antes 6) con verificación por mutación (las pruebas de los dos defectos FALLAN si se reintroducen) y 16 en la cadena, todas en verde en host. El ensamblado Gradle/NDK del APK no se ejecutó en este entorno: lo confirma el CI de la etiqueta.

# CHANGELOG — v2.5.47 (338) — Cadena 4D CABLEDA punta a punta
Los seis módulos 4D existían y se probaban, pero el producto NO los alcanzaba: no se compilaban en la lib nativa, no había JNI ni control desde el dispositivo. Ahora sí.
1. **Superficie de control global (nueva):** `music_intelligence/FourDControlSurface` posee UNA instancia por proceso de `FourDExcitationChain` (el `SceneTargetBus` es singleton por proceso), se alimenta desde los mismos puntos que el motor IME y expone controles atómicos y estado. TODO APAGADO POR DEFECTO (identidad estricta).
2. **Enlazada al target nativo:** `FourDControlSurface.cpp` y `jni/ivanna_4d_jni.cpp` entran en `libivanna_omega.so`. Antes de esto los símbolos no existían en el APK: la función era inalcanzable desde el dispositivo.
3. **JNI + Kotlin (nuevos):** 15 entradas `nativeFourD*` en `Ivanna4DNative.kt` (carga perezosa por el punto único `NativeLibraryLoader`), con nombres verificados uno a uno contra el `.cpp`.
4. **Panel "Predicción 4D" (nuevo):** `FourDPredictionPanel.kt` con el deslizador de anticipación (5–10 ms), profundidad, háptica multibanda con escala, cancelación de diafonía y selector de criterio del dosímetro (WHO/NIOSH/OSHA), más telemetría en vivo (armado, confianza, periodo, amplitud háptica, dosis, LAeq, guiñada, ITD, escenas publicadas). Conectado a la pantalla principal.
5. **Defecto real encontrado y corregido (medido):** el detector de onsets del conductor exigía ΔtransientRate > 0.04, pero el extractor real produce saltos de **0.01667** por golpe (normaliza `onsets*inv*8`). El umbral quedaba POR ENCIMA de la señal: con música real el conductor nunca se armaba (medido: confianza 0.000, anticipación 0.000 con pulso perfecto a 120 BPM). Bajado a 0.008 (holgura >2x); medido: se arma, estima 500.0 ms y la anticipación llega a 0.83. El refractario de 40 ms sigue impidiendo el doble disparo.
6. **6 pruebas nuevas** en la puerta host (`test_4d_control_surface`): identidad bit-exacta apagada, apertura anticipada a 120 BPM, techo háptico ≤255, dosis finita, publicación real en el SceneTargetBus e inicialización idempotente.
7. Las 16 pruebas previas de `test_4d_ultra_chain` siguen en verde tras el cambio de umbral (sin regresión).

# CHANGELOG — v2.5.46 (337) — Cadena 4D de excitación anticipada
El campo espacial, la háptica y los armónicos ahora se abren 5–10 ms ANTES del golpe, no después.
1. **Conductor de excitación (nuevo):** `ExcitationConductor` estima el instante del próximo golpe desde el historial de IOI del `MusicFeatureExtractor` (media recortada + regularidad) y adelanta la apertura del campo en un `leadMs` de 5–10 ms, publicando en `SceneTargetBus`. Con confianza baja la salida es la entrada exacta (bit-exacto).
2. **Háptica multibanda (nueva):** `MultibandHapticMapper` reparte la vibración en cuatro bandas perceptivas independientes (sub 20–80 Hz, cuerpo 80–400 Hz, transitorio de ataque, textura de aire/densidad) con refracción por banda y amplitud 0–255 lista para la API háptica.
3. **Cancelación de diafonía (nueva):** `CrosstalkCanceller` recupera el campo holográfico por AUX/altavoces con la inversa regularizada del par (shuffler recursivo), ITD fraccional, paso-alto de reinyección a 120 Hz para no tocar el sub y rotación con la cabeza.
4. **Fusión 6DoF del rastreo (nueva):** `HeadTrackerFusion` cierra el pendiente del rastreador: filtro complementario (giroscopio rápido, acelerómetro para inclinación, magnetómetro para acimut), sesgo estimado solo en reposo y predicción por latencia de sensor para que el campo gire CON la cabeza.
5. **Dosímetro de exposición (nuevo):** `ExposureDosimeter` acumula dosis sobre ponderación A exacta (bilineal de la función racional, 3 biquads por canal) según WHO/H.870, NIOSH y OSHA, con límite de pico de 135 dB y carga que atenúa la háptica.
6. **Banco A/B (nuevo):** `NeuromorphicAbHarness` mide el aporte audible del motor neuromórfico con PRUEBA DE NULL: alinea por correlación cruzada, mide el residuo por bandas del Atlas y lo compara con el umbral de audibilidad.
7. **Medición honesta:** ponderación A medida contra la tabla IEC 61672-1 (error 0.00 dB a 20 Hz, 0.05 dB a 100 Hz, 0.00 dB a 1 kHz); dosis medida contra la fórmula H.870 (120 s a 80 dB(A) = 8,33 % exacto); tasa de intercambio de 3 dB verificada (60 s a 83 dB(A) = 8,33 %).
8. **16 pruebas nuevas** en CTest (4 suites) — total 284/284.
9. Todo apagado por defecto: sin encender la cadena no se altera un solo bit.

# CHANGELOG — v2.5.45 (336) — 4D adaptativo
La dinámica espacial 4D se adapta a la música: la profundidad escala con la densidad de golpes medida (techo = slider). Ruta A y B.

# CHANGELOG — v2.5.44 (335) — 4D temporal
Nueva dinámica espacial temporal (4D): el ancho lateral respira con la música — enfoque en cada golpe, apertura en las colas — reutilizando el TransientDetector existente. Centro y graves intactos, bit-exacto apagado. Ruta A y B, control en el panel y en el perfil 4D.

# CHANGELOG — v2.5.43 (334) — Sincronía háptica por ruta
La háptica de graves se sincroniza con la salida real: retardo por latencia de la ruta (Bluetooth por códec; cable/USB 0) más ajuste fino, y perfil 4D de un toque para AUX/DAC.

# CHANGELOG — v2.5.42 (333) — Retumbo háptico
La háptica de graves suma una capa de retumbo opcional (apagada por defecto) que sigue el sub sostenido entre golpes, con tope del 60 %.

# CHANGELOG — v2.5.41 (332) — Háptica de graves 4D
Nueva háptica de graves: el teléfono vibra con cada golpe de bombo/sub detectado en el audio procesado (Ruta A). Detector probado en JVM; apagado por defecto; no actúa en Ruta B.

# CHANGELOG — v2.5.40 (331) — 


# CHANGELOG — v2.5.39 (330) — 


# CHANGELOG — v2.5.38 (329) — 


# CHANGELOG — v2.5.37 (328) — 


# CHANGELOG — v2.5.36 (327) — Datasets SOFA / RIR / SAF: ahora SÍ se despliegan
1. **Causa raíz (APK):** `app/build.gradle.kts` tenía `ignoreAssetsPatterns += "*.sofa"` → el APK no llevaba NINGÚN SOFA (HRTF AES69 + 200 HpIR ARI) y `IvannaAssetProvider` extraía una carpeta `sofa/` vacía. Ahora los SOFA viajan en el APK (el APK crece por ello).
2. **Causa raíz (módulo Magisk):** `build.yml` excluía `system/etc/ivanna_omega/sofa/*` y `*.sofa` del zip, así que `customize.sh` imprimía «SOFA no encontrado»; además al árbol del módulo le faltaban los 200 HpIR de `sofa/ari/` (el despliegue recursivo «de ARI» nunca tuvo qué copiar). Se quitan las exclusiones, se añaden los 200 archivos y el CI exige que el zip contenga EXACTAMENTE los SOFA/RIR/HRTF del árbol (239 / 200 / 12) y el SAF.
3. **`IvannaAssetProvider` (sin root)** — resolución por dataset (módulo → `/data/adb` → APK extraído): antes, con un módulo viejo instalado no se extraía nada. Extracción con archivos `.part` + rename atómico, copia solo si el tamaño difiere y marcador `.deployed_v<versionCode>` escrito únicamente sin fallos (antes bastaba que existiera `kemar.ihr1` para no reintentar jamás). `status()` cuenta archivos reales frente a lo empaquetado.
4. **Controles y estado reales:** tarjeta «DATASETS — DESPLIEGUE» en Control Espacial (SOFA/HRTF/RIR/SAF con conteo, MB y origen) con botón «RE-DESPLEGAR DATASETS»; los textos «SAF no desplegado» / «Dataset: …» ya no solo miran rutas de root; `SpatialAudioPanel` inicializa SAF con la ruta resuelta en vez de `/data/adb/...` fija.
5. **Puerta nueva `scripts/check_datasets_deploy.py` (7.ª):** paridad APK↔módulo, cabeceras mágicas (HDF5/IHR1/RIFF, sin punteros Git-LFS), RIR = filas de `metadata.csv`, SHA-256 del índice HRTF, SAF idéntico en 3 sitios y ninguna exclusión de datasets en Gradle ni en los workflows. Verificada con prueba de mutación: falla con las tres regresiones originales.
6. **Pendiente en dispositivo:** instalar el APK/zip y confirmar en el panel «DATASETS» los conteos 239 / 12 / 200 + SAF. El tamaño del APK aumenta (~80 MB de SOFA).

# CHANGELOG — v2.5.35 (326) — 


# CHANGELOG — v2.5.34 (325) — 


# CHANGELOG — v2.5.33 (324) — 


# CHANGELOG — v2.5.32 (323) — 


# CHANGELOG — v2.5.31 (322) — 


# CHANGELOG — v2.5.30 (321) — 


# CHANGELOG — v2.5.29 (320) — 


# CHANGELOG — v2.5.28 (319) — Test de flags de usuario sin carrera
- test_user_flags_channel usa un bus único por instancia (fallaba intermitente en paralelo bajo ASan)

# CHANGELOG — v2.5.27 (318) — COGNITIVO 9–15 explica su estado real
- Eje líder distingue: sin motor, desactivado, midiendo, módulo externo, sin audio\n- Mapeo de índices UI↔JNI verificado para las 3 tarjetas (fases 9–15)

# CHANGELOG — v2.5.26 (317) — Sliders sin lag y cognitivo con estado claro
- SupremeAxesPrefs.save coalesce disco+daemon (150 ms) en vez de escribir ~40 claves por evento de arrastre\n- Pestaña COGNITIVO distingue: motor no cargado / desactivado / esperando audio

# CHANGELOG — v2.5.25 (316) — Touch fluido: sondeos fuera del hilo principal
- 12 bucles de telemetría JNI/IPC pasan a Dispatchers.Default (antes en el hilo de UI)\n- getprop térmico y extracción de RIR fuera del hilo principal\n- Comandos al daemon desde la UI se coalescen por acción (el último valor gana) en vez de cola ilimitada

# CHANGELOG — v2.5.24 (315) — Telemetría del sistema honesta
- Etiquetas de telemetría corregidas al layout real del motor; sin dato térmico ya no se asume FULL; temperatura real de batería

# CHANGELOG — v2.5.23 (314) — Experiencia auditiva viva
- Seleccionar un vídeo (o ELEGIR VIDEO) lo reproduce con el reproductor del sistema mientras el motor procesa el audio; antes la selección no hacía nada

# CHANGELOG — v2.5.22 (313) — Ficha de ingeniería
- docs/ENGINEERING.md: cifras medidas, decisiones de diseño, verificación y límites; enlace en README

# CHANGELOG — v2.5.21 (312) — Licencia Apache-2.0
- Se añade LICENSE (Apache-2.0) y NOTICE; el código propio queda legalmente reutilizable

# CHANGELOG — v2.5.20 (311) — Telemetría cognitiva sin valores inventados
- NativeBridge.safeGet* ya no devuelven constantes inventadas: array vacío si no hay snapshot vivo

# CHANGELOG — v2.5.19 (310) — Pantalla de motores sin porcentajes inventados
- EnginesStatusScreen muestra — cuando no hay telemetría nativa viva (antes 88/90/96%…)

# CHANGELOG — v2.5.18 (309) — Flyer en README
- README muestra el flyer promocional y la invitación a probar en dispositivos reales

# CHANGELOG — v2.5.17 (308) — Barra de posición del reproductor viva
- BridgePlayerCard recibe posición, duración y seek reales del reproductor (antes quedaba muerta con valores por defecto)

# CHANGELOG — v2.5.16 (307) — Controles muertos de la Ruta B: ahora llegan al audio del sistema
1. **Hallazgo:** la app solo abre el bus de control como LECTOR, así que `OmegaControlBus::publish()` desde el JNI devolvía `false`: los toggles Volterra H2, FastRPC cDSP y ATI (y los 5 controles del panel Atlas) solo cambiaban la Ruta A. El daemon, además, reconstruía `snapshot.flags` desde cero y borraba esos bits. Con Spotify/YouTube (Ruta B) esos controles no hacían nada.
2. **`include/omega_user_flags.hpp` (nuevo)** — canal real app → socket del daemon → estado → snapshot → `omega_effect`, con tabla blanca de bits (Volterra, FastRPC, ATI, 4 de Atlas, Unity) y arquetipo manual codificado en los bits 20..23. Lógica pura + cliente de socket (mejor esfuerzo, solo hilo de UI).
3. **`command_server.cpp/.h`** — comando `SET_USER_FLAGS` (rechaza claves fuera de la tabla blanca) y mezcla de los bits del usuario en `publishCurrentState()`; con máscara 0 el snapshot es idéntico al de v2.5.15 (no cambia el comportamiento hasta que el usuario toca un control).
4. **JNI** — `nativeSetVolterraEnabled`, `nativeSetFastRpcEnabled`, `nativeSetAtiEnabled`, `nativeSetUnityEnabled` y los 5 `nativeIme*` usan ese canal. Además Ruta B aplica `wfs_spread_scale` y `upmix_immersivity` del ADE (antes descartados).
5. **Pruebas:** 245 de 245 CTest en verde (8 nuevas en `test_user_flags_channel`, que compila el `CommandServer` REAL: comando JSON → snapshot publicado → lector, el bit sobrevive a otros comandos, claves no permitidas rechazadas, codificación de estilo contra el decodificador de `omega_effect`). ASan+UBSan limpio. **Pendiente en dispositivo:** con daemon activo, confirmar que el toggle cambia el audio de Spotify/YouTube; sin daemon (sin root) no existe canal de escritura desde la app hacia la Ruta B.

# CHANGELOG — v2.5.15 (306) — Acoustic Unity Engine medido de punta a punta
1. **`include/acoustic_unity_engine.hpp`** — de los 16 campos del contexto solo 2 llegaban al audio (`msWidenerMultiplier`, `rirWetDuckFactor`). Nuevo `ClosedLoopCoherenceMeter`: compara el original (pre-cadena) con la salida final y mide expansión lateral, correlación L/R, pérdida de cresta y exceso de agudos; reparte presupuestos spatial/room/exciter/dynamics con piso de fidelidad y recuperación con holgura. Revive `spectralCrestDb`, `sibilanceEnergyRatio`, `dynamicHeadroomDb`, `spatialBudget`, `harmonicBudget` y `roomReverbBudget` con medición real, y añade `dynamicsBudget`. Guardia sin espera de un solo escritor (el singleton se comparte entre instancias del efecto), interruptor `OMEGA_FLAG_UNITY_OFF` y rampa Hermite de arranque en frío (revive `getColdStartFactor`).
2. **Correcciones de fondo** — el ensanche pedía +30 % a toda mezcla; ahora solo a fuentes estrechas. Doble ducking de voz en la sala (0.25 × 0.25 = −24 dB en vez de −12 dB). `voice_score` del motor adaptativo en Ruta B era una constante inventada (0.72/0.35); ahora es la probabilidad VOICE medida. `exc_red` del ADE se escribía y publicaba pero nadie lo aplicaba.
3. **Ruta A (`ivanna_omega_jni.cpp`)** — ahora llama al Unity Engine, mide el resultado final y aplica `rir_wet_scale`, `ms_widener_scale` y `eq_tilt_db` del ADE con el criterio "el más conservador manda" (sin componer factores). La wet del RIR se escala en cada bloque (antes solo con snapshot nuevo). `ParametricEQ::setFatigueTilt()` aplica el tilt de fatiga en las bandas 5-7.
4. **Control de punta a punta** — `nativeSetUnityEnabled` / `nativeIsUnityEnabled` / `nativeGetUnityTelemetry` (+ bit del snapshot para la Ruta B) y toggle persistente "UNITY MEDIDO" con telemetría real en el panel de control.
5. **Pruebas:** 237 de 237 CTest en verde (18 nuevas: `test_unity_closed_loop` 13, `test_eq_fatigue_tilt` 5), 8 existentes del Unity Engine intactas; ASan+UBSan limpio y TSan sobre escritores concurrentes. Medición sintética en host, **sin escucha en dispositivo**: validar el timbre en un equipo real.

# CHANGELOG — v2.5.14 (305) — TinyML honesto y clasificador vivo
- Panel TinyML sin cifras inventadas: fuente real (CRNN/heurística) y tiempo medido\n- Clasificador del hilo de fondo publica scores y latencia a la UI

# CHANGELOG — v2.5.13 (304) — NAVEGACIÓN: rutas alias sin navegar durante la composición
1. **`MainActivity.kt`** — las rutas alias (`ope`, `binaural`, `telemetry`, `auditory`, `adaptive_dash`) llamaban `nav.navigate(...)` directamente en el cuerpo del `composable`: efecto secundario durante la composición que puede navegar varias veces o dejar una pantalla vacía parpadeando. Ahora usan `LaunchedEffect(Unit)` (misma redirección, ejecutada una vez).
2. **`ControlTabScreen.kt`** — los botones OPE y TELEMETRÍA de CONTROL navegan directo a `IvannaRoute.SOUND` / `IvannaRoute.SYSTEM` con `launchSingleTop`, sin pasar por el alias.
3. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.5.12 (303) — PROFILER: solo mediciones reales, sin cifras ni flags de adorno
1. **`NeonProfilerPanel.kt`** — el panel mostraba datos fijos o inventados: «HEAP HILO AUDIO 0.00 B», «NEON activo», GFLOPS estimados con un modelo de 1200 ops/muestra, «32 Q-Registers», y leía `cpuPercent` (que ya no se publica falso). Ahora muestra: latencia ida y vuelta medida; CPU de la app (delta de `Process.getElapsedCpuTime`); heap nativo (`Debug.getNativeHeapAllocatedSize`) y Java; CPU del dispositivo desde `/proc/cpuinfo` (NEON/ASIMD, FP16, dotprod, i8mm, bf16); y presupuesto DSP, salud de buffer, jitter, underruns, bypasses, anti-pop y resyncs de `OmegaMetrics`.
2. **Flags de compilación reales** — el card ARMV8 TARGET FLAGS afirmaba `-mcpu=cortex-a76 -march=armv8.2-a+fp16 -ffast-math -flto`, pero el CMake usa `-march=armv8-a+fp+simd -fno-fast-math` y no hay LTO. `CMakeLists.txt` inyecta ahora `BASE_FLAGS` como `IVANNA_NATIVE_FLAGS` y la UI las lee del binario con el nuevo `nativeGetBuildFlags()` (`ivanna_omega_jni.cpp`; «unknown» en builds host).
3. **Intrinsics**: descripciones corregidas (`vrecpeq_f32` es sólo estimación, `vdupq_n_s16` es difusión, no multiplicación) y etiquetas con los módulos que de verdad los usan.
4. **Verificación local:** la definición de CMake probada con Ninja y Makefiles; pasan jni_wiring, kotlin_wrapper_wiring, header_wiring, rt_safety, build_flags y docs_claims. Kotlin/NDK se validan en CI.

# CHANGELOG — v2.5.11 (302) — SAF: botón REPETIR TONO en la calibración HRTF
1. **`SaFEngine.replayStimulus`** y **`SaFCalibrationScreen`** — nuevo botón para volver a oír el tono actual sin responder ni avanzar la calibración (antes cada tono sonaba una sola vez, al avanzar, sin posibilidad de repetirlo).
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.5.10 (301) — EVOLUTIVO: kernel serializado y genoma publicado sin bloqueo
1. **`evolutionary_kernel_v2.cpp`** — `evo_initialize_population`, `evo_evolve_generation`, guardar y cargar estado se serializan con `g_evoMutex` (orden g_evoMutex → g_saveMutex). Antes INICIAR/PASO de la UI regeneraban la población mientras el hilo de fondo de `pd_engine` la evolucionaba: data race real sobre `g_population`.
2. **Mejor genoma por seqlock** (`g_bestSeq`/`g_bestGenome`): `evo_get_best_genome` — lo lee `audio_orchestrator` desde el procesamiento de audio — ya no puede ver un genoma a medio escribir ni bloquea; se publica tras init, cada generación y carga.
3. **Verificación local:** arnés + estrés de 3 s con 4 hilos (evolución, reinicios con cambio de población, lector de genoma, paso con convergencia): sin bloqueos, fitness finito, 110 M lecturas.
4. **Pruebas:** CTest en CI.

# CHANGELOG — v2.5.9 (300) — EVOLUTIVO: población y generaciones llegan de verdad al kernel
1. **`evolutionary_kernel_v2.cpp` / `.h`** — población activa en runtime (`evo_set_population_size`, 32..128, los mejores N del arreglo ordenado) y tope de generaciones (`evo_set_max_generations`; el paso con convergencia devuelve 0 al alcanzarlo). Antes POBLACIÓN y GENERACIONES eran constantes: el JNI hacía `(void)popSize`.
2. **`evolutionary_kernel_jni.cpp` / `IvannaNativeLib.kt`** — `nativeInitializeEvolution` ahora respeta ambos parámetros y nuevo `nativeSetEvolutionParams` los ajusta en caliente SIN reiniciar la población.
3. **`BrainScreen.kt` / `IvannaControlPanel.kt`** — los sliders llamaban `nativeInitializeEvolution` en cada evento táctil (CONTROL) o al soltar (mi 2.4.38), lo que regeneraba la población y borraba el progreso. Ahora usan el ajuste en caliente; rango real 32..128 (antes 10..200 aparentaba más de lo que el motor soportaba).
4. **Pruebas:** 2 tests nuevos en `test_evolutionary_kernel.cpp`; la lógica del kernel se verificó localmente con un arnés (clamp, elitismo con población reducida, parada exacta en el tope, sin reinicio al ajustar). El resto (Kotlin/JNI) se valida en CI.

# CHANGELOG — v2.5.8 (299) — ENGINE: AI ANALYSIS con género y confianza reales del NPE
1. **`OmegaMetrics.refreshFromNative`** — ahora publica género y confianza del NPE (`getDetectedGenre`, `getSynthClassify()[1]`) para el bloque AI ANALYSIS del panel ENGINE, que se quedaba en «— · 0 % confidence» salvo con el bridge reproduciendo.
2. **`IvannaBridgePlayer.kt`** — la confianza se leía de `getSynthClassify()[0]` (que es el id de cluster) en vez de `[1]` (confidence), así que mostraba 0 % o 100 %.
3. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.5.7 (298) — CONTROL: reconstrucción de realidad con telemetría real (sin cifras de relleno)
1. **`IvannaControlPanel.kt` → AcousticRealityReconstructionCard** — cuando no había datos nativos mostraba cifras fijas (realismo 88 %, sala 6.8×8.6×3.5 m, RT60 0.38 s, juez humano 90 %, homeostasis 96 %, gemelo digital 92 %, EVO 89 %…). Ahora lee `safeGet*OrNull` y muestra «—» hasta que el hilo del motor adaptativo (que alimenta al `AcousticRealityOrchestrator` con métricas de audio reales y publica cada ciclo) entrega un ciclo vivo; desde entonces todo son valores medidos. HUD NPE: CONF. ya no muestra un porcentaje cuando el género es «—».
2. **Cadena verificada:** adaptive_decision_engine → orchestrateCycle → stateBus → JNI (null si la secuencia no supera a la sembrada, v2.4.40) → UI.
3. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.5.6 (297) — BRAIN: telemetría perceptual con unidades y estado vacío honesto
1. **`BrainScreen.kt` → PerceptualTab** — mostraba 5 valores crudos sin unidad (RMS/Pico lineales, "CPU" que en realidad era ganancia objetivo) y quedaba vacía sin motor. Ahora presenta 9 magnitudes reales del motor adaptativo (RMS/Pico en dBFS, reducción de ganancia en dB, ganancia objetivo ×, compresión, exciter, ancho espacial, margen de seguridad y protección de voz en %) y un mensaje claro si el motor no responde.
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.5.5 (296) — ENGINE: la ganancia objetivo ya no se muestra como carga de CPU
1. **`OmegaMetrics.refreshFromNative` (mío, 2.4.33)** y **`IvannaBridgePlayer`** — usaban `nativeGetAdaptiveTelemetry()[3]` como CPU/DSP load, pero ese índice es `target_gain` (0..2): ganancia 1.0 aparecía como 100 % de carga. Se elimina; la carga DSP real sigue viniendo de `AudioThreadBudgetGuard` vía `PlaybackCaptureService`.
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.5.4 (295) — ADAPTIVE: escala de intensidad 0–100 hacia el motor nativo
1. **`ControlTabScreen.kt` / `BrainScreen.kt`** — `nativeSetAdaptiveControls` espera intensidad en 0..100 (`g_adaptiveUiIntensity` se clampa a 0..100, igual que `AdaptiveEngineScreen`), pero CONTROL y BRAIN enviaban 0..1: el motor recibía como máximo 1 % de intensidad. Ahora todas las llamadas usan 0..100.
2. **Pruebas:** sin toolchain Android local; validación en CI.

# CHANGELOG — v2.5.3 (294) — APK con firma estable (corrige "app no instalada" al actualizar)
1. **`.github/workflows/build.yml`** — el CI generaba un keystore nuevo en cada ejecución, de modo que cada APK salía firmado con una llave distinta (verificado: v2.5.0 y v2.5.1 tenían huellas de certificado diferentes). Android rechaza actualizar una app con otra firma y el instalador mostraba "app no instalada". Ahora el keystore se toma del secreto `ANDROID_KEYSTORE_B64` y todas las versiones futuras comparten la misma firma.
2. **Aviso de migración (una sola vez):** el APK instalado hoy está firmado con una llave efímera que ya no existe, así que v2.5.3 requiere desinstalar la versión anterior antes de instalarla. A partir de v2.5.3 las actualizaciones se instalan encima sin perder datos. Sin cambios de código DSP.

# CHANGELOG — v2.5.2 (293) — IVANNA Acoustic Unity Engine: coordinación global, reinyección armónica y calibración autónoma
1. **`include/acoustic_unity_engine.hpp` (nuevo, RT-safe, C++20)** — Implementación exhaustiva del motor de unidad acústica que transforma la cadena DSP aislada en un organismo acústico cooperativo con comunicación bidireccional de estado (Fases 1 a 7):
   - **Fase 1 (Cartografía Total)**: Intercambio unificado de métricas acústicas en tiempo real entre WFS, HRTF, RIR, EQ, Excitador y Limitador mediante un bus lock-free de snapshot arbitral.
   - **Fase 2 (Cambio de Paradigma)**: Acoplamiento cooperativo donde cada motor modula su intervención según lo que el resto de los módulos ya generó (evita sobre-procesamiento acumulativo).
   - **Fase 3 (Reinyección Diferencial Evolucionada)**: Separación espectro-temporal entre señal original inmutable (timbre, pegada transitoria, dinámica) y reconstrucción acústica inteligente (espacio, microdetalle, extensión armónica).
   - **Fase 4 (Motor de Coherencia Acústica)**: Evaluación en tiempo real de fase, correlación y balance energético multibanda para impedir competencia destructiva (WFS vs HRTF, RIR vs inteligibilidad, Excitador vs fatiga auditiva).
   - **Fase 5 (Reconstrucción Perceptual)**: Optimización de escena acústica para la audición humana (profundidad, focalización de centro, bajos orgánicos sin emborronamiento y agudos sedosos).
   - **Fase 6 (Calibración Autónoma)**: Arranque inteligente "Zero-Pop" con estabilización inmediata de parámetros desde el primer milisegundo de reproducción sin clics ni sobreimpulsos.
   - **Fase 7 (Optimización Extrema)**: Cero asignaciones dinámicas en el bucle de procesamiento, estructuras con `alignas(64)` para evitar *false sharing* en núcleos ARM big.LITTLE, y seguridad RT estricta probada bajo ASan/UBSan.
2. **`omega_effect.cpp` e `IvannaFusionCore.cpp`** — Cableado en la ruta de procesamiento de audio en tiempo real y persistencia JNI de los parámetros del Acoustic Unity Engine.
3. **`tests/test_acoustic_unity_engine.cpp` (nuevo, 8 pruebas)** — Validación integral de todas las fases del Acoustic Unity Engine, incluyendo medición de latencia acotada (< 2 ms), preservación de fase interaural, estabilidad numérica ante entradas patológicas e inmunidad contra clics.
4. **Pruebas:** 219 de 219 pruebas CTest pasando en verde (100%) bajo ASan + UBSan. Todas las 6 puertas estáticas de calidad en `[PASS]`.

# CHANGELOG — v2.5.1 (292) — Test del reinyector: familia new/delete completa (ASan en verde)
1. **`tests/test_differential_reinjector.cpp`** — el contador de asignaciones reemplazaba solo `operator new(size_t)`/`delete`, dejando `new[]`, nothrow y alineados con la implementación por defecto. Bajo ASan+UBSan eso producía `alloc-dealloc-mismatch` (new vs free) y ponía en rojo el job `CTest (ASan+UBSan)` (los tests 69-79 del reinyector). Ahora se reemplaza la familia completa sobre `malloc`/`posix_memalign`/`free`. Es un cambio SOLO de test: no se toca código de producción, ni umbrales, ni sanitizers.
2. **Pruebas:** 11 de 11 `DifferentialReinjector.*` OK bajo ASan+UBSan en local; `ProcessDoesNotAllocate` sigue verificando que `process()` no asigna. Binarios funcionalmente idénticos a v2.5.0; la versión se sube porque el release v2.5.0 es inmutable y no admite el build corregido.

# CHANGELOG — v2.5.0 (291) — Ruta A: reinyección DIFERENCIAL inteligente (causa raíz del peine)
1. **Causa raíz (medida, no supuesta):** el oyente recibe `Y(f)=X(f)·[1+g·H(f)·e^{-jωL}]`, con X el original (0 ms, imposible de retrasar o silenciar por API) y L≈20-40 ms la latencia de la reinyección. El peine depende de la parte del delta que es función estable del original (`H`), no del nivel. v2.4.50 usaba delta con ganancia fija 0.6: cuando el compresor/limitador atenúa, `delta=-k·x` y el rizado era 1.0-1.35 dB rms (3.5 dB con un delta espacial lineal). Un primer diseño con correlación temporal ρ NO bastaba (para ruido estacionario ρ(L)=0 y el peine existe igual): lo detectaron los propios tests y se sustituyó por coherencia espectral.
2. **`include/differential_reinjector.hpp` (nuevo, RT-safe):** Output = Dry + Σ_b g_b·Δ_b. (a) **Alineación**: estimación del retardo propio del DSP por correlación con interpolación parabólica, lectura con Lagrange cúbico, slew gradual (0.05 % de tono) y crossfade de 5 ms en saltos. (b) **Guardián anti-peine**: espectro cruzado Δ·X* (FFT 8192, retardo L real, corrección de sesgo 1/M y del solape de ventana) → ganancia coherente por banda; exige `g_b·c_b ≤ 10^(0.8 dB/20)−1`. Ataque 20 ms / recuperación 400 ms; sin datos arranca en peor caso (fidelidad > efecto). (c) **Tres bandas** Butterworth 4.º (180 Hz / 3.5 kHz, suma exacta): topes graves 0.25 / medios 0.70 / agudos 1.00. (d) **Inmersión perceptual** = intensidad × f(latencia Haas, coherencia, fase/confianza de alineación, energía del delta, voz). Cero malloc/locks/excepciones en `process()`.
3. **Cableado real:** `nativeReinject*` (JNI) → `DSPBridge.reinject*` → `PlaybackCaptureService` (usa el motor nativo; delta simple solo como respaldo). Latencia = `adaptiveLatency.measuredLatencyMs`; intensidad = rampa de arranque existente (`gEnd`); voz = `g_voice_protect_score` (VoiceProtectionController). Telemetría real al log `Reinject:` cada ~2 s. `check_rt_safety` ahora audita `nativeReinjectProcess` y `DifferentialReinjector::process` (36/36).
4. **Medido (host, 48 kHz, bloques de 320; `[MEDIDA]` en la salida de `test_differential_reinjector`):**
   - Peine rms, peor caso delta=−k·x: L=20/30/40 ms → v2.4.50 1.35/1.21/1.03 dB → **0.36/0.21/0.13 dB**.
   - Delta espacial lineal estacionario: 3.53 → 0.36 dB; chorus variante: 0.44 → 0.08; información incoherente: 0.13 → 0.05 (y pasa −10.1 dB del delta vs −21.1 dB el coherente: lo coherente se limita, lo nuevo pasa).
   - Graves 80 Hz a L = 2, 2.5, 3, 3.5 periodos: desviación 2.38/1.87/2.38/1.87 dB → **0.33/0.24/0.19/0.18 dB**.
   - Alineación: error de retardo ≤0.25 muestras (D=0, 3, 7.4, 21.75); delta residual con DSP=solo latencia ≤ −28.6 dB (−139…−223 dB en D ≤ 7.4).
   - Estabilidad L=8…150 ms ±3 ms de jitter: finito, pico ≤0.105, riesgo de peine ≤0.113 (tope 0.0965 + holgura de convergencia).
   - Clicks: paso máx./límite de pendiente = 0.998 en cambios bruscos de intensidad/latencia/topes/voz, 0.666 en salto de alineación 3→40.5 muestras (click > 1.5).
   - Inmersión (techo): nuevo@10 ms 0.81 → @40 ms 0.26; coherente@40 ms 0.13; con voz 0.13.
5. **Pruebas:** `test_differential_reinjector` (11 tests, incluye `process()` sin asignaciones y NaN/Inf/bloques gigantes). **211 de 211** host OK (Release); RT-safety, JNI wiring, header wiring, wrappers Kotlin, build flags y check_docs_claims PASS.
6. **Pendiente / límites honestos:** (i) Kotlin y JNI sin compilar contra el NDK/SDK en este entorno (JNI verificado con `-fsyntax-only` y stubs: 0 errores en el bloque nuevo) → valida CI. (ii) **Verificado por medición en host con señales sintéticas, NO en dispositivo**: las metas "sin eco corto / sin efecto acuoso / más profundidad / más escena" requieren escucha real y logcat (`Reinject:`). (iii) Los topes por banda y la intensidad base (0.6/0.45) son constantes; ningún slider de UI los controla todavía (no se añadió un control decorativo). (iv) Modelo de peine por banda con factor de cresta Rayleigh 1.6: aproximación, no cota dura.

# CHANGELOG — v2.4.50 (290) — Limitador: comportamiento real fijado por tests
1. **`tests/test_limiter_route_params.cpp` (nuevo, 5 tests)** — MEDIDO: con los parámetros que usan ambas rutas (umbral -1 dBFS, techo -0.1 dBFS) el SafetyLimiter es transparente bajo -1 dBFS (<0.05 dB), 0 dBFS sale a -0.66 dBFS y +3 dBFS a -0.55 dBFS. El hallazgo previo (-2.8 dB a 0 dBFS) solo ocurre con los defaults de `setParams()` (-4 dBFS), que ninguna ruta usa: no se toca el limitador. Los tests fijan transparencia, no aplastar masters calientes, dinámica sobre el umbral, techo inviolable con +5 dBFS/impulsos/ruido/percusión, y release limpio.
2. **Documentación:** conteo CTest sincronizado a 200 de 200 (README, LÉAME, telemetría) y seguimiento de los hallazgos 1-4 en `AGENT_CLAIMS.md`.
3. **Pruebas:** 200 de 200 host OK; check_docs_claims PASS.

# CHANGELOG — v2.4.49 (289) — Documentacion sincronizada con la suite CTest real
1. **`README.md` / `LÉAME.md` / `telemetry/ctest_summary.json`** — CI regenera `ctest_summary.json` con el total REAL de CTest y `check_docs_claims` (Fase 0) exige que la documentación coincida: la doc decía 182 mientras la suite real tiene 195 (incluye los 6 tests nuevos del excitador), lo que rompía la puerta Fase 0 del job CTest (fallo visto en el run del commit 0aef446). Conteos y ancla de §15 sincronizados a 195 (reproducido en local: FAIL con 7 desajustes -> PASS).
2. **Pruebas:** 195 de 195 host OK; check_docs_claims PASS tras regenerar el resumen desde el log real.

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
