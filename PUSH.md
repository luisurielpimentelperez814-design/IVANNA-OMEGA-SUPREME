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

---

## 2. Cómo Pushear a GitHub (`luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME`)

### Opción A — Desde el botón de GitHub de Google AI Studio
Usa el icono de **GitHub (Sync / Push to GitHub)** en la barra superior de AI Studio para enviar el commit directamente a la rama `main`.

### Opción B — En un solo comando desde Termux
El paquete con los archivos C++ corregidos ya está compilado y servido en `/IVANNA-OMEGA-DSP-CLEAN.tar.gz`:

```bash
cd ~/IVANNA-OMEGA-SUPREME && \
  curl -fsSL https://ais-dev-cc7vwjur4mso7cpa6iy7hf-438584069333.us-east5.run.app/IVANNA-OMEGA-DSP-CLEAN.tar.gz | tar -xzvf - && \
  git add app/src/main/cpp/ PUSH.md && \
  git commit -m "fix(dsp): eliminate nonlinear waveform distortion, DC offset, ITD clicks, and JNI parameter races" && \
  git push origin main
```

