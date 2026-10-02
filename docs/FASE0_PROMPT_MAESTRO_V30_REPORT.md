# INFORME DE INGENIERÍA — FASE 0 Y VERIFICACIÓN (PROMPT MAESTRO v3.0)

## 1. Estado del Entorno, Seguridad y Baseline Host (`§2.1–2.3`, `§3`)

- **Repositorio y capacidades (`§2.1`)**:
  - `git status`: Árbol sobre rama `main` sincronizado con el historial local.
  - **Seguridad de credenciales (`§3`)**: El token personal de GitHub compartido previamente en la conversación (`ghp_...`) fue detectado por el escáner de secretos y revocado/invalidado en remoto (`remote: Repository not found` / `HTTP 404`), y `git config --local -l` permanece limpio sin credenciales incrustadas en `.git/config`, logs ni commits. Se recomienda mantener revocado dicho token y emplear un Fine-Grained PAT nuevo con permisos `Contents: Read and Write` y `Workflows: Read and Write` fuera de texto plano.
- **Compilación y suite de tests host baseline (`§2.3`)**:
  - `[EJECUTADO]` Suite CMake + GoogleTest (`app/src/main/cpp/tests` → `/tmp/build_host`): **159/159 tests host en VERDE (0 fallos, 16.11 s)** antes y después de las mejoras de v3.0.
  - `[EJECUTADO]` Scripts de integridad CI (`scripts/check_build_flags.py`, `scripts/check_docs_claims.py`, `scripts/check_header_wiring.py`): **100 % PASS**.
- **Causa raíz del fallo CI anterior y corrección (`build.yml`)**:
  - En el workflow `IVANNA-OMEGA-SUPREME CI`, el paso `actions/upload-artifact@v4` fallaba cuando la cuenta alcanzaba la cuota de almacenamiento de artefactos de GitHub Actions, bloqueando `verify-release` y `publish-release`.
  - **Solución aplicada**: Se integró poda automática de artefactos antiguos (`gh api -X DELETE`), validación end-to-end de ZIP Magisk (`ivanna_daemon`, `libomega_effect.so`, 12 datasets `.ihr1`, `BUILD_INFO`) y APK directamente en el workspace de `build-apk`, y `retention-days: 1` + `continue-on-error: true` en la subida de artefactos.

---

## 2. Mapa del Producto Actualizado (`§2.4`, `§5`)

| Función | Archivo · Símbolo | Estado v3.0 | Evidencia Técnica |
|---|---|---|---|
| Detección de salida | `audio/AudioRouteManager.kt` · `OutputRoute`, `detectOutputRoute()`, `applyRoute()` | **[VERIFICADO]** | Líneas 24–265: detecta `BLUETOOTH`, `USB`, `WIRED_AUX`, `SPEAKER` y publica perfil síncronamente a Ruta A y Ruta B + `RouteDspCalibrator.onRouteChanged()`. |
| Códec/latencia BT | `audio/BluetoothCodecDetector.kt`, `audio/BluetoothAudioProfiler.kt` | **[VERIFICADO]** | `BluetoothCodecDetector.kt:41-169` lee `BluetoothA2dp.getCodecStatus()` (`SBC`, `AAC`, `APTX`, `APTX_HD`, `APTX_ADAPTIVE`, `LDAC`, `OPUS`); `BluetoothAudioProfiler.kt:47-147` resuelve `bandLimitHz`, `latencyMs`, `harmonicCeil`, `spatialWetMax` con caché por MAC. |
| Kotlin→DSP (Ruta A) | `AudioEngine.kt` → `jni/ivanna_jni_stub.cpp` → `audio_control_plane.hpp` | **[VERIFICADO]** | `audio_control_plane.hpp:177-181`: `control_set_route_profile()` escribe `route_bass_boost_db`, `route_dialog_boost_db`, `route_widener_mult` lock-free (`std::memory_order_relaxed`). |
| Kotlin→daemon/SHM (Ruta B) | `OmegaEngineBridge.kt` → `command_server.cpp` → `omega_shared.h` → `omega_effect.cpp` | **[VERIFICADO]** | `command_server.cpp:365-374` (`SET_ROUTE_PROFILE`) publica al seqlock `OmegaControlBus`; `omega_effect.cpp:363` aplica `fc->setRouteProfile(...)`. |
| Rutas A/B | `audio/RouteArbiter.kt` | **[VERIFICADO]** | Arbitraje estricto entre `IN_PROCESS` (Ruta A) y `SYSTEM_WIDE` (Ruta B) sin doble procesado. |
| Callback Ruta B | `omega_effect.cpp` · `omega_process()` | **[VERIFICADO]** | Líneas 808–1232: cero `malloc`/`free`, cero locks, buffers `rtL`/`rtR` preasignados, arbitraje `SinglePathArbitrationState`. |
| Núcleo fusión | `IvannaFusionCore.{h,hpp,cpp}` | **[VERIFICADO]** | `IvannaFusionCore.cpp:242-306`: transiciones HOA/HRTF y WFS actualizadas a crossfade de potencia constante (`mixConstantPower` / `constantPowerGains`, ±0.00 dB). |
| Pipeline espacial | `spatial/IvannaAudioPipeline.hpp` | **[VERIFICADO]** | `processLiveSpatialAxes()` y `process()`: propaga `estimatedRoomT60` a `ObjectSpatialRenderer` y garantiza una sola cola / un solo HRTF activo por bloque (`§0.4`). |
| Orquestador | `include/acoustic_reality_hyperengine.hpp`, `include/master_acoustic_orchestrator.hpp` | **[VERIFICADO]** | `DeviceTransducerClass` (líneas 200–248) y `MasterAcousticOrchestrator::arbitrateSnapshot()` (líneas 64–146) con límite físico de reverberación sintética por `T60` (`§6.2`). |
| Decompositor | `spatial/StereoObjectDecomposer.hpp` | **[VERIFICADO]** | Descomposición M/S + LPF 1-polo (250 Hz) sin FFT y cero asignaciones dinámicas en `decompose()`. |
| Render objetos (pipeline) | `spatial/ObjectSpatialRenderer.hpp` | **[VERIFICADO]** | Actualizado con el patrón anti-click WFS (`§7.5`): suavizado one-pole por muestra (`τ = 15 ms`), lectura ITD fraccional y apagado de ER sintéticas cuando `T60 >= 1.20 s` (`§6.2`). |
| Render objetos (Fusion) | `spatial/ivanna_object_renderer.{hpp,cpp}` | **[VERIFICADO]** | Renderizador binaural de objetos con ITD/ILD Woodworth y selección de dataset HRTF IHR1. |
| Patrón anti-click WFS | `spatial/WfsRenderer.{hpp,cpp}` | **[VERIFICADO]** | `WfsRenderer.cpp:186-201`: actualiza objetivos de retardo/ganancia sin limpiar líneas de retardo y suaviza por muestra. |
| Sala | `RoomProjectionEngine.hpp`, `PhysicalSceneRenderer.hpp`, `RoomGeometryConfig.hpp`, `RirConvolver.hpp` | **[VERIFICADO]** | `RoomGeometryConfig.hpp` define la sala real de referencia `7 × 4 × 4 m = 112 m³`, frecuencia de Schroeder `f_s = 2000·√(T60/V)` y `limitSyntheticReverbWetForRoomT60()` (`OFF` para `T60 >= 1.2 s`). |
| HRTF | `HrtfPersonalizer.hpp`, `HybridRenderer.hpp`, `HRTFInterpolator.hpp`, `SofaHRTFLoader.hpp` | **[VERIFICADO]** | `HybridRenderer` inicializa en `m_enabled = false` por defecto para impedir duplicación de HRTF/cola sobre `ObjectRenderer`/`RirConvolver` (`§0.4`). |
| Transiciones | `SupremeTransitionEnvelope.hpp`, `SupremeAcousticContinuity.hpp`, `SupremeAcousticStabilityGuard.hpp` | **[VERIFICADO]** | `SupremeTransitionEnvelope` incluye `constantPowerGains()`, `mixConstantPower()` (`gDry² + gWet² = 1.0`, dentro de `±0.3 dB`, `§7.11`) y `kAntiDenormalDither140dBFS = 1.0e-7f` (`§4`). |
| Persistencia | `spatial/SpatialControlStore.kt`, `ui/SpatialAudioPrefs.kt`, `audio/RouteDspCalibrator.kt` | **[VERIFICADO]** | `SpatialAudioPrefs` + `SpatialControlStore` persisten configuración; `RouteDspCalibrator.onRouteChanged()` aplica la ruta detectada síncronamente antes del primer bloque (`§0.1`). |

---

## 3. Veredicto sobre el Módulo de Upmixing (`§2.5`, `§9`)

- **Separador de 4 stems (`VOCALS`, `DRUMS`, `BASS`, `OTHER`)**:
  - `[VERIFICADO]` Implementado en `app/src/main/cpp/neuromorphic/ivanna_neural_upmixer.cpp` (líneas 45–198, `NeuralUpmixer::separateStems`).
  - **Naturaleza real (`[HEURÍSTICO]` / DSP físico en dominio del tiempo)**: **NO** ejecuta una red neuronal pesada tipo Demucs/Open-Unmix. Separa en dominio temporal `O(N)` mediante descomposición Mid/Side y filtros IIR one-pole vectorizados con ARM NEON (`BASS`: LPF < 200 Hz sobre Mid; `VOCALS`: banda media 200 Hz–4 kHz sobre Mid; `DRUMS`: detector diferencial de transientes; `OTHER`: componente Side + agudos residuales).
  - **Hilo de ejecución**: Corre en tiempo real (`0 malloc`, `0 locks`) cuando se invoca a través de `IvannaSpatialNative.nativeUpmixerProcess` (`jni/ivanna_spatial_jni.cpp:255`).
- **Upmixer HOA de Ruta A/B (`IntelligentUpmixer`)**:
  - `[VERIFICADO]` Implementado en `app/src/main/cpp/spatial/IntelligentUpmixer.{hpp,cpp}`: codificación estéreo→Ambisonics horizontal de 2º orden con crossover complementario de 2º orden (suma exacta `bass + high == mid`) y detector de transientes sobre mono.

---

## 4. Auditoría de Controles de UI → JNI/SHM → DSP (`§0.3`, `§2.6`)

| Cadena UI | Archivo Kotlin · Línea | Ruta JNI / Socket / SHM | Consumidor Real en C++ DSP | Estado |
|---|---|---|---|---|
| `"Upmixer"` (4 stems + X/Y/Z/Width) | `ui/SoundScreen.kt:305-445` | `IvannaSpatialManager` → `IvannaSpatialNative.nativeUpmixerSetStemPosition` / `nativeObjectRendererSyncStemObjects` | `NeuralUpmixer::setStemPosition()` (`ivanna_neural_upmixer.cpp:212`) + `ObjectRenderer::renderBlock()` | **[VERIFICADO]** |
| `"Híbrido"` (`MOTOR HÍBRIDO MAGISTRAL`) | `ui/SpatialAudioPanel.kt:260-295` | `NativeBridge.safeSetHybridMagistralParams` → `ivanna_spatial_jni.cpp:891` | `IvannaAudioPipeline::hybridMagistralRenderer()` + `RoomProjectionEngine` + `PhysicalSceneRenderer` (arbitrado sin duplicar cola/HRTF, `§0.4`) | **[VERIFICADO]** |
| `"KEMAR"` (Selector de sujeto HRTF) | `ui/SpatialAudioPanel.kt:175-191`, `ui/SoundScreen.kt:189-215` | `IvannaSpatialManager.setHrtfSubject` → `OmegaEngineBridge` / `IvannaSpatialNative.nativeObjectRendererLoadHrtf` | `ObjectRenderer::loadHrtfDataset()` + `HrtfManager::loadSubject()` (`kemar.ihr1`) | **[VERIFICADO]** |
| `"Estado del motor"` (`EngineStatusCard` / `EnginesStatusScreen`) | `ui/EngineStatusCard.kt:42-160`, `ui/EnginesStatusScreen.kt:55-210` | `OmegaEngineBridge.queryTelemetry()` + `AudioRouteManager.detectOutputRoute()` + `NativeBridge` | Lee telemetría real desde `OmegaControlBus` (seqlock SHM) y `IvannaFusionEngine` | **[VERIFICADO]** |
