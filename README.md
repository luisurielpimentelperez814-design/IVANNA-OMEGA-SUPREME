# IVANNA OMEGA SUPREME

Motor de procesamiento de audio para Android, construido como un sistema
completo de tres capas: un efecto global en AudioFlinger (módulo Magisk),
una app de control con DSP propio, y un daemon nativo con scheduler en
tiempo real. Todo el procesamiento es de código abierto y corre en el
dispositivo.

> Proyecto personal publicado como open source. La documentación técnica
> detallada vive en `docs/`. Los datos y afirmaciones de este README
> corresponden al código tal como existe en este repositorio.

## Arquitectura

```
Apps de terceros (Spotify, YouTube, ...)
        │
        ▼
AudioFlinger ──► libomega_effect.so        ← módulo Magisk (Efecto global,
        │           (DSP por instancia)      INSERT_ANY, UUID propio)
        │                ▲
        │                │ OmegaControlBus: SHM seqlock + CRC32
        │                │
Daemon: ivanna_daemon (SCHED_FIFO, big cores, ionice RT,
        socket abstracto @omega_daemon_socket, watchdog anti-bootloop)
        ▲
        │ LocalSocket + JSON
        │
App IVANNA (Compose) ──► libivanna_omega.so (JNI)
   32 pantallas UI      Pipeline DSP nativo: GainStage → HarmonicExciter
   Captura MediaProjection  → Compressor → ParametricEQ → StereoWidener
   Reproductor propio       → SafetyLimiter + motores cognitivos
```

## Capa 1 — Efecto AudioFlinger (módulo Magisk)

- `libomega_effect.so` implementa un efecto global (`INSERT_ANY`) con
  estado DSP **aislado por sesión de audio**: cada app que abre una
  sesión recibe su propia instancia de `IvannaFusionCore` — ningún
  singleton compartido puede contaminar el audio de otra aplicación.
- El bus de control (`include/omega_control_bus.h`) publica snapshots de
  parámetros en memoria compartida con seqlock + CRC32, leídos por el
  efecto en el hot-path sin locks. Si el daemon no está presente, el
  efecto conserva el último snapshot válido.
- El módulo instala `audio_effects.xml` válido sin sobrescribir la
  configuración OEM, con política SELinux propia (`sepolicy.rule`) y
  protección anti-bootloop por contador de arranques.

## Capa 2 — Daemon nativo

`app/src/main/cpp/daemon/ivanna_daemon.cpp` compila a un binario ARM64
estático que `service.sh` lanza tras el boot:

- Scheduling `SCHED_FIFO` (prioridad 98), afinidad a big cores
  (detectados por `cpuinfo_max_freq`), `ionice` clase real-time.
- Socket abstracto `@omega_daemon_socket` con protocolo JSON
  (intensidad, ruta, parámetros perceptuales, estado SAF, scores del
  clasificador).
- PID file, watchdog de socket, `SO_REUSEADDR`, cierre limpio por
  señal, y monitor MQA separado rastreado por PID file.

## Capa 3 — App + pipeline DSP nativo

El pipeline nativo (`libivanna_omega.so`, ~80 000 líneas de C++17)
incluye:

- **Cadena base**: `GainStage → HarmonicExciter (Biquad HP + oversampling)
  → Compressor → ParametricEQ → StereoWidener → SafetyLimiter`
  (`cpp/dsp/`, `cpp/include/`).
- **Motor adaptativo**: `AdaptiveDecisionEngine` con λ_t dinámico y RT60
  medido por `RoomSimulator`; la app lo alimenta con telemetría real del
  pipeline (RMS, bandas espectrales, fatiga estimada).
- **Cerebro perceptual**: `PerceptualCortex` — analizador psicoacústico
  de 24 bandas Bark, inferencia de estado (TinyML en Kotlin), tracker de
  fatiga auditiva con modelo de dosis de exposición, y mapeo a controles
  DSP.
- **Clasificador de contenido**: YAMNet (TFLite) alimenta al
  `AntiDolbyController` y al motor anti-dolby nativo
  (`anti_dolby.cpp`) con scores de voz/música/graves.
- **Audio espacial**: convolver HRTF con dataset propio en formato IHR1
  (`HRTFBinLoader`, `hrtf_dataset.ihr1`), interpolación por azimut,
  fallback sintético documentado, y renderer de objetos
  (`spatial/`). El optimizador SAF (Stochastic Adaptive Filter sobre el
  espacio latente PCA del dataset) calibra el HRTF por escucha activa
  con feedback del usuario (`SaFOptimizer`, `SaFCalibrationScreen`).
- **Motores neuromórficos** (`cpp/neuromorphic/`): banco de envolventes
  biquad por banda, upmixer neuronal, manifold coclear, neuronas LIF,
  sintetizador armónico (NHO), y Pi-LSTM.
- **Kernel evolutivo**: EQ evolutivo offline (`evolutionary_kernel.cpp`,
  `EvolutionaryEQ.hpp`) con persistencia de población y guardado
  periódico.
- **Visualizador**: FFT propia de 64 bandas escala Bark con renderer
  OpenGL (`visualizer/`).
- **Integración Hexagon DSP** (experimental, `cpp/hexagon/`): cliente
  FastRPC preparado para offload al DSP Hexagon de Qualcomm.

La app (`app/src/main/java`, ~25 000 líneas Kotlin) aporta:

- UI Compose con ~30 pantallas: control del DSP, laboratorio de medición
  (`IvannaLabMonitor`, pantalla de benchmark), calibrador ISO 226,
  calibración SAF, dashboard del cerebro perceptual, estado del módulo
  Magisk, selector de perfiles y editor.
- Captura de audio del sistema vía `MediaProjection`
  (`PlaybackCaptureService`) con **cero allocations** en el hilo de
  audio (`THREAD_PRIORITY_URGENT_AUDIO`, buffers reutilizables).
- Reproductor local propio (`IvannaBridgePlayer`) que decodifica con
  `MediaCodec` y pasa cada bloque por el pipeline DSP completo —
  incluido el motor neuromórfico y la protección de voz.
- Detección de ruta de salida (Bluetooth/AUX/USB/altavoz) con perfiles
  de compensación por ruta (`AudioRouteManager`).
- Perfiles de audio serializados (`res/raw/audio_profiles.json`,
  override en `filesDir`), protección de voz con perfiles
  (podcast/call/broadcast/whisper) y persistencia.

## Tests

- CTest host sobre el núcleo DSP: estabilidad numérica (sin denormales),
  gammatone, estrés del bus de control, regresión de tuning, dataset RIR
  (`app/src/main/cpp/tests/`).
- Tests unitarios JVM del estado de audio y del modulador adaptativo
  (`app/src/test/`).
- CI (`.github/workflows/build.yml`): build del APK + binarios nativos
  (NDK, arm64-v8a) y suite CTest en cada push.

## Estado de madurez

| Subsistema              | Estado    | Notas                                        |
|-------------------------|-----------|----------------------------------------------|
| Efecto AudioFlinger     | Funcional | UUID propio, estado por instancia            |
| Bus de control SHM      | Funcional | seqlock, CRC32, árbitro de ruta              |
| Daemon + watchdog       | Funcional | SCHED_FIFO, big cores, anti-bootloop         |
| HRTF convolver          | Funcional | dataset IHR1 propio o sintético con log      |
| Calibración SAF         | Funcional | feedback del usuario, optimizador latente    |
| Motor adaptativo        | Funcional | λ_t + RT60 dinámicos                         |
| Cerebro perceptual      | Funcional | Bark 24 bandas, fatiga, emoción              |
| App UI                  | Funcional | Compose, ~30 pantallas                       |
| Tests CTest host        | Parcial   | ver `app/src/main/cpp/tests/`                |
| Offload Hexagon DSP     | Experimental | cliente FastRPC presente                  |
| Benchmarks públicos     | Pendiente | la instrumentación existe (`IvannaLabMonitor`) |

## Compilar

Requisitos: JDK 17, Android SDK 35, NDK 26.

    ./gradlew assembleDebug

El wrapper descarga Gradle automáticamente. El módulo Magisk se genera
en CI como artefacto (`ivanna_omega_supreme.zip`); no se commitea para
mantener el repositorio liviano.

## Estructura del repositorio

    app/src/main/java/com/ivanna/omega/   app Android (Kotlin, Compose)
    app/src/main/cpp/                     motor nativo (C++17, JNI)
    app/src/main/cpp/daemon/              daemon standalone (ARM64)
    app/src/main/cpp/tests/               suite CTest host
    magisk_module/                        módulo Magisk (scripts, XML, SELinux)
    docs/                                 documentación técnica y auditorías

## Licencia y atribuciones

- Código del proyecto: ver `LICENSE`.
- `hrtf_dataset.ihr1` es generado por el proyecto. Datasets SOFA/RIR
  externos que se añadan deben respetar sus licencias y documentarse en
  `docs/ATTRIBUTION.md`.
- Este proyecto utiliza el clasificador YAMNet (TensorFlow Lite) para
  análisis de contenido; el resto del pipeline es implementación propia.
- Las marcas de terceros mencionadas pertenecen a sus respectivos
  dueños; este proyecto no está afiliado a ellos.

## Contribuir

Issues y PRs bienvenidos. Abre un issue antes de un cambio grande para
evitar trabajo duplicado.
