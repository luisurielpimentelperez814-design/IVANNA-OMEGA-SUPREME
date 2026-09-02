<div align="center">

```
╔══════════════════════════════════════════════════════════════════════════════════╗
║                                                                                  ║
║    ██╗██╗   ██╗ █████╗ ███╗   ██╗███╗   ██╗ █████╗                              ║
║    ██║██║   ██║██╔══██╗████╗  ██║████╗  ██║██╔══██╗                             ║
║    ██║██║   ██║███████║██╔██╗ ██║██╔██╗ ██║███████║                             ║
║    ██║╚██╗ ██╔╝██╔══██║██║╚██╗██║██║╚██╗██║██╔══██║                             ║
║    ██║ ╚████╔╝ ██║  ██║██║ ╚████║██║ ╚████║██║  ██║                             ║
║    ╚═╝  ╚═══╝  ╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═══╝╚═╝  ╚═╝                             ║
║                                                                                  ║
║               ██████╗ ███╗   ███╗███████╗ ██████╗  █████╗                       ║
║              ██╔═══██╗████╗ ████║██╔════╝██╔════╝ ██╔══██╗                      ║
║              ██║   ██║██╔████╔██║█████╗  ██║  ███╗███████║                      ║
║              ██║   ██║██║╚██╔╝██║██╔══╝  ██║   ██║██╔══██║                      ║
║              ╚██████╔╝██║ ╚═╝ ██║███████╗╚██████╔╝██║  ██║                      ║
║               ╚═════╝ ╚═╝     ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝                      ║
║                                                                                  ║
║    ███████╗██╗   ██╗██████╗ ██████╗ ███████╗███╗   ███╗███████╗                 ║
║    ██╔════╝██║   ██║██╔══██╗██╔══██╗██╔════╝████╗ ████║██╔════╝                 ║
║    ███████╗██║   ██║██████╔╝██████╔╝█████╗  ██╔████╔██║█████╗                   ║
║    ╚════██║██║   ██║██╔═══╝ ██╔══██╗██╔══╝  ██║╚██╔╝██║██╔══╝                   ║
║    ███████║╚██████╔╝██║     ██║  ██║███████╗██║ ╚═╝ ██║███████╗                 ║
║    ╚══════╝ ╚═════╝ ╚═╝     ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝╚══════╝                 ║
║                                                                                  ║
╚══════════════════════════════════════════════════════════════════════════════════╝
```

### *Motor de Inteligencia Acústica de Grado OEM++ para Android*

[![Build](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=github&label=BUILD&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Tests](https://img.shields.io/badge/CTest-31%2F31%20%E2%97%8F%20100%25-23F09A?style=for-the-badge)](app/src/main/cpp/tests/)
[![Android](https://img.shields.io/badge/Android-10%20%E2%86%92%2015-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com)
[![NDK](https://img.shields.io/badge/NDK-r26%20%C2%B7%20arm64--v8a-6FF3FF?style=for-the-badge)](app/src/main/cpp/)
[![Module](https://img.shields.io/badge/Magisk%20v2.3.0-Magisk%20%7C%20KernelSU-FF3E86?style=for-the-badge)](magisk_module/)
[![License](https://img.shields.io/badge/%C2%A9%202026-GORE%20TNS-57708F?style=for-the-badge)](LICENSE)

```
245 archivos C++  ·  188 archivos Kotlin  ·  75 suites de test
218 datasets SOFA  ·  12 HRTFs IHR1  ·  200 RIRs medidas  ·  12 perfiles de artista
```

**No es un ecualizador. No es un plugin. Es la capa de audio de un SoC de primer nivel, trasplantada a cualquier Android.**

</div>

---

## ◈ Filosofía

Los motores de audio de consumo (ViPER, JamesDSP) aplican bloques DSP estáticos sin retroalimentación. Los servicios de IA externas procesan contenido, no oyentes. Los DACs de hardware mejorar un solo punto de la cadena.

IVANNA ataca los tres frentes simultáneamente: un grafo DSP dinámico nativo en C++17/NEON que reacciona a cada bloque de audio, un motor de inteligencia acústica personal que aprende al oyente en el dispositivo, y una capa de sistema global (Magisk) que aplica todo esto a *cada sample* que el SoC produce — Spotify, YouTube, llamadas, juegos, todo.

El principio rector es uno: **cero decisiones sin datos reales**. Las curvas de loudness son ISO 226:2003 medidas. Los HRTFs son datasets de laboratorio (KEMAR, CIPIC, TU-Berlin). Los espacios son 200 RIRs de salas reales. La clasificación es CRNN entrenada in-house. Nada se sintetiza cuando existe una medición.

---

## ◈ Arquitectura: dos rutas, un cerebro

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│                               TU DISPOSITIVO                                       │
│                                                                                    │
│   Spotify · YouTube · Juegos · Llamadas · Sistema             IVANNA App           │
│         │                                                    (Ruta A · 48 kHz)     │
│         │                                                          │               │
│         ▼ AudioFlinger                                             ▼               │
│  ┌─────────────────────┐  OmegaControlBus   ┌───────────────────────────────┐      │
│  │  libomega_effect.so │◄──── seqlock SHM ─►│  ivanna_daemon                │      │
│  │  (Ruta B · sistema  │  512 B · CRC32 ·   │  SCHED_FIFO 98 · PIE · RELRO │      │
│  │   global · Magisk)  │  bidireccional     │  Unix socket · TCP fallback   │      │
│  └─────────┬───────────┘                    └──────────────┬────────────────┘      │
│            │ IvannaFusionCore × sesión                     │                       │
│            │ FusionEngine::process()                       │ /data/adb/            │
│            │ → RIR (overlap-save FFT) → SAF → Limiter      │ ivanna_omega/         │
│            ▼                                               ▼                       │
│     Audífonos / Altavoz ◄──── libivanna_omega.so (JNI)                             │
│                               ParametricEQ → Compressor → HarmonicExciter          │
│                               → StereoWidener → PDEngine → GainStage               │
│                               → SafetyLimiter (−0.1 dBFS)                          │
└────────────────────────────────────────────────────────────────────────────────────┘
```

**Ruta A — En proceso** · La app procesa su propio reproductor (`IvannaBridgePlayer`) y captura MediaProjection de otras apps. Latencia medida en LAB: ~2.8 ms. Pipeline completo: ParametricEQ 10 bandas → Compresor → HarmonicExciter (2× oversampleado) → StereoWidener M/S → PDEngine (NHO + BiquadEnvelopeBank + CueBasedSpatial) → GainStage → SafetyLimiter.

**Ruta B — System-wide** · `libomega_effect.so` se carga dentro de `audioserver` (proceso del sistema) como `GlobalEffect` via Magisk. Una instancia `IvannaFusionCore` independiente por sesión AudioFlinger. Controlada cross-process vía `OmegaControlBus` — seqlock sobre SHM compartida, 512 bytes, completamente lock-free en el callback de audio.

---

## ◈ La Cadena DSP Nativa — Ocho Etapas, Cada una Auditada

> **Cero allocaciones en el hot path.** Buffers preasignados en `EFFECT_CMD_SET_CONFIG` (8 192 frames). El callback de audio nunca llama a `malloc`, nunca toma locks pesados, nunca parsea strings.

| # | Etapa | Fuente C++ | Qué hace con precisión | Mecanismos de seguridad |
|---|-------|------------|------------------------|------------------------|
| 1 | **Pre-EQ Peak Guard** | `jni/ivanna_omega_jni.cpp` | Headroom preventivo antes del EQ | −1 dBFS clamp antes de la cadena |
| 2 | **ParametricEQ 10 bandas** | `dsp/ParametricEQ.cpp` | Filtros biquad RBJ en 31/63/125/250/500/1k/2k/4k/8k/16kHz | Crossfade anti-zipper 15 ms · compensación de headroom acumulado por stack aplicada en los 4 puntos de `setParams` |
| 3 | **Compressor** | `dsp/Compressor.cpp` | RMS + sidechain HPF · envolvente suavizada | Sin escalones · ratio continuo |
| 4 | **HarmonicExciter** | `dsp/HarmonicExciter.cpp` | Saturación Padé [3/2] de tanh · generación de 2ª y 3ª armónica | Oversampling 2× + LPF 14.5 kHz anti-aliasing · clamp Padé ±3 · techo `excScale_` con ataque inmediato / release 20 ms · bypass **bit-exacto** a wet=0 |
| 5 | **StereoWidener** | `dsp/StereoWidener.cpp` | Procesamiento M/S (mid/side) | Clamp de correlación para evitar inversión de fase |
| 6 | **PDEngine** | `pd_engine.hpp` | NHO (Non-linear Harmonic Oscillator) + BiquadEnvelopeBank + CueBasedSpatial | Motor no-lineal con inhibición lateral; cero artefactos en señal limpia |
| 7 | **GainStage** | `dsp/GainStage.cpp` | Trim de salida con suavizado por muestra | Filtro one-pole sample-accurate; sin doble limitación con la etapa siguiente |
| 8 | **SafetyLimiter** | `dsp/SafetyLimiter.cpp` | Techo brickwall −0.1 dBFS como última instancia | Soft-knee real (threshold −4 dBFS) · ataque y release **recalculados** por SR de sesión (8 kHz–384 kHz) · presente en Ruta A y Ruta B con paridad exacta |

---

## ◈ Espacialización — Datos Medidos, No Sintetizados

El motor espacial de IVANNA es uno de los conjuntos de datos de campo libre más completos shippeados en una aplicación Android.

### Datasets embarcados

| Dataset | Cantidad real | Formato | Procedencia |
|---------|--------------|---------|-------------|
| **HRTF IHR1** | 12 datasets | `.ihr1` binario propio con guard de integridad | KEMAR pinna normal/grande · TU-Berlin · CIPIC (003/008/009/010/011/012/165) · Pulse · freefield demo |
| **SOFA AES69** | 218 archivos | `.sofa` estándar internacional | MIT KEMAR · GeneralTF · GeneralSOS · UMA AnnotatedReceiver · 200 sujetos individuales ARI (headphones AKGK271, DT770PRO, HD650, HD280…) |
| **RIR medidas** | **200 salas reales** | WAV PCM + `metadata.csv` con RT60 por sala | Selección de entornos reales con parámetros de reverberación catalogados |
| **SAF Model** | Modelo JSON total | `SAF_model_total.json` + `SAF_model_espacial.json` | Personalización espectral y espacial |
| **PCA Basis** | Matriz de vectores | `pca_basis_V.bin` | Base de compresión para el optimizador latente |

### Pipeline espacial

```
Audio estéreo de entrada
        │
        ▼
ObjectRenderer  →  12 altavoces virtuales en disposición dodecaédrica
        │          (distribución isotrópica del espacio esférico)
        ▼
HRTFConvolver × speaker  →  interpolación IDW bilineal entre HRTFs medidos
        │                   Morph conducido por vector latente q[7] del
        │                   optimizador Φ_SAF^∞
        ▼
Φ_SAF^∞ Cross-Process (ABI v2)
        │  publica saf_q[7] en OmegaDspSnapshot
        │  → la Ruta B recibe la personalización por bloque
        │  → la HRTF del sistema es la misma que la de la app
        ▼
RirConvolver  →  convolución overlap-save FFT Radix-2 real-time safe
        │        selección de sala por RT60 medido desde el daemon
        │        (comandos SET_ROOM_RT60 / GET_ROOM_STATUS)
        ▼
Audio binaural procesado  →  audífonos / altavoces
```

**HRTF wet/dry cross-process:** cuando Android cambia la ruta al DAC USB-C, Kotlin llama `nativeSetHrtfWetDryStatic(0f)` + `nativeFlushHrtfHistoryStatic()`. Transcurridos ~150 ms (historia limpia), restaura `wet=1f`. El flag `g_hrtf_flush_req` es un one-shot lock-free consumido en el callback: sin ruido de transición, sin artefactos de convolución con historia sucia.

---

## ◈ Motor Neuromorfo — La Capa Más Profunda

Más allá de la cadena DSP clásica, IVANNA implementa un modelo computacional del sistema auditivo mamífero.

### NeuroCochlear Manifold (`neuromorphic/neuro_cochlear_manifold`)

Un banco de **32 canales de banda gammatone** que modelan las células ciliadas internas del oído (IHC), cada canal con:

- **Filtro gammatone biquad** (constantes b₀, b₁, b₂, a₁, a₂ con historia de doble polo z₁/z₂)
- **Kernel Volterra de 3er orden** (h1, h2, h3 × `VOLTERRA_TAPS = 16`) para modelar no-linealidades cocleares
- **Estado IHC** (inner hair cell) con half-wave rectification y filtro LP de adaptación
- **Integración numérica Runge-Kutta 4** (4 sub-pasos por sample) a 96 kHz → `DT_RK4 = 1/(96000×4)`
- **Inhibición lateral** entre canales vecinos (mecanismo de agudeza frecuencial del oído interno)

### NPE Engine — Neural Processing Engine (`neuromorphic/ivanna_npe_engine`)

Upsamplers FIR de alta precisión para expandir el ancho de banda antes del motor espacial:

- **1 024 coeficientes** con ventana Blackman-Harris × sinc
- **Factor 16×:** 48 kHz → **768 kHz** de resolución interna
- **~0.78 GMACs** por bloque de audio a 768 kHz
- Buffers alineados a **64 bytes** para NEON/HVX; cero copias en el hot path
- Selección automática **Hexagon DSP vs. CPU NEON** basada en `ivanna::hexagon::is_available()`

### Volterra H2 Simétrico (`neuromorphic/volterra_h2_symmetric`)

Corrección no-lineal de orden 2 para compensar la resonancia mecánica del transductor físico:

```
y[n] = h1[n]*x[n] + Σk Σl h2[k,l] * x[n-k] * x[n-l]
```

El kernel h2 simétrico corrige compresión de aire, distorsión de suspensión y resonancias mecánicas del driver en tiempo real, sin necesidad de modelo físico del altavoz.

### PI-LSTM Bridge (`pi_lstm_bridge_jni.cpp` · `neuromorphic/pi_lstm_milenio.hpp`)

Red LSTM con integrador proporcional-integral para modelar la dinámica temporal de adaptación auditiva. Expuesto a Kotlin vía JNI (`PiLstmBridge.kt`).

---

## ◈ Inteligencia en el Dispositivo — Sin Nube

```
  audio raw ──► AntiDolbyCrnnClassifier ──► AdaptiveDecisionEngine ──► AdaptiveState
  (Ruta A+B)    CRNN INT8, 4 clases          controlLoop @ 50 ms       seqlock
                log-mel 32×40 @ 16 kHz        RawMetricsBus SPSC         consume if newer
                                              │
          ┌──────────────────────────────────┼─────────────────────────────────┐
          ▼                                  ▼                                 ▼
  PerceptualBrainEngine              CMA-ES evolutivo                ISO 226:2003
  RMS · LUFS · SpectralCentroid      512 bandas · 256 taps FIR      Calibrador
  SpectralFlux · MelEnergy[64]       smooth phase · crossfade        29 frecuencias
  BarkEnergy[24] · CrestFactor       fitness psicoacústico           equal-loudness
  TransientDensity                   ISO 226 + resonance penalty     medidas reales
```

### Clasificador de Contenido — CRNN In-House

El clasificador YAMNet fue sustituido por un **CRNN entrenado in-house** (`anti_dolby_crnn.tflite`):

| Característica | Valor |
|----------------|-------|
| Arquitectura | CRNN (Convolutional Recurrent Neural Network) |
| Cuantización | INT8 |
| Clases | 4: `Voz / Música / Bajos / Silencio` |
| Features | Log-mel spectrogram 32×40 @ 16 kHz |
| Ventana mínima | 5 472 muestras (~342 ms @ 16 kHz) |
| Modelo de labels | `anti_dolby_labels.txt` embarcado |
| Fallback | Shim YAMNet (`YamnetClassifier`) para callers legacy — acepta buffers de 15 600 samples sin cambios |

El clasificador alimenta el motor de contenido con `speech`, `music` y `bass` scores en tiempo real. Estos scores ajustan el comportamiento de todas las etapas DSP: la compresión baja en señal de voz, el exciter reduce su agresividad en música clásica, la espacialización aumenta en contenido cinematográfico.

### Motor Adaptativo Evolutivo

- **CMA-ES** (Covariance Matrix Adaptation Evolution Strategy): optimiza una población de 512 bandas sobre un FIR de 256 taps con fase suave. La función de fitness es multi-criterio: `ISO 226 loudness + resonance penalty + tonality consistency`.
- **AdaptiveDecisionEngine** (experimental): un hilo dedicado *no-RT* publica a 50 ms en la `AdaptiveStateBus` (seqlock). Consume métricas del `RawMetricsBus` SPSC escritas por el audio callback en O(1) — un `memcpy` de un POD, sin bloqueo. Detecta: clipping inminente · sibilancia · fatiga espectral.
- **AdaptiveLearning:** ring buffer de hasta 1 000 experiencias auditivas (features + output + user-adjusted). Listo para re-entrenamiento con 50+ experiencias. Model versioning con `modelVersion` StateFlow.
- **Fatigue Mitigator:** ajusta high-cut IIR del daemon en sesiones largas (filtro 1er orden, 16–19.5 kHz).
- **Persistencia total:** TinyML, CMA-ES σ, Fatigue, perfiles y estado spatial sobreviven reinicios vía `PersistedStateRestorer`.

### Calibrador ISO 226:2003

La curva de igual-loudness se aplica en **tres capas simultáneas**:

```
Calibración ISO 226:2003 (29 frecuencias: 20 Hz … 12.5 kHz)
   Af = 4.47×10⁻³ × (10^(0.025×Ln) − 1.15) + (0.4 × 10^((Tf+Lu)/10 − 9))^αf
   Lp = (10/αf) × log10(Af) − Lu + 94
   ΔEQ(f) = Lp(f, refPhon) − Lp(f, listenPhon)

   ┌─────────────────────────────────────────────────────────────────┐
   │ 1. Android Equalizer (AudioEffect)  → todas las apps del sistema │
   │ 2. DSPBridge (libivanna_omega.so)   → reproductor propio         │
   │ 3. OmegaEngineBridge (socket)       → daemon Magisk system-wide  │
   └─────────────────────────────────────────────────────────────────┘
```

Un solo toque aplica la corrección de percepción de loudness a toda la cadena de audio.

---

## ◈ ThermalGovernor — Calidad Continua Bajo Carga Térmica

Los motores de audio clásicos corren a máxima carga sin importar la temperatura del SoC. Cuando el kernel hace throttling de emergencia, se producen XRuns audibles. IVANNA **degrada proactivamente** la carga DSP antes de que el kernel intervenga.

| Tier térmico | Headroom | Acción |
|-------------|---------|--------|
| **Nominal** | < 0.30 | Operación completa sin restricciones |
| **Light** | 0.30 – 0.60 | Reduce `exciter_reduction` (oversampling 2× es la etapa más costosa) |
| **Moderate** | 0.60 – 0.80 | Reduce `spatial_width` + `compressor_amount` |
| **Severe** | > 0.80 | Protección agresiva: espacialidad y compresor al mínimo perceptual |
| **Critical** | API no disponible | Inerte — cero efecto colateral en dispositivos sin HAL térmico |

- Fuente de verdad: `PowerManager.getThermalHeadroom()` (API 29+), polling cada **2 segundos** desde hilo IO.
- El callback de audio nunca consulta temperatura — solo consume el estado ya calculado.
- `target_gain` (volumen del usuario) **nunca se toca** — es sagrado por diseño.

---

## ◈ USB Audio Pro — DAC Directo a 384 kHz

Para dispositivos con DAC externo USB-C, IVANNA puede saltarse completamente el mezclador de Android:

```kotlin
// UsbAudioProManager — acceso isochronous directo al endpoint USB OUT
SAMPLE_RATE  = 384_000 Hz
CHANNELS     = 2 (estéreo)
BIT_DEPTH    = 32-bit float
FRAME_SIZE   = 8 bytes por frame
Modo         = USB Asíncrono — el DAC es master de reloj
```

La transferencia isochronous directa evita el remuestreo de AudioFlinger, la mezcla del sistema y cualquier latencia de buffer adicional. El DAC recibe muestras a la tasa que su propio reloj dicta.

---

## ◈ Motor Cinematográfico en Tiempo Real

`RealTimeCinematicEngine` clasifica contenido cada 50 ms y aplica la cadena de efectos correspondiente con crossfade de transición (50 ms × SR samples):

| Modo | Contenido detectado | Cadena activa |
|------|--------------------|--------------------|
| `SCIFI` | Electrónica futurista | Reverb larga + Delay modulante + Sub-harmonic generator |
| `COSMIC` | Espacial/ambiental | Reverb máxima + Delay difuso + Gain reductivo |
| `HORROR` | Tenso/percusivo | Reverb corta + Formant shifter + Sub-harmonic agresivo |
| `VOID` | Silencio / ruido | Reverb sala muerta + Delay mínimo + Formant neutral |
| `NONE` | — | Bypass completo |

Limiter interno: `limThreshold = 0.95` · ataque `0.995` · release `0.9995` — impide clipping en modos de máxima intensidad.

---

## ◈ Motor Conversacional — IVANNA Personal Acoustic Intelligence

IVANNA no es un chatbot con un plugin de audio encima. Es un sistema de razonamiento acústico que ocurre completamente en el dispositivo, y que además puede conectarse opcionalmente a Gemini 1.5 Flash para capacidades cognitivas ampliadas.

### Pipeline conversacional completo

```
 Usuario (voz o texto)
         │
         ▼
 IvannaSpeechRecognizer  →  ASR modular (intercambiable sin tocar el núcleo)
         │
         ▼
 IvannaLanguageCore  ──────────────────────────────────────────────────────
         │  · 17 intenciones acústicas clasificadas
         │  · Ventana de contexto de 6 turnos
         │  · Desambiguación por historial de sesión
         │  · Comprensión de frases musicales naturales:
         │    "más alma" · "que respire" · "más pegada" · "sentir el escenario"
         │    "como disco de colección" · "me duele la cabeza"
         ▼
 IvannaMusicalIntentEngine  ─────────────────────────────────────────────
         │  · 12 presets canónicos: Épico · Abbey Road · Vinilo · Cinematográfico
         │    Analógico · Estudio Pro · Concierto Masivo · Gentle Mode · y más
         │  · Detección de intenciones encadenadas:
         │    "épico + estadio" → ÉPICO + CONCIERTO MASIVO simultáneos
         │  · Traducción directa a parámetros IvannaEffectProfile (EQ, bass, exciter…)
         ▼
 IvannaCognitiveCore  ───────────────────────────────────────────────────
         │  · Razona la intención contra el estado REAL del DSP
         │  · Si hay clipping activo → rechaza subida de volumen/graves
         │  · Si temperatura es alta → limita espacialidad al 50%
         │  · Si la escena es VOICE → redirige SPATIAL a VOICE_CLARITY
         │  · Toda decisión queda en el decisionLog del AgentCore
         ▼
 IvannaAgentCore  →  cinco agentes especializados a cadencia ~1 Hz
         │  · AcousticPerceptionAgent — escena desde telemetría JNI real
         │  · DecisionAgent           — política por escena y por salud del sistema
         │  · DspControlAgent         — canales seguros (nunca toca el audio thread)
         │  · HealthMonitoringAgent   — clips · latencia · motor · daemon
         │  · OptimizationAgent       — ajustes automáticos reversibles
         ▼
 IvannaDSPOrchestrator  ──────────────────────────────────────────────────
         │  1. IvannaGlobalEffectManager.applyProfile()  — EQ/bass/virt/comp
         │  2. VoiceController.executeCommand()          — concert_mode, spatial…
         │  3. IvannaConversationalCore.recordAdjustment() — memoria de sesión
         ▼
 IvannaVoiceEngine  ──────────────────────────────────────────────────────
         │  · TTS con selección inteligente de voz española
         │  · Prosodia adaptada a la intención:
         │    SIMPLE:   confirmación directa ("He subido el volumen.")
         │    MUSICAL:  ritmo más lento, énfasis descriptivo para que el
         │              usuario procese los detalles técnicos de la configuración
         │    TECHNICAL: cadencia reducida para parámetros numéricos
         │  · Segmentación de frases → pausas reales entre ideas
         ▼
 Respuesta auditiva y visual al usuario
```

### Lenguaje acústico propio

IVANNA comprende 17 intenciones acústicas y español musical especializado:

| Lo que dices | Intención detectada | Acción DSP real |
|---|---|---|
| "Mejora las voces" | `VOICE_CLARITY` | HPF sidechain · boost 2–4 kHz · widener reducido |
| "Quiero más cine" | `MOVIE_IMMERSION` | RIR sala grande · HRTF completo · exciter al 70% |
| "Me duele la cabeza" | `LISTENING_FATIGUE` | Gentle mode · high-cut 16 kHz · compresor suave |
| "Más espacio" | `SPATIAL_EXPANSION` | ObjectRenderer 12 speakers · IDW interpolado |
| "¿Qué hiciste?" | `EXPLAIN` | Respuesta desde DecisionRecord ring buffer |
| "Hazlo épico" | `PRESET_EPIC` | EQ V-shape · spatial máximo · exciter 100% |
| "Neutro" | `FLAT_NEUTRAL` | Bypass parcial · curva plana ISO |

### Gemini 1.5 Flash — Capa Cognitiva Opcional

`IvannaGeminiAgent` conecta opcionalmente con Gemini 1.5 Flash para conversación amplia (cualquier tema, no solo audio). El sistema prompt incluye el vocabulario DSP completo y un protocolo de control:

```
[CMD:voice_clarity]   · [CMD:cinema_mode]    · [CMD:music_mode]
[CMD:concert_mode]    · [CMD:spatial_mode]   · [CMD:gentle_mode]
[CMD:flat_mode]       · [CMD:volume_up/down] · [CMD:bass_boost]
[CMD:treble_reduce]   · [CMD:auto_optimize]  · y más
```

Cuando Gemini detecta una intención acústica en la respuesta, el comando se parsea y ejecuta directamente sobre la cadena DSP nativa — sin UI intermedia. La IA habla, el DSP ejecuta.

### Memoria local — 100% en el dispositivo

Ningún dato de audio, voz ni preferencia sale del teléfono:

- **IvannaListenerProfile** — preferencias acústicas aprendidas · modo favorito · sensibilidad a fatiga · comandos frecuentes · ancho espacial preferido
- **IvannaContextMemory** — escena dominante anterior · última explicación de IVANNA · perfiles favoritos
- **IvannaConversationalCore** — canción actual · artista · último preset · últimos cambios DSP · preferencias temporales de sesión ("no me gustan los bajos muy fuertes")
- `clearMemory()` borra las tres capas en cascada sin tocar la configuración DSP permanente

### Explicabilidad — sin cajas negras

Cada acción queda en el `DecisionRecord` ring buffer de `IvannaAgentCore`. `explainLastDecision()` lo traduce a lenguaje humano:

> *"Reduje la agresividad de agudos porque detecté fatiga auditiva después de 40 minutos de escucha."*

---

## ◈ Perfiles de Artista — DSP Ajustado a la Producción Original

IVANNA incluye 12 perfiles de audio ajustados manualmente a la firma sonora de producciones discográficas reales:

| Perfil | Descripción de producción | Parámetros característicos |
|--------|--------------------------|---------------------------|
| **Steve Miller** | Classic rock americano — punchy mids, soft top-end | exciter moderado · ancho 1.05 · sin boost de bajos agresivo |
| **RUSH** | Rock progresivo técnico — estéreo ancho, transientes nítidos | imagen estéreo máxima · transient preservation · EQ plano con aire |
| **Budgie** | Heavy power trio — low end grueso, mids saturados | bass boost marcado · exciter en medios · highs controlados |
| **Grand Funk Railroad** | All-american rock — low-mid punch equilibrado | boost 200–500 Hz · sin coloración artificial extra |
| **Led Zeppelin** | *Bonham's Room* — batería ambiental de John Bonham | room mic mezclado al nivel del close mic · spatial amplio y profundo · dinámica preservada, compresión mínima |
| **Edgar Winter Group** | *Frankenstein Punch* — ARP synth, clavinet, bass synth | exciter para armónicos de sintetizador · presencia 2–4 kHz · low-end gordo y controlado |
| **Bachman-Turner Overdrive** | *Not Fragile Drive* — rock de autopista, tight y con torque | medios al frente · graves firmes · cero reverb artificial |
| **Music** | Perfil diario balanceado | realce moderado · sin coloración excesiva |
| **Voice** | Podcasts y llamadas | máxima inteligibilidad · ancho reducido · graves mínimos |
| **Gaming** | Audio posicional | imagen estéreo amplia · pasos y direccionalidad |
| **Studio** | Referencia de monitoreo | coloración mínima · fiel a la fuente |
| **Safe** | Sesiones largas / protección auditiva | ganancia y excursión limitadas |

---

## ◈ Daemon — El Plano de Control de Sistema

| Componente | Especificación técnica |
|-----------|----------------------|
| **`ivanna_daemon`** | PIE + FULL RELRO + BIND_NOW · `-static-libstdc++` (sin dependencias del APK) · **SCHED_FIFO prioridad 98** (bajo AudioFlinger=~99) · OOM-immune via Magisk |
| **Socket primario** | Unix abstracto `@omega_daemon_socket` · JSON con respuestas semánticas (`applied` / `accepted_pending_consumer` / generation) |
| **Socket secundario** | TCP loopback `:12121` — fallback automático si el socket abstracto no conecta |
| **SHM OmegaControlBus** | `/data/adb/ivanna_omega/omega_control_snapshot` · seqlock embebido · 512 bytes · MAGIC + VERSION + CRC32 · bidireccional |
| **Telemetría B→A** | `raw_rms` · `raw_peak` · `effect_frames` escritos por audioserver y leídos por la app — la UI sabe cuándo la Ruta B está activa |
| **Route Arbiter** | Estado explícito `OFF / IN_PROCESS / SYSTEM_WIDE` en cada snapshot |
| **Fatigue Control** | `SET_ROOM_RT60` · `GET_ROOM_STATUS` · high-cut IIR dinámico |

---

## ◈ Módulo Magisk — Plataforma Runtime Autónoma

El módulo Magisk de IVANNA no es un zip de archivos estáticos. Es un sistema operativo autónomo de 12 subsistemas:

```
ivanna_autonomous_core.sh — 12 subsistemas en tiempo de ejecución
  ├── 1.  Motor de Estado Autónomo     (UNKNOWN→BOOT→OPTIMAL→PROTECTED→DEGRADED→SAFE_MODE)
  ├── 2.  Mapeo Completo del Dispositivo  (device_runtime_profile: SoC, RAM, ROM, capabilidades)
  ├── 3.  Sistema Predictivo de Fallos    (security_intelligence_scan + evolution_layer_check)
  ├── 4.  Sistema de Memoria (Memory Core)  (memory_core.json + device_profile.json persistentes)
  ├── 5.  Reparación Autónoma (Self-Healing)  (detección y recuperación de estados degradados)
  ├── 6.  Motor de Decisiones              (decision_evaluate — precondiciones antes de cada acción)
  ├── 7.  Capa de Aislamiento              (isolation/ dir · componentes separados por fallos)
  ├── 8.  Sistema de Actualización Evolutiva  (evolution_layer_check + update hooks)
  ├── 9.  Seguridad Adaptativa             (SELinux policy reaplicada en cada boot — fix reboot loop)
  ├── 10. Cero Impacto Invisible           (OOM-immune · SCHED_FIFO · nohup sin rastro de shell)
  ├── 11. Observatorio Interno             (observatory.log · single source of truth timestampeada)
  └── 12. Modo Forensic                    (forensics/ dir · dump completo en fallos críticos)
```

**Fix SELinux reboot (auditado):** `customize.sh` aplica `sepolicy.rule` vía `magiskpolicy --live` solo en instalación — volátil. Sin reaplIcación post-reboot, SELinux enforcing deniega el `connect()` de `untrusted_app` al socket del daemon. `service.sh` lo reaplica en `late_start` con hasta 10 reintentos hasta que `magiskpolicy` esté disponible en el PATH.

### Contenido del módulo

```
magisk_module/
├── system/bin/ivanna_daemon             ← PIE · ARM64 · RELRO · BIND_NOW
├── system/vendor/lib64/soundfx/
│   └── libomega_effect.so               ← AudioFlinger GlobalEffect
├── system/etc/audio_effects_ivanna.xml  ← registro del effect en audioserver
├── system/etc/audio_effects_ivanna_omega.xml
├── system/etc/ivanna_omega/
│   ├── hrtf/          (12 datasets .ihr1)
│   ├── rir/           (200 WAV + metadata.csv)
│   └── SAF_model_total.json
├── sepolicy.rule
├── service.sh          ← daemon lifecycle · SELinux · SCHED_FIFO
├── core/
│   ├── ivanna_autonomous_core.sh  ← 12 subsistemas
│   └── ivanna_diag.sh
├── concert_mode.sh
├── health_check.sh
├── ivanna_control.sh
└── mqa_monitor.sh
```

---

## ◈ Offloading Hexagon DSP — Qualcomm Snapdragon

En SoCs Snapdragon con aDSP accesible, IVANNA puede descargar procesamiento pesado al Hexagon:

```cpp
// hexagon/ivanna_fastrpc_client.hpp + ivanna_fastrpc_client_load.cpp
// Selección automática en NpeEngine::process():
if (ivanna::hexagon::is_available()) {
    // FastRPC → Hexagon aDSP (convolución RIR · FIR upsampling)
} else {
    // CPU NEON fallback — sin interrupción de audio
}
```

- Interface IDL (`ivanna_fastrpc_client.idl`, `hexagon_dsp_integration.idl`) para comunicación tipada con el DSP
- Fallback CPU **sin latencia de transición** — el cambio es lock-free e invisible para el audio thread
- Aplica tanto a `libivanna_omega.so` como a `libomega_effect.so`

---

## ◈ Visualizador GL — Tres Shaders Reactivos al Audio

El visualizador de IVANNA usa GLSL ES 3.2 con **13 bandas Bark reales** extraídas del DSP nativo:

### `wallpaper_v2.glsl` — Aurora Kaleidoscópica Psicodélica
Domain warping fractal en 2 niveles · simetría caleidoscópica de 6–12 sectores modulada por bajos · paleta arco iris cromática que rota con el tiempo · 13 nodos orbitales pulsantes · aurora breathing con los medios · aberración cromática en brillos altos · **modo adaptativo**: `u_quality=1.0` (5 octavas fbm, 12 sectores) / `u_quality=0.5` (3 octavas, 6 sectores) para preservar framerate bajo carga.

### `circular_spectrum.glsl` — Espectro Circular de Bandas
Visualización polar del espectro de frecuencias en 13 bandas con animación reactiva a la energía por banda.

### `wave_particles.glsl` — Campo de Partículas de Onda
Partículas que forman y deforman ondas reactivas a la dinámica del audio en tiempo real.

---

## ◈ Suite IvannaLab — Medición Real, No Declarada

`ivannalab.cpp` / `ivannalab.h` implementan medición de calidad de audio profesional con resultados reales (no planceholders):

| Métrica | Implementación real | Rango de valores |
|---------|--------------------|--------------------|
| **THD** | DFT con ventana Hann · energía H2/H3/H4 vs fundamental | 0–100% · −1 = no medido |
| **IMD** | Doble tono SMPTE 250 Hz/8 kHz · productos laterales medidos | 0–100% · −1 = no medido |
| **LUFS integrado** | Filtrado K-weighting · pre-filter + RLB · gating BS.1770-4 | LUFS · −1 = no medido |
| **LRA** | Gated loudness high/low percentile (BS.1770-4 Annex 2) | LU · −1 = no medido |
| **SNR** | Relación señal/ruido sobre ventana de silencio de referencia | dB · −1 = no medido |
| **Peak** | Peak sample directo | dBFS · −1 = no medido |
| **True Peak** | Interpolación 4× para detectar inter-sample peaks | dBTP · −1 = no medido |

La UI expone estos valores en el LAB panel en tiempo real. Los campos devuelven −1.0f cuando los datos son insuficientes — honestidad de implementación explícita en el contrato de la API.

---

## ◈ Interceptación Global Sin Root

Para dispositivos sin Magisk, `IvannaGlobalEffectManager` usa el mecanismo estándar de Android:

```
Android → OPEN_AUDIO_EFFECT_CONTROL_SESSION (broadcast)
       → AudioSessionReceiver captura sessionId
       → IvannaGlobalEffectManager crea en esa sesión:
          · Equalizer 10 bandas (prioridad Int.MAX_VALUE)
          · BassBoost
          · Virtualizer estéreo
          · LoudnessEnhancer
          · DynamicsProcessing (compresor)
       → CLOSE_AUDIO_EFFECT_CONTROL_SESSION → liberación limpia
```

**Límite técnico honesto:** el DSP de convolución profunda (FusionCore, RIR, Volterra) requiere privilegios de sistema para inyectarse en el proceso de audio de otra app. Sin Magisk, se aplica EQ paramétrico + BassBoost + Virtualizer + compresor a todas las apps. El pipeline completo sigue activo para el reproductor propio de la app.

---

## ◈ UI — Instrumento de Precisión

**Aurora Obsidiana** — tema propio con gradientes negros-azules-violetas.  
**Jetpack Compose** — UI declarativa, reactividad total con StateFlow.

```
┌─────────────────────────────────────────────────────────────────┐
│  CONTROL   │   BRAIN   │  ADAPTIVE  │  SPATIAL  │   SYSTEM     │
├────────────┴───────────┴────────────┴───────────┴──────────────┤
│  CONTROL:  EQ 10 bandas interactivo · Exciter · Widener         │
│            Profiles selector con 12 presets de artista          │
│            Sparklines RMS en vivo                               │
│                                                                 │
│  BRAIN:    IVANNA ASSISTANT — orb animado por fase              │
│            Panel de inteligencia: intención · agente · acción   │
│            Panel de memoria: contexto · preferencias            │
│                                                                 │
│  ADAPTIVE: CMA-ES status · AdaptiveLearning buffer size         │
│            ThermalGovernor tier + headroom real                 │
│            Fatigue mitigation state                             │
│                                                                 │
│  SPATIAL:  HRTF selector por dataset                            │
│            ObjectRenderer 12 speakers (3D plot)                 │
│            RIR room selector por RT60                           │
│            SAF Φ^∞ latent vector display                        │
│                                                                 │
│  SYSTEM:   MagiskStatusPanel · daemon socket status             │
│            OmegaControlBus telemetría (RMS/Peak/frames)         │
│            NEON profiler panel                                  │
│            IvannaLab (THD/IMD/LUFS/SNR/TruePeak)               │
│            ISO 226 calibrator → aplicar en un toque             │
└─────────────────────────────────────────────────────────────────┘
```

Si un dataset no está desplegado, la UI lo dice en vez de simular que funciona. Los estados son honestos.

---

## ◈ Calidad Verificada — CI de Artefactos con Integridad

### 31 tests CTest — 100% pasando

| Suite | Cobertura |
|-------|-----------|
| `SafetyLimiterRegression` | PassthroughBelowThreshold |
| `CompressorRegression` | MakeupCompensatesRuntimeAmount |
| `AudioQualityMetrics` | SNR · THD · latencia · floor numérico (1 000 bloques sin NaN) |
| `ExciterOvershoot` | NeverExceedsFullScale · WetZeroIsTransparent · NoNaN · MaxDrive |
| `LimiterHiResTiming` | SoftKnee · AttackScalesWithSR · ReleaseRealtime · Idempotent |
| `test_adaptive_engine` | Control loop aislado · convergencia |
| `test_rir_dataset` | 200 WAVs reales verificados |
| `test_close_loop` | Lazo cerrado daemon↔effect |
| `test_stability` | 4.16 s de señal de peor caso · sin inestabilidad |
| `test_control_frame_bus_stress` | **15 segundos** de estrés del bus de control |
| `test_audio_bus` | Integridad del bus de audio |

### CI — Verificación de Integridad de Artefactos

El build falla (no advierte — **falla**) si:
- El daemon no es `ARM64 / PIE / RELRO / BIND_NOW`
- El zip Magisk carece de `system/bin/ivanna_daemon` o `sepolicy.rule`
- El APK CRC no coincide antes del staging
- Los binarios no pasan el hash de integridad post-build
- Cualquier binario precompilado aparece en el repo (paso de purga anti-contaminación)

### Supply Chain — Cadena de Suministro Verificada

Workflow adicional `.github/workflows/supply-chain.yml` que audita dependencias y la cadena de construcción completa.

---

## ◈ Números Reales del Proyecto

```
Codebase
──────────────────────────────────────────────────────
  245  archivos C++  (DSP nativo · neuromorphic · IPC · daemon · tests)
  188  archivos Kotlin  (audio · AI · UI · assistant · agent · saf)
   75  suites de test C++
   31  tests CTest en CI  (100% verdes)

Datos medidos embarcados
──────────────────────────────────────────────────────
  218  datasets SOFA AES69  (200 sujetos ARI + 18 datasets de sala/referencia)
   12  HRTFs IHR1  (KEMAR normal/grande, TU-Berlin, CIPIC ×7, Pulse, freefield)
  200  RIRs de salas reales  (WAV PCM + RT60 catalogado)
   12  perfiles de artista ajustados a producción discográfica real
   29  frecuencias ISO 226:2003  (equal-loudness calibrator)

Arquitectura
──────────────────────────────────────────────────────
    8  etapas DSP auditadas en la cadena nativa
   12  paquetes Kotlin  (agent · ai · assistant · audio · core · dsp · magisk
                        neuromorphic · preferences · saf · spatial · ui)
   32  canales gammatone del NeuroCochlear Manifold
   12  altavoces virtuales del ObjectRenderer (disposición dodecaédrica)
1024  taps FIR en el NPE Engine  (Blackman-Harris · 48→768 kHz)
  16×  factor de oversampling del NPE Engine
  512  bandas del EQ evolutivo CMA-ES
   50  ms  cadencia del AdaptiveDecisionEngine control loop
    2  s   polling del ThermalGovernor (desde hilo IO, nunca desde audio thread)
```

---

## ◈ Lo Que IVANNA No Hace — Honestidad de Ingeniería

- Sin root, la Ruta B (system-wide) no existe. La app cae a `AudioEffect` por sesión — EQ/DynamicsProcessing de Android sin el DSP profundo custom.
- Los datasets SOFA/RIR/HRTF ocupan espacio real en `/system/etc/ivanna_omega/` (montaje Magisk — sin tocar la partición del sistema).
- El PMU no es accesible en la mayoría de SoCs de consumo: el throughput GFLOPS se reporta como `N/M` en la UI, no como un número inventado.
- El Hexagon FastRPC solo activa en Snapdragon con aDSP accesible. En ARM genérico, NEON es el fallback y nadie lo nota.
- `IvannaGeminiAgent` requiere API key. Sin ella, `IvannaLanguageCore` + `IvannaCognitiveCore` siguen operando con toda su inteligencia on-device.

---

## ◈ Instalación

**Requisitos:** Android 10+ · Magisk o KernelSU (para Ruta B system-wide) · ARM64.

```bash
# 1. Desde el último CI verde: descarga el artefacto del job "Build APK & Native Binaries"
#    Contiene ivanna_omega_supreme.zip (módulo Magisk) + el APK

# 2. Flashea en Magisk/KSU → reinicia
#    El módulo instala: daemon PIE + libomega_effect.so + HRTF/RIR datasets + sepolicy

# 3. Instala el APK → abre IVANNA → concede permisos de captura si quieres Ruta A sobre otras apps

# La app y el módulo van a la par: v2.3.0 / versionCode 2300
```

---

<div align="center">

```
╔════════════════════════════════════════════════════════════╗
║                                                            ║
║   Construido muestra a muestra.  Auditado commit a commit. ║
║                                                            ║
║          © 2026 Luis Uriel Pimentel Pérez                  ║
║                    GORE TNS                                ║
║                                                            ║
║              ⬡  IVANNA OMEGA SUPREME  ⬡                    ║
║                                                            ║
╚════════════════════════════════════════════════════════════╝
```

</div>
