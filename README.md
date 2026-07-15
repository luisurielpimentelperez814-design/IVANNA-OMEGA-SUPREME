# IVANNA OMEGA SUPREME

Motor de procesamiento de audio adaptativo para Android.

Autor: Luis Uriel Pimentel Pérez  
Licencia: Propietaria y confidencial  
Plataforma: Android 9+ (arm64-v8a), Snapdragon

---

## Estado actual del producto

Este documento describe el estado real del código a la fecha, verificado por auditoría directa del repositorio. No describe intenciones ni planes futuros.

---

## Qué funciona hoy

### Ruta de audio A — Reproductor propio (IvannaBridgePlayer)

Cuando el usuario reproduce un archivo desde el reproductor interno de IVANNA:

```
Archivo local
→ MediaExtractor/MediaCodec (PCM)
→ DSPBridge.process() / nativeProcessBlock()
→ ParametricEQ (8 bandas)
→ Compressor (threshold/ratio, con setRuntimeAmount adaptativo)
→ HarmonicExciter (2x oversampling, HPF 2.4 kHz, con setRuntimeReduction adaptativo)
→ StereoWidener (M/S crossover mono-safe de graves)
→ GainStage (input trim + output gain, con setRuntimeGain adaptativo)
→ PDEngine (NHO armónico + Spatial HRTF + EvolutionaryKernel)
→ SafetyLimiter (-0.1 dBFS ceiling, lookup table, sin malloc en RT)
→ AudioTrack → DAC
```

El AdaptiveDecisionEngine modula en tiempo real `target_gain`, `compressor_amount`, `exciter_reduction` y `spatial_width` sobre esta cadena. Las decisiones del ADE se basan en métricas reales medidas sobre la señal de salida: RMS, Peak, GainReduction del SafetyLimiter, y energía por banda espectral (low 80-200 Hz, mid 500 Hz-2 kHz, high 4-16 kHz) extraída de los envelopes IIR de 8 bandas que PDEngine ya calcula.

### Ruta de audio B — Apps externas con Magisk (Spotify, YouTube, etc.)

Requiere dispositivo con root y módulo Magisk instalado.

```
Spotify / YouTube / Tidal / juego / navegador
→ AudioFlinger (HAL Android)
→ libomega_effect.so (AudioEffect global registrado por Magisk)
→ applyAgc() — ganancia adaptativa + métricas → shared memory
→ omega_daemon (processLoop)
   → PFEngine (4-band EQ vía Biquads, low shelf 200 Hz / mid peak / high shelf 8 kHz / presence 3.5 kHz)
   → Compressor (mismo código que Ruta A, con setRuntimeAmount adaptativo)
   → HarmonicExciter (2x oversampling, HPF, con setRuntimeReduction adaptativo)
   → StereoWidener (M/S crossover mono-safe de graves)
   → SafetyLimiter (-0.1 dBFS ceiling, RT-safe)
   → target_gain del AdaptiveDecisionEngine (via ai_runtime_gain_mul en shared memory)
→ AudioTrack / salida del sistema
```

El ADE recibe métricas reales de esta ruta (RMS, Peak de applyAgc, band energies de 3 filtros IIR en omega_daemon). Aplica `target_gain`, `compressor_amount` y `exciter_reduction` sobre la cadena completa del daemon. Los tres parámetros viajan por `ai_runtime_gain_mul`, `ai_runtime_comp_amount` y `ai_runtime_exciter_red` en shared memory (campos añadidos junto a este cierre). `spatial_width` no tiene análogo en esta ruta (no hay PDEngine con HRTF en el daemon); sigue siendo gap conocido.

### AdaptiveDecisionEngine

Motor de decisión que corre en hilo de control a 20 Hz. Analiza las métricas de ambas rutas y publica un `AdaptiveState` via seqlock MPSC. El audio thread consume ese estado de forma lock-free.

Detecta:
- Exceso de energía (clip risk) → reduce target_gain
- Sibilancia (high-band ratio elevado) → sugiere exciter_reduction
- Fatiga espectral sostenida → ajusta compressor_amount
- Señal casi-mono → aumenta spatial_width

No toca SafetyLimiter. No opera dentro del audio thread.

---

## Qué no funciona o está incompleto

| Componente | Estado | Nota |
|---|---|---|
| VoiceController (control por voz) | Huérfano | Existe en código, no conectado a flujo principal |
| CloudSyncManager | Parcial | Requiere setup Firebase externo |
| USB DAC / AUX Reference Mode | Ausente | No implementado |
| IvannaLab (suite de medición THD/IMD/LUFS) | Skeleton | Stubs compilables; medición real pendiente |
| Ruta B: spatial_width adaptativo | Sin efecto | No hay PDEngine/HRTF en el daemon; gap conocido |
| Magisk: mqa_monitor.sh auto-preset | Sin verificar en vivo | El script existe, no hay evidencia de prueba en dispositivo real |
| NPE neuromorphic (modelo coclear) | Parcial | El .so existe, se inicializa, su impacto audible no está cuantificado |
| Hexagon DSP (FastRPC) | Huérfano | Código presente, sin enlace, no compila para producción |

---

## Arquitectura interna

```
App Android (Kotlin + Compose)
│
├── IvannaControlPanel           UI de sliders y presets
├── DSPBridge → libivanna_omega.so
│     ├── ParametricEQ           8 bandas
│     ├── Compressor
│     ├── HarmonicExciter
│     ├── StereoWidener
│     ├── GainStage
│     ├── SafetyLimiter          -0.1 dBFS, RT-safe
│     └── PDEngine
│           ├── NHO              Generador armónico Volterra
│           ├── Spatial          HRTF sintético
│           └── EvolutionaryKernel
├── AdaptiveDecisionEngine       Hilo de control 20 Hz, lock-free
├── IvannaNativeLib              API de bloque estéreo (nativeProcessBlock)
├── IvannaGlobalEffectManager    AudioEffect sessions (Spotify/YouTube via Android AudioEffect API, sin Magisk)
├── LearningBias                 EMA por parámetro/contexto, aprende correcciones del usuario
├── IvannaBridgePlayer           Reproductor de archivos locales
└── PlaybackCaptureService       MediaProjection: captura para análisis/visualizador

Módulo Magisk (requiere root)
├── libomega_effect.so           AudioEffect global en HAL
├── omega_daemon                 Daemon RT (SCHED_FIFO 98)
├── service.sh / post-fs-data.sh Scripts de instalación y watchdog
└── ivanna_control.sh            CLI de control
```

---

## Compilación

```bash
# APK debug
./gradlew assembleDebug

# Tests DSP nativos (host, sin Android)
cmake -B build-tests -DIVANNA_BUILD_TESTS=ON
ctest --test-dir build-tests --output-on-failure
```

Arquitectura: `arm64-v8a` exclusivamente (APK ~60% más pequeño).  
NDK: 25.1.8937393 | CMake: 3.22+ | AGP: 8.x | minSdk: 28 | targetSdk: 35
