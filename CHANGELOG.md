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
