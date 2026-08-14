# IVANNA OMEGA SUPREME

Sistema completo de procesamiento de audio para Android construido en tres
capas independientes pero coordinadas: un efecto global insertado en
AudioFlinger vía Magisk, un daemon nativo con scheduler de tiempo real, y
una aplicación de control con pipeline DSP propio. Todo el código corre en
el dispositivo, sin servidores externos.

[![CI](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml/badge.svg)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)

---

## Qué hace

La app intercepta y procesa el audio del sistema a nivel de AudioFlinger
(antes de que llegue al hardware), aplica una cadena de DSP con parámetros
configurables en tiempo real, y devuelve el audio procesado al stack del SO.
El procesamiento incluye: ecualización ISO 226, compresión dinámica,
excitador armónico, ensanchamiento estéreo, espacialización binaural con
HRTF medido, y reverberación de sala real por convolución con RIR medidas
en 200 salas físicas distintas.

El usuario controla todo desde la app. El daemon mantiene el estado y lo
publica en memoria compartida para que el efecto de AudioFlinger lo lea sin
bloqueos en el hot-path de audio.

---

## Arquitectura

```
Aplicaciones Android (Spotify, YouTube, llamadas, etc.)
        │
        ▼
AudioFlinger ──► libomega_effect.so    ← INSERT_ANY, UUID propio, estado
        │         (proceso audioserver)   por sesión, sin singleton global
        │                ▲
        │                │  OmegaControlBus: SHM mmap + seqlock + CRC32
        │                │  /data/adb/ivanna_omega/omega_control_snapshot
        ▼                │
ivanna_daemon ───────────┘   SCHED_FIFO pr=98, big cores, ionice RT
  @omega_daemon_socket        watchdog anti-bootloop, PID file, SO_REUSEADDR
  protocolo JSON + texto      mqa_monitor separado (auto-preset por app)
        ▲
        │  LocalSocket + JSON / texto plano
        │
App IVANNA (Compose, ~140 archivos Kotlin)
  libivanna_omega.so (~163 archivos C++17, JNI)
  Pipeline DSP: GainStage → HarmonicExciter → Compressor → ParametricEQ
                → StereoWidener → RirConvolver → SafetyLimiter
  Motores: Volterra H2, evolutivo, perceptual, SAF, neuromorfo
  Visualizador: FFT 64 bandas Bark, OpenGL
```

### Bus de control cross-process

`OmegaControlBus` (`include/omega_control_bus.h`) implementa un bus de
control entre el daemon y `libomega_effect.so` usando memoria compartida
con seqlock y CRC32:

```
Daemon (writer):                 omega_effect (reader, hilo de audio):
guard.fetch_add(1) ← odd        g1 = guard.load()  ← odd? reintentar
memcpy(&snapshot)               memcpy(&local_copy)
guard.fetch_add(1) ← even       g2 = guard.load()  ← g1≠g2? reintentar
                                 crc32 válido? aplicar parámetros
```

El snapshot (`OmegaDspSnapshot`) es un struct POD trivialmente copiable,
≤512 bytes, validado por magic `0x4F4D4543` + CRC32. Cada publicación
incrementa `generation`. El reader nunca bloquea: máximo 32 reintentos
de ~1 µs cada uno.

---

## Efecto AudioFlinger — libomega_effect.so

- Implementa el interfaz `effect_handle_t` de AOSP (misma API que los
  efectos de fábrica de cualquier dispositivo Android).
- Tipo `INSERT_ANY`: procesa el audio de toda sesión del sistema.
- **Estado por sesión**: cada llamada a `omega_create_effect()` asigna
  su propia instancia de `IvannaFusionEngine`; ningún estado global
  puede contaminar el audio de otra app.
- **Route Arbiter** en `omega_process()`: lee el `RouteMode` del snapshot
  antes de cada bloque. Si la ruta activa no es `SYSTEM_WIDE`, el efecto
  hace passthrough — sin doble procesamiento cuando la Ruta A
  (`nativeProcess`) está activa.
- **Procesamiento overlap-save** (`RirConvolver`) tras el motor de fusión:
  aplica la RIR de la sala seleccionada sin allocations en el hot-path.

---

## Daemon — ivanna_daemon

Binario ARM64 compilado con NDK, lanzado por `service.sh` tras el boot:

- `SCHED_FIFO` prioridad 98 — mismo nivel que los hilos de AudioFlinger
  en dispositivos sin ajuste especial.
- Afinidad a big cores detectados por `cpuinfo_max_freq` (no hardcodeado).
- `ionice -c 1` (clase real-time, nivel 4).
- Socket abstracto `@omega_daemon_socket` con protocolo dual:
  - **JSON** para la app (`OmegaEngineBridge`): comandos con respuesta
    enriquecida (`ok`, `applied`, `status`, `generation`, `route`,
    `consumer`, `error`).
  - **Texto plano** para scripts shell (`ivanna_control.sh`, `nc`).
- `@omega_command_socket` secundario para el bus de control (acceptLoop
  con SO_RCVTIMEO=150ms, hilo independiente).
- **Control Plane**: cada comando mutante actualiza `OmegaDspState` y
  llama a `publishCurrentState()` → el snapshot SHM se actualiza
  atómicamente antes de responder al cliente.

### Comandos de socket (JSON)

| Comando | Parámetros clave | Efecto |
|---|---|---|
| `SET_EQ_BANDS` | `gains[10]`, `listenPhon`, `refPhon` | ISO 226 por banda |
| `SET_PERCEPTUAL_STATE` | `compressor`, `spatialWidth`, `harmonicGain`, `loudnessTargetLuFS` | Parámetros perceptuales |
| `SET_INTENSITY` | `intensity` [0,1] | Intensidad global DSP |
| `SET_PF_PARAMS` | `params[13]` | Parámetros PF Engine en bulk |
| `SET_ADAPTIVE_STATE` | `targetGain`, `compAmount`, `excRed` | Estado adaptativo |
| `SET_ROUTE_PROFILE` | `bassBoostDb`, `dialogBoostDb`, `widenerMult` | Perfil de ruta |
| `SET_SAF_STATE` | `deltaEnergy`, `metricNorm`, `memory`, `gain` | SAF Φ_SAF^∞ |
| `SET_ROOM_RT60` | `rt60` [0..5], `wet` [0,1], `idx` | Sala por RT60 objetivo |
| `GET_ROOM_STATUS` | — | RT60/idx/wet/bypass actuales |
| `SET_ACTIVE_ROUTE` | `route` 0=OFF 1=IN\_PROCESS 2=SYSTEM\_WIDE | Árbitro de ruta |
| `GET_STATUS` | — | Estado completo DSP |
| `GET_HEALTH` | — | Estado del bus de control |
| `PING` | — | Confirmación de vida |

### Comandos de socket (texto plano)

`STATUS` · `PING` · `GET_TELEMETRY` · `RELOAD_PARAMS` · `SET_BYPASS:0/1`
· `SET_PRESET:nombre` · `SET_REVERB:0.0–1.0` · `SET_ROOM_RT60:segundos`
· `SET_ROOM_WET:0.0–1.0` · `GET_ROOM_STATUS` · familia `SET_PF_*`

---

## HRTF — espacialización binaural

### Datos

El proyecto incluye o referencia los siguientes datasets de respuesta en
frecuencia relacionada con la cabeza (HRTF):

- **MIT KEMAR** (`MIT_KEMAR_large_pinna.sofa`, `MIT_KEMAR_normal_pinna.sofa`):
  dataset de referencia académica con maniquí anecóico, micrófonos
  dentro del canal auditivo. Dos variantes de pinna.
- **ARI Database** (Austrian Research Institute for Artificial Intelligence):
  200+ sujetos medidos individualmente en cámara anecoica.
- **CIPIC** (UC Davis): sujetos con medidas antropométricas documentadas.
- **TU-Berlin QU KEMAR**: dataset anecóico a 0.5 m de distancia.
- **Headphone IRs**: AKG K271/K272, Beyerdynamic DT770/DT990,
  Sennheiser HD25/HD280/HD650 — para ecualización específica por modelo.
- Total: 216+ archivos SOFA / 73 MB en `app/src/main/assets/sofa/`.
- Dataset procesado propio: `hrtf_dataset.ihr1` (4.9 MB, formato binario
  IHR1) en `magisk_module/system/etc/ivanna_omega/`.

### Cargadores

- `HRTFBinLoader`: parser binario IHR1 propio, sin libsndfile.
- `SofaHRTFLoader`: parser SOFA (stub extendible).
- `HrtfConvolver`: convolución overlap-save, crossfade entre ángulos.
- `SyntheticHRTF`: fallback documentado cuando no hay dataset en disco.
- `HrtfSubjectSelector`: selección del perfil más cercano por medidas
  antropométricas usando la base de datos disponible.

### Optimizador SAF — Φ_SAF^∞

El modelo `SAF_model.json` (214 sujetos, K=7 dimensiones) define el
espacio latente PCA del manifold de HRTFs. El optimizador calibra el
perfil activo por retroalimentación perceptual del usuario:

```
p_{t+1} = Π_S^{G_t}( p_t + α_t · G_t⁻¹ · Δ_t )

α_t  = E_t / (E_t + ‖Δ_t‖²_{G_t} + λ‖Δ_t‖²_{M_t} + ε)
E_t  = ‖q_t − target‖²_{G_t}
Δ_t  = target_d − q_t
```

- **G_t**: métrica de Fisher derivada de las 214 mediciones SOFA.
- **Π_S**: proyección al subespacio estable — acota en [0.1, 2.0].
- **α_t**: paso adaptativo acotado — el denominador cuadrático previene
  pasos grandes cuando el error ya está bajo tracking.
- Convergencia típica: ≤5 iteraciones de feedback.

---

## RIR — reverberación de sala real

200 respuestas al impulso de sala medidas físicamente
(`magisk_module/system/etc/ivanna_omega/rir/`, 18 MB):

| Estadístico | Valor |
|---|---|
| Salas | 200 |
| RT60 mínimo | 0.276 s |
| RT60 mediana | 0.723 s |
| RT60 máximo | 2.500 s |
| Volumen mínimo | 38.6 m³ |
| Volumen máximo | 1 443.1 m³ |
| Formato | WAV estéreo PCM16, 16 kHz |

`metadata.csv` incluye dimensiones de sala (ancho/alto/fondo), posición
fuente y micrófono, distancia fuente-micrófono y RT60 por sala.

### RirConvolver

`spatial/RirConvolver.hpp/.cpp` implementa convolución overlap-save en
frecuencia con la RIR seleccionada:

- Resampleo 16 kHz → 48 kHz con filtro sinc Kaiser-7tap (offline, no en
  el hot-path).
- FFT radix-2 DIT Cooley-Tukey, N\_FFT=32768.
- Pre-normalización por energía de la RIR.
- Wet/dry atómico ajustable en tiempo real.
- `process()` sin malloc, sin I/O, sin locks del SO.
- Selección de sala por RT60 objetivo (`SET_ROOM_RT60`) o por volumen.

---

## Pipeline DSP nativo

`libivanna_omega.so` (~163 archivos C++17, ~80 000 líneas):

### Cadena base (`cpp/dsp/`, `cpp/include/`)

```
GainStage → HarmonicExciter (HP biquad + oversampling 2× con sinc)
         → Compressor (feed-forward, ataque/release configurables)
         → ParametricEQ (filtros biquad por banda, 10 bandas ISO 226)
         → StereoWidener (mid-side con control de ancho)
         → RirConvolver (sala medida, overlap-save)
         → SafetyLimiter (clipper suave con lookahead)
```

### Volterra H2 simétrico (`cpp/neuromorphic/volterra_h2_symmetric.cpp`)

Modelado no-lineal de segundo orden. El kernel h2[k1,k2] captura
interacciones entre muestras con retardo relativo, análogo a lo que hace
la cóclea con señales complejas:

```
y[n] = Σ_{k1} Σ_{k2} h2[k1,k2] · x[n-k1] · x[n-k2]
```

Delay lines por canal L/R, actualización atómica de kernels en tiempo
de ejecución (sin parar el audio). NEON intrinsics en ARM64.

### Motor evolutivo (`evolutionary_kernel.cpp`, `EvolutionaryEQ.cpp`)

Población de 128 genomas que controlan el timbre de la síntesis aditiva.
Función de fitness:

```
fitness = energía_media × (1 - 0.85 × varianza)
```

Favorece distribuciones espectrales suaves. Evoluciona bloque a bloque
a partir de señales reales del audio (loudness, transientes, contenido
espacial). Persistencia de población entre reinicios de la app.

### Motor perceptual (`PerceptualCortex`)

- Analizador psicoacústico de 24 bandas Bark.
- Tracker de fatiga auditiva con modelo de dosis de exposición (IEC
  61672 / NIOSH equivalente).
- Estimador de estado emocional (Valencia/Arousal) a partir de features
  espectrales.
- Mapeo automático a controles DSP (intensidad, EQ, compresor).

### NPE Engine (`cpp/neuromorphic/ivanna_npe_engine.cpp`)

- FIR 1024 taps con ventana Blackman-Harris.
- Oversampling 16× (48 kHz → 768 kHz) para procesamiento no-lineal sin
  aliasing de frecuencia.
- Cliente FastRPC para offload al Hexagon DSP de Qualcomm (experimental).

### YAMNet + AntiDolby

- TFLite YAMNet clasifica el contenido del audio (voz/música/graves/ruido).
- `anti_dolby.cpp` neutraliza la compresión comercial aplicada por
  plataformas de streaming, ajustado por los scores del clasificador.

---

## Módulo Magisk

```
magisk_module/
├── service.sh            watchdog del daemon: SCHED_FIFO, big cores,
│                         socket readiness via /proc/net/unix, mqa_monitor
├── post-fs-data.sh       anti-bootloop (counter+LAST_OK), setprop módulo
├── customize.sh          deploy binario + SAF_model.json + RIR dataset
│                         + permisos ELF + SELinux live via magiskpolicy
├── mqa_monitor.sh        auto-preset por app (Tidal→Flat, Spotify→Warm,
│                         YouTube→Spatial, juegos→Punch)
├── ivanna_control.sh     CLI: preset/status/bypass/room/sala desde shell
├── concert_mode.sh       preset Spatial + reverb 0.7 vía socket
├── sepolicy.rule         SELinux: untrusted_app ↔ daemon socket;
│                         audioserver → adb_data_file (SHM de control)
├── uninstall.sh          limpieza: daemon, monitor, logs, SHM, SELinux
└── system/etc/ivanna_omega/
    ├── SAF_model.json    214 sujetos HRTF, K=7 dimensiones PCA
    ├── hrtf_dataset.ihr1 4.9 MB, HRTF procesado propio
    └── rir/              200 WAV estéreo PCM16 16kHz + metadata.csv
```

### Anti-bootloop

`post-fs-data.sh` mantiene un contador de arranques en
`/data/adb/ivanna_omega/boot_count`. Si el daemon crashea 3 veces
consecutivas antes de que `service.sh` toque `LAST_OK`, el módulo entra
en safe-mode (disable). El contador se resetea cuando `LAST_OK` existe
al inicio del arranque (boot previo exitoso).

### SELinux

Reglas propias en `sepolicy.rule`, aplicadas live via `magiskpolicy`:
- `untrusted_app` puede conectar al socket abstracto del daemon.
- `audioserver` puede leer `adb_data_file` (el SHM de control en
  `/data/adb/ivanna_omega/omega_control_snapshot`).
- Fallbacks para `system_data_file`, `magisk_file`, `unlabeled` (distintos
  vendors etiquetan `/data/adb/` de forma diferente).

---

## App Android

~140 archivos Kotlin, Jetpack Compose:

- **~30 pantallas**: control DSP, calibrador ISO 226, calibración SAF,
  laboratorio de medición, benchmark, estado del módulo Magisk, selector
  de sala RIR, dashboard perceptual, editor de perfiles.
- **Captura del sistema** via `MediaProjection` (`PlaybackCaptureService`):
  cero allocations en el hilo de audio, buffers reutilizables,
  `THREAD_PRIORITY_URGENT_AUDIO`.
- **Reproductor propio** (`IvannaBridgePlayer`): decodificación con
  `MediaCodec`, cada bloque pasa por el pipeline DSP completo.
- **Detección de ruta** (`AudioRouteManager`): Bluetooth/AUX/USB/altavoz
  con perfiles de compensación por ruta.
- **Visualizador** (`visualizer/`): FFT propia de 64 bandas escala Bark,
  renderer OpenGL, datos reales del pipeline.
- **Calibrador ISO 226** (`Iso226Calibrator`): aplica curvas de igual
  loudness medidas a la EQ del daemon por feedback del usuario.
- **Primer lanzamiento**: al conectar al daemon por primera vez, la app
  envía un preset calibrado de entrada completo (spatial, harmonic, bass,
  widener, intensidad, ISO 226) vía socket, antes de que el usuario
  toque nada.

---

## CI / CD y supply chain

`.github/workflows/`:

- **build.yml**: APK + binarios nativos ARM64 (NDK 26, cmake 3.22), suite
  CTest host en paralelo (desacoplados — el APK se produce aunque los tests
  fallen por infraestructura).
- **supply-chain.yml**: SBOM SPDX-JSON y CycloneDX con Syft; análisis de
  vulnerabilidades con Trivy (falla solo en CRITICAL con fix disponible);
  firma con Cosign keyless OIDC (sin claves privadas, Rekor public log);
  SLSA L2 via `actions/attest-build-provenance`.
- `.github/dependabot.yml`: actualizaciones automáticas semanales de
  GitHub Actions y dependencias Gradle.
- GoogleTest 1.14.0 vendoreado en `tests/third_party/googletest/` — sin
  FetchContent, sin red, configure <1s, 100% determinista.

### Tests CTest host

| Test | Qué valida |
|---|---|
| `dsp_core_stability` | Sin denormales, Bark gammatone, estabilidad numérica |
| `test_control_frame_bus_stress` | SeqlockBus, generación monotónica, no corrupción |
| `test_audio_bus` | OmegaControlBus publish/read, validez CRC |
| `test_regression_tuning` | Regresión de parámetros SafetyLimiter/Compressor |
| `test_rir_dataset` | 200 salas, metadata.csv, WAV PCM16, sample rate |

---

## Compilar

**Requisitos**: JDK 17, Android SDK 35, NDK 26.

```sh
./gradlew assembleDebug
```

**Tests host** (sin dispositivo, sin NDK):

```sh
cmake -B build-tests -S app/src/main/cpp/tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests -j$(nproc)
ctest --test-dir build-tests --output-on-failure
```

**Verificar módulo en dispositivo**:

```sh
adb shell getprop persist.ivanna.daemon_active  # → 1
adb shell grep omega_daemon_socket /proc/net/unix
adb shell /data/adb/modules/ivanna_omega_supreme/ivanna_control.sh status
```

---

## Estructura del repositorio

```
app/src/main/java/com/ivanna/omega/    app Android (Kotlin, Compose)
app/src/main/cpp/                      motor nativo (C++17, JNI)
  daemon/                              daemon standalone ARM64
  spatial/                             HRTF, RirDataset, RirConvolver
  neuromorphic/                        Volterra H2, NPE, manifold coclear
  dsp/                                 cadena DSP base
  include/                             omega_control_bus.h, headers públicos
  tests/                               suite CTest host + third_party/googletest
app/src/main/assets/sofa/              216+ archivos SOFA (73 MB)
magisk_module/                         módulo Magisk (scripts, XML, SELinux)
  system/etc/ivanna_omega/             SAF_model.json, hrtf_dataset.ihr1, rir/
docs/                                  documentación técnica, ADR, auditorías
.github/workflows/                     CI/CD, supply chain
```

---

## Licencia y atribuciones

- Código del proyecto: © 2026 Luis Uriel Pimentel Pérez. Ver `LICENSE`.
- Datasets SOFA (MIT KEMAR, ARI, CIPIC, TU-Berlin): se rigen por sus
  respectivas licencias académicas; ver `docs/ATTRIBUTION.md`.
- Clasificador YAMNet: TensorFlow Lite, licencia Apache 2.0.
- GoogleTest 1.14.0 (vendoreado en tests): licencia BSD-3-Clause.
- Las marcas mencionadas (Qualcomm Hexagon, Magisk, Android) pertenecen
  a sus respectivos titulares. Este proyecto no está afiliado a ellos.

---

## Contribuir

Issues y PRs bienvenidos. Para cambios grandes, abrir un issue primero.
Los tests CTest host son la referencia de calidad mínima — cualquier PR
debe pasar la suite completa sin regresiones.
