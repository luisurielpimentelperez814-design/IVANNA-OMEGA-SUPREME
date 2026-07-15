# IVANNA OMEGA SUPREME

Motor de procesamiento de audio adaptativo para Android, con módulo Magisk
opcional para aplicar la cadena DSP al audio del sistema.

- Autor: Luis Uriel Pimentel Pérez
- Licencia: Propietaria y confidencial
- Plataforma: Android 9+ (API 28), ABIs `arm64-v8a` y `armeabi-v7a`
- APK: `com.ivanna.omega` — versionName **1.8** / versionCode **1800**
- Módulo Magisk: `ivanna_omega_supreme` — **v2.0.0 / 2000**
  (los versionados de APK y módulo Magisk NO están sincronizados hoy)

> Este README describe el estado real del código en `main`, verificado por
> auditoría directa del repositorio. No describe intenciones ni planes
> futuros. Los múltiples documentos `*_REPORT.md`, `PHASE_*`, `COMPLETION_*`,
> `EXECUTIVE_SUMMARY.md`, etc. son notas de trabajo históricas y pueden
> contradecirse entre sí; este archivo tiene prioridad sobre ellos.

---

## Qué es el producto hoy

Dos rutas de audio independientes que comparten la mayor parte del DSP
nativo (`libivanna_omega.so` para la app, `libomega_effect.so` +
`omega_daemon` para el módulo Magisk), gobernadas por un
`AdaptiveDecisionEngine` que corre en un hilo de control a ~20 Hz y publica
un `AdaptiveState` vía seqlock MPSC lock-free.

### Ruta A — Reproductor propio (`IvannaBridgePlayer`)

Reproducción de archivos locales desde la app IVANNA:

```
Archivo local
 → MediaExtractor / MediaCodec (PCM)
 → DSPBridge.process() → nativeProcessBlock() (libivanna_omega.so)
   → ParametricEQ (8 bandas)
   → Compressor            (setRuntimeAmount ← ADE)
   → HarmonicExciter       (2× oversampling, HPF 2.4 kHz, setRuntimeReduction ← ADE)
   → StereoWidener         (M/S, crossover mono-safe de graves)
   → GainStage             (input trim + output gain, setRuntimeGain ← ADE)
   → PDEngine              (NHO armónico Volterra + HRTF sintético + EvolutionaryKernel)
   → SafetyLimiter         (−0.1 dBFS, lookup table, sin malloc en RT)
 → AudioTrack → DAC
```

El `AdaptiveDecisionEngine` modula en tiempo real `target_gain`,
`compressor_amount`, `exciter_reduction` y `spatial_width` sobre esta
cadena, a partir de RMS, Peak, GainReduction del SafetyLimiter y energías
por banda (low 80–200 Hz, mid 500 Hz–2 kHz, high 4–16 kHz) extraídas de
los envelopes IIR de 8 bandas de PDEngine.

### Ruta B — Apps externas vía módulo Magisk (opcional, requiere root)

```
Spotify / YouTube / Tidal / juego / navegador
 → AudioFlinger (HAL Android)
 → libomega_effect.so  (AudioEffect global instalado por Magisk)
   → applyAgc()  (ganancia adaptativa + publica métricas en shared memory)
 → omega_daemon (SCHED_FIFO 98, processLoop)
   → PFEngine (4 biquads: low-shelf 200 Hz, mid peak, high-shelf 8 kHz, presence 3.5 kHz)
   → Compressor          (mismo código que Ruta A, setRuntimeAmount ← ADE)
   → HarmonicExciter     (2× oversampling, HPF, setRuntimeReduction ← ADE)
   → StereoWidener       (M/S, mono-safe)
   → SafetyLimiter       (−0.1 dBFS, RT-safe)
   → aplica ai_runtime_gain_mul (target_gain ← ADE)
 → salida del sistema
```

Los tres parámetros del ADE viajan por `ai_runtime_gain_mul`,
`ai_runtime_comp_amount` y `ai_runtime_exciter_red` en shared memory
(`omega_shared.h`). `spatial_width` **no tiene efecto en la Ruta B**: el
daemon no incluye PDEngine/HRTF, es un gap conocido.

Sin el módulo Magisk cargado, la app también expone
`IvannaGlobalEffectManager` (AudioEffect API estándar de Android + broadcast
`OPEN_AUDIO_EFFECT_CONTROL_SESSION`, estilo Wavelet/Poweramp), pero solo
funciona para apps que anuncian su sesión de audio y solo aplica los
efectos publicados por el HAL.

### Clasificador de contexto (YAMNet)

`assets/yamnet.tflite` se carga con TensorFlow Lite (`org.tensorflow:tensorflow-lite:2.14.0`).
`YamnetClassifier.kt` produce una categoría de sonido + confianza que
alimenta al `AdaptiveDecisionEngine` (loop cerrado
YAMNet → ADE → daemon/DSP). El tflite se marca como `noCompress` en Gradle.

### UI

App en Kotlin + Jetpack Compose (`material3` 1.2, Compose UI 1.6, compiler
extension 1.5.14). `IvannaControlPanel` da los sliders y presets,
`EngineStatusCard` muestra estado DSP, `IvannaLab` expone métricas.
`PlaybackCaptureService` usa MediaProjection para captura de audio de
sistema (análisis y visualizador OpenGL con shaders en `assets/shaders/`).

---

## Qué NO está terminado hoy

| Componente | Estado real en `main` | Nota |
|---|---|---|
| Ruta B — `spatial_width` adaptativo | Sin efecto | No hay PDEngine/HRTF en `omega_daemon`. |
| `IvannaLab` (THD / IMD / LUFS / LRA / SNR / TruePeak) | Skeleton | Tipos compilables; medición real limitada (IMD solo test SMPTE 250 Hz / 8 kHz). |
| `CloudSyncManager` (Firebase) | Requiere setup externo | Firebase se inicializa por `FirebaseOptions.Builder` sin `google-services.json` — hay que proveer credenciales antes de que Firestore/Auth funcionen. |
| `VoiceController` | Huérfano | Existe en código, no cableado al flujo principal. |
| Hexagon DSP / FastRPC (`hexagon/`, `ivanna_fastrpc_client.cpp`) | Presente, no productivo | Se compila dentro de `libivanna_omega.so`, no hay offload real al DSP Hexagon. |
| NPE neuromórfico (`ivanna_npe_engine`, cochlear manifold, Volterra H2) | Corre, sin impacto cuantificado | Se inicializa y procesa, pero no hay medición A/B publicada de su aporte audible. |
| USB DAC / AUX Reference Mode | Ausente | `UsbAudioProManager.kt` presente, sin flujo de referencia. |
| Head tracking 6DoF | Parcial | `IvannaHeadTracker` existe; fusión de sensores completa está en RELEASE_NOTES como pendiente de v1.1. |
| `mqa_monitor.sh` (auto-preset MQA en Magisk) | Sin verificación en vivo | Script existe, sin evidencia de prueba en dispositivo real. |

---

## Arquitectura (resumen)

```
App Android (Kotlin + Compose, minSdk 28, targetSdk 35)
├── ui/                UI Compose (IvannaControlPanel, EngineStatusCard, ...)
├── audio/             Motor de reproducción, captura y ruteo (IvannaBridgePlayer,
│                      IvannaGlobalEffectManager, PlaybackCaptureService, ...)
├── ai/                YAMNet, LearningBias, SpectralClassifier, AdaptiveLearning
├── dsp/               DSPBridge / DSPState / DSPViewModel (puente Kotlin ↔ JNI)
├── magisk/            OmegaDaemon, ShmManager, OmegaEngineBridge
├── neuromorphic/      IvannaNpeEngine, PiLstmBridge
├── spatial/           IvannaHeadTracker, IvannaSpatialEngine
├── visualizer/        OpenGL ES renderer (GLSL en assets/shaders/)
└── core/              CloudSyncManager (Firebase), PresetManager, ParameterStore, ...

Nativo (C++17, NDK 25.1.8937393, CMake 3.22.1)  →  libivanna_omega.so
├── dsp/               ParametricEQ, Compressor, HarmonicExciter,
│                      StereoWidener, GainStage, SafetyLimiter
├── spatial/           spatial_engine, room_model, hrtf_convolver,
│                      ivanna_head_tracker, ivanna_object_renderer
├── neuromorphic/      neuro_cochlear_manifold, volterra_h2_symmetric,
│                      ivanna_neural_upmixer, ivanna_npe_engine
├── hexagon/           ivanna_fastrpc_client (no offload real)
├── experimental/adaptive_engine/  AdaptiveDecisionEngine + tests
├── audio_control_plane, audio_orchestrator, pd_engine, phase_oracle
├── shm_hyperplane, omega_daemon (fuente compartida con la Ruta B)
└── jni/               puentes a Kotlin

Módulo Magisk (magisk_module/, requiere root)
├── module.prop                v2.0.0 / 2000
├── customize.sh               instalación idempotente
├── post-fs-data.sh            arranque temprano
├── service.sh                 arranca el daemon y aplica preset guardado
├── ivanna_control.sh          CLI de control
├── mqa_monitor.sh             watcher MQA (no verificado en vivo)
├── concert_mode.sh
├── system/etc/audio_effects_ivanna*.xml
└── vendor_base/sku_*.xml      overlays para SKUs Blair / Holi
```

---

## Build

```bash
# APK debug
./gradlew assembleDebug

# APK release (proguard on, firmado con debug key por defecto — no distribuir así)
./gradlew assembleRelease

# Tests DSP nativos, host, sin NDK (los mismos que CI ejecuta como gate rápido)
cmake -B build-tests -S app/src/main/cpp/tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests -j
ctest --test-dir build-tests --output-on-failure
```

Toolchain: NDK 25.1.8937393 · CMake 3.22.1 · AGP 8.5.1 · Kotlin 1.9.24 ·
Java/JVM target 17 · compileSdk 35 · minSdk 28.

Flags C++ activos: `-O3 -fno-fast-math -fno-associative-math -ffp-contract=off`
+ (`arm64-v8a`) `-march=armv8-a+fp+simd -funroll-loops -fno-exceptions -fno-rtti`
o (`armeabi-v7a`) `-march=armv7-a -mfpu=neon`. `-ffast-math` está prohibido
a propósito (rompe NEON en SD8 Gen2/3 y genera NaN).

---

## CI

`.github/workflows/build.yml`:

1. **`test-native-dsp`** — compila `app/src/main/cpp/tests` en host y corre
   CTest (gammatone numerical stability, denormals low level, DSP core
   stability). Gate rápido antes del build de Android.
2. **`build-apk`** — instala Android SDK y NDK manualmente y produce el APK.
3. Validación de release: verifica que `libomega_effect.so` es un ELF válido
   y exporta `AUDIO_EFFECT_LIBRARY_INFO_SYM` antes de empaquetar. Publica
   APK y ZIP a GitHub Releases al empujar una tag `v*`; `update.json` queda
   publicado para clientes Magisk.

---

## Advertencias importantes

- El APK release usa la **debug signing key** por defecto. No apto para
  distribución pública sin sustituir el `signingConfig`.
- La app declara permisos protegidos (`CAPTURE_AUDIO_OUTPUT`,
  `PACKAGE_USAGE_STATS`, `READ_LOGS`, `MEDIA_CONTENT_CONTROL`,
  `BIND_AUDIO_EFFECT_SERVICE`) que solo se conceden en dispositivos rooteados
  o con firmas de sistema. En un teléfono estándar, varias funciones caerán
  al modo sin root (`NoRootAudioProcessor`, `IvannaGlobalEffectManager`).
- La Ruta B requiere Magisk y un dispositivo compatible; instalar el módulo
  Magisk puede impedir el arranque si el HAL de audio no acepta el
  AudioEffect global — el script `service.sh` incluye watchdog, pero
  respalda `boot.img` antes de instalar.
- `CloudSyncManager` **no** trae un proyecto Firebase real: el `FirebaseOptions`
  se construye manualmente y hay que proveer `apiKey/applicationId/projectId`
  antes de que la sincronización funcione (ver comentarios en el archivo).
