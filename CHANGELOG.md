# CHANGELOG — v2.4.5 (245) — Cableado de UI y entrega de artefactos
1. **`WfsCalibrationPanel` cableado** — Ruta `wfs_calibration` + tarjeta en el hub SPATIAL (existía completo, JNI→daemon→WfsRenderer, sin acceso desde la UI).
2. **`MusicIntelligencePanel` cableado** — Ruta `music_intelligence` (con scroll) + tarjeta en el hub SYSTEM.
3. **Duplicados eliminados** — `AudioResampler.kt` (el 48→16 kHz ya vive en `AudioPipeline`) y `HeadTrackingManager.kt` (duplicado de `IvannaHeadTracker`, ya cableado).
4. **Entrega de artefactos** — `main` ya no cancela builds en curso; `CpuLoadRealTimeBudget` mide CPU del hilo (era flaky bajo carga y bloqueaba `build-apk`); `publish-release` anota el fallo y no intenta re-publicar un release inmutable (hay que subir versión).
5. **Tronido al subir volumen** — `softCeiling` de etapa ya no se apaga con g==1 (salto 0.85→techo por bloque).
6. **Tronido al cambiar de ventana** — `IvannaBridgePlayer` aplica fade de volumen (40–80 ms) en cambios de foco de audio en vez de `pause()`/`setVolume` en escalón.
7. **R8** — R8 9.1.29 (mínimo para Kotlin 2.4) + reglas `-dontwarn`/`keep` para clases opcionales faltantes y metadata Kotlin/serialization.

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
