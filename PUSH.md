# IVANNA-OMEGA-SUPREME — Push de Correcciones Acústicas DSP (C++20/23) & UI Fixes A·B·C·D

## 1. Correcciones Acústicas DSP Aplicadas (Cero Distorsión, Cero Clics, Cero Offset DC)

| # | Archivo C++ | Causa Raíz Corregida |
|---|-------------|----------------------|
| 1 | `app/src/main/cpp/neuromorphic/nho_engine.hpp` | **ODE NHO inestable:** Se usaba `zL += mu * (...)` en lugar de `zL += dzL` (que incluye el amortiguamiento `-0.12f * zL`), causando saturación tipo *Schmitt-trigger* en `±1.0` (onda cuadrada) cuando `beta > 0`. Además se añadió suavizado *one-pole* por muestra para `alpha`, `beta`, `mu`, `harmonic_gain` y `wet`. |
| 2 | `app/src/main/cpp/spatial/cue_based_spatial.hpp` | **Saltos de puntero ITD y discontinuidad en 0°:** Se añadió interpolación y suavizado *one-pole* por muestra (`thetaSmooth_`, `widthSmooth_`, `wetSmooth_`) eliminando clics de *delay-line* al mover el ángulo espacial o cuando actúa el Kernel Evolutivo. |
| 3 | `app/src/main/cpp/pd_engine.hpp` | **Inicialización HRTF + integrador con fuga:** `PDEngine::init()` ahora inicializa `hrtf.init(sr)`. Se corrigió el orden de `z_prev` en `update_state()`, se añadió fuga `0.995f` y suavizado por muestra de `modL`/`modR` en `decode()`. |
| 4 | `app/src/main/cpp/IvannaFusionCore.cpp` & `.h` | **Offset DC de `-0.12` en `applyGoldenEarGAN`:** El polinomio de Chebyshev `2x² - 1` vale `-1.0` en silencio, inyectando un offset DC constante en `fast_tanh_scalar()` y causando distorsión par asimétrica y pops. Se reemplazó por `2x²` con bloqueo DC de 1er orden (`m_h2DcMeanL/R`) y se sincronizó `m_sampleRateF`. |
| 5 | `app/src/main/cpp/EvolutionaryEQ.cpp` | **Doble saturación no-lineal (`tanh` sobre `tanh`):** Se eliminó el `fast_tanh_scalar` redundante dentro de `EvolutionaryEQ::processNEON` (ya existe al final de `IvannaFusionEngine::process`) y se normalizó el FIR por ganancia espectral máxima (`maxMag <= 1.4125`). |
| 6 | `app/src/main/cpp/Psychoacoustics.cpp` | **Waveshaping en cruces por cero:** `applyMaskingCompensation` calculaba `absL / m_envLeft` muestra a muestra con `τ = 2 ms`, amplificando los cruces por cero de cada ciclo senoidal. Ahora expande únicamente en función de la envolvente lenta (`m_envLeft`) con suavizado de `15 ms`. |
| 7 | `app/src/main/cpp/include/acoustic_reality_hyperengine.hpp` | **Escalones de ganancia por bloque:** `MicroDetailExtractor::applyMicroIntelligibilityPass` ahora aplica `normGain` con rampa por muestra (`normGainSmooth_`) y suaviza la envolvente rápida (`fL`/`fR`) para evitar intermodulación en agudos. |
| 8 | `app/src/main/cpp/supreme/WarpedLatticeTransducerInverter.hpp` | **Tono ultrasónico y sobre-extrapolación de crestas:** Se desactivó por defecto `microChirpEnabled_` (17.5–19 kHz), se restringió `reconstructClippedCrest` a recortes planos reales (`>= 0.985`) con techo `±0.998`, y se acotó `blRatio` a `[0.92, 1.08]`. |
| 9 | `app/src/main/cpp/adaptive_engine_v2.hpp` | **Data race en FFT y pico de CPU en ACF:** Se reemplazaron los arreglos `static float re[], im[]` por buffers locales alineados en pila y se acotó la ventana ACF a 512 muestras con paso 8. |
| 10 | `app/src/main/cpp/jni/ivanna_omega_jni.cpp` | **Data race en `DSPBridge_nativeSetParams`, centrado espacial y rampa LUFS:** Los cambios de EQ/Comp/Exciter/Gain se publican al hilo de audio vía `g_params_dirty` sin mutar biquads en mitad de un bloque; `sp_angle` evolutivo se centró en `[-30°, +30°]` y `trimLin` de sonoridad se suaviza por muestra (`loudnessTrimSmooth`). |
| 11 | `app/src/main/cpp/supreme/SupremeTransitionEnvelope.hpp` | **Capa Arquitectónica Global "Supreme Zero-Pop State Transition Layer":** Estructura `alignas(64)` libre de bloqueos y trivialmente copiable (`attack_ms = 8 ms`, `release_ms = 18 ms`, `thermal_ms = 35 ms`) integrada en los 5 motores Supremos (`WarpedLattice`, `PhaseCoherentTransharmonic`, `SnnNmfHoaUpmixer`, `PinnaManifoldInterpolator` con crossfade dual-slot FIR, `SupremeMsoFarrowArbitrator` con suavizado de retardo fraccional), `CochlearActiveInverseEngine`, `VolterraH2Symmetric`, `MicroRealityExtractor`, `RoomProjectionEngine`, `HrtfManager`, `IvannaFusionCore`, `IvannaAudioPipeline` y `omega_effect.cpp`. Elimina todos los bypass duros (`if (!enabled_) return;`) y saltos de `ThermalGovernor`. |
| 12 | `app/src/main/cpp/neuromorphic/volterra_h2_symmetric.cpp` | **Off-by-one en buffer circular, filtro peine de 1 muestra y hard clip:** `d_idx` se incrementaba antes del lazo de convolución haciendo que `k=0` leyera la muestra más antigua (`t - 63`) en lugar de la actual `x[n]`. Se corrigió la indexación respecto a `write_idx`, se eliminó el eco fijo `0.08f * x[n-1]` y se reemplazó `std::clamp` por saturador racional C2. |
| 13 | `app/src/main/cpp/IvannaFusionCore.h` & `.cpp` | **Ceros pre-cargados en FIFO y doble espacialización:** `resetFifo()` pre-cargaba 128 ceros en `m_outFifoCount`, causando caídas periódicas a cero (efecto hélice) cuando `AudioFlinger` entregaba bloques no múltiplos de 128. Además se arbitró `HoaBinauralDecoder` vs `WfsRenderer` vs `stereoWidth` para impedir triple espacialización en serie. |
| 14 | `app/src/main/cpp/supreme/SupremeAcousticStabilityGuard.hpp` | **Arquitectura Permanente `SupremeAcousticStabilityGuard` (Ruta A + Ruta B):** Instrumentación RT-safe por etapa (`StageBlockMetrics`, `FirstFaultReport`), arbitraje de ruta única (`SinglePathArbitrationState`), techo de energía por etapa (`enforceStageEnergyCeiling`), bloqueador DC de 2º polo (3.5 Hz), gobernador continuo de headroom C2 (`0.92f`) y continuidad Hermite C1 en fronteras de bloque. |
| 15 | `app/src/main/cpp/dsp/SafetyLimiter.cpp` & `IvannaAgentCore.kt` | **Eliminación de bombeo periódico por falsos positivos de clip:** `SafetyLimiter` ahora solo contabiliza sobrecargas reales (`> 1.05f` post-envolvente o no finitos) y `DspControlAgent` incorpora `recoveryHoldCycles` y rampa gradual de recuperación de ganancia. |
| 16 | `PlaybackCaptureService.kt`, `IvannaBridgePlayer.kt`, `IvannaSpatialEngine.kt`, `OmegaVibratoryProcessor.kt` | **Arbitraje de Ruta Única en Kotlin, retardo ITD fraccional y detector estéreo vinculado:** Se evita re-procesar en Kotlin bloques ya finalizados por `DSPBridge` C++ o el daemon Magisk; se corrigió el salto de `itdMax` muestras en `IvannaSpatialEngine` con interpolación lineal fraccional y se vinculó la envolvente estéreo L/R en `OmegaVibratoryProcessor`. |
| 17 | `HarmonicExciter.cpp`, `ParametricEQ.cpp`, `ivanna_omega_jni.cpp`, `Iso226Calibrator.kt`, `IvannaGlobalEffectManager.kt` | **Corrección de tronido al subir sliders del EQ Paramétrico de 8 bandas / ISO 226 (> 0 dB):** Se corrigió la inversión negativa de headroom (`headL < 0 → needL = -1e8 → scaleL negativo`) y el recorte duro pre-`GainStage` en `HarmonicExciter::process()`; se añadió consolidación continua de filtro en `ParametricEQ::setBand()` durante arrastre de sliders y compensación progresiva desde `> 0 dB`; se unificó `normalizeMasterToDb()` y auto-headroom en `Iso226Calibrator` e `IvannaGlobalEffectManager`. |

---

## 2. Mapa Arquitectónico: Supreme Zero-Pop & Acoustic Stability Layer

```text
PRODUCTOR DE ESTADO (UI Kotlin / JNI / PersistedStateRestorer / ThermalGovernor / AdaptiveDecisionEngine)
  │
  ▼
CONTROL BUS / JNI / SHM (OmegaControlBus Seqlock / AcousticRealityStateBus / Atomic Flags)
  │
  ▼
CAPA DE TRANSICIÓN: SupremeTransitionEnvelope + SinglePathArbitrationState (alignas(64), lock-free, 0 malloc)
  ├─ TransitionProfile::Standard      (OFF→ON: 8 ms, ON→OFF: 18 ms)
  ├─ TransitionProfile::Thermal       (NORMAL ↔ LIMITED ↔ SAFE/BYPASS: 35 ms)
  └─ TransitionProfile::FastParameter (Modulación continua de parámetros/slots FIR: 5–6 ms)
  │
  ▼
MÓDULOS DSP + SupremeAcousticStabilityGuard::enforceStageEnergyCeiling()
  ├─ InspectStage RT-Safe (peakL/R, rmsTotal, dcOffsetL/R, maxSampleDelta, FirstFaultReport)
  ├─ Arbitraje de Ruta Única (1 Spatial/HRTF, 1 Room/RIR, 1 Nonlinear/Volterra, 1 Widener)
  └─ Gobernador Pre-Limiter C2 (DC Blocker 3.5 Hz + Anti-Runaway + SoftCeiling Padé 0.92 + Hermite C1)
  │
  ▼
SALIDA PCM (AudioFlinger Ruta B / JNI Ruta A — Cero clipping, cero efecto hélice, cero tronidos)
```

---

## 3. Cómo Pushear a GitHub (`luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME`)

### Opción A — Desde el botón de GitHub de Google AI Studio
Usa el icono de **GitHub (Sync / Push to GitHub)** en la barra superior de AI Studio para enviar el commit directamente a la rama `main`.

### Opción B — En un solo comando desde Termux
El paquete con todos los archivos C++ y Kotlin corregidos ya está empaquetado y servido en `/IVANNA-OMEGA-DSP-CLEAN.tar.gz`:

```bash
cd ~/IVANNA-OMEGA-SUPREME && \
  curl -fsSL https://ais-dev-cc7vwjur4mso7cpa6iy7hf-438584069333.us-east5.run.app/IVANNA-OMEGA-DSP-CLEAN.tar.gz | tar -xzvf - && \
  git add app/src/main/cpp/ app/src/main/java/ PUSH.md && \
  git commit -m "fix(dsp): implement SupremeAcousticStabilityGuard and eliminate clipping, rotor modulation, and pops" && \
  git push origin main
```

