<div align="center">

# ⬡ IVANNA OMEGA SUPREME

### Sistema de audio Android de grado OEM++ con DSP nativo, IA adaptativa y procesamiento espacial binaural

[![Build](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=github&label=BUILD&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Android](https://img.shields.io/badge/Android-14%2B-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com)
[![Root](https://img.shields.io/badge/Root-Magisk%20%2F%20KSU-FF3E86?style=for-the-badge)](https://github.com/topjohnwu/Magisk)
[![Language](https://img.shields.io/badge/C%2B%2B17-NEON%20ARM64-6FF3FF?style=for-the-badge)](https://developer.android.com/ndk)

</div>

---

## ¿Qué es IVANNA?

**IVANNA** no es un ecualizador. Es un motor de procesamiento de audio de sistema que corre a nivel de kernel con privilegios Magisk, procesa cada muestra de audio que produce el dispositivo, y usa inteligencia artificial para adaptar los parámetros en tiempo real según el contenido que escuchas.

La filosofía: **Qualcomm Hexagon DSP → NEON ARM64 → pipeline nativo → sin pasar por AudioFlinger cuando el hardware lo permite.**

---

## Arquitectura del motor

```
Audio del sistema
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│                   omega_effect.cpp (Magisk)                 │
│  AudioEffect HAL → SHM → Ruta A: daemon ivanna_omega RT    │
│                        → Ruta B: DynamicsProcessing API     │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              ivanna_omega_jni.cpp  (NEON ARM64)             │
│                                                             │
│  Pre-EQ peak guard (−1dBFS) → ParametricEQ (8 biquads)     │
│       → Compressor (sidechain HPF) → HarmonicExciter 2×OS  │
│       → StereoWidener M/S → PDEngine (NHO+Spatial)         │
│       → GainStage (output trim) → SafetyLimiter (−0.1dBFS) │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              Motores en paralelo (hilo IO 500ms)            │
│                                                             │
│  AdaptiveDecisionEngine ←→ IA clasificadora (4 clases)      │
│  ThermalGovernor (PowerManager API 29+, poll 2s)            │
│  IvannaSpatialManager (HRTF + head tracking)                │
│  RirConvolver (overlap-save FFT, crossfade 32 bloques)      │
│  EvolutionaryEQ (CMA-ES, hilo background)                   │
│  UsbAudioProManager (isochronous directo, 384kHz/32bit)     │
└─────────────────────────────────────────────────────────────┘
```

---

## Centro de control OEM++

La UI es el equivalente visual de un panel de control de fábrica. Accesible desde **SISTEMA → CENTRO DE CONTROL OEM++**.

### Dashboard principal
Estado en tiempo real del motor, backend activo (Hexagon DSP / NEON ARM64 / CPU fallback), ruta de audio, medidores de señal (RMS, Peak, GR, SAF), estado térmico y acceso a todos los módulos.

### Audio Espacial · HRTF · SOFA
Perfil HRTF activo desde dataset CIPIC/KEMAR, sujeto seleccionado, mapa 3D de posición sonora, controles de anchura y ángulo. Errores de diseño: ITD < 13.5 µs, ILD < 0.7 dB, interpolación < 0.4 dB.

### Acústica SAF · RIR
200 salas reales grabadas. Convolución overlap-save FFT con crossfade de 32 bloques en el dominio de la frecuencia — sin corte audible al cambiar de sala. No es reverberación sintética.

### IA Adaptativa
Clasificador de 4 clases (voz / música / bajos / silencio) con probabilidades en tiempo real. Ajuste automático de HRTF, RIR, ancho espacial y gestión energética. Aprendizaje on-device (bias EMA, sin modelo externo).

### Control Térmico
ThermalGovernor con política OEM de 4 niveles. Degrada proactivamente: excitador → espacialidad → compresor. El volumen nunca se toca. Nunca oculta al usuario por qué cambió el procesamiento.

### Telemetría OEM
Estado FastRPC/HAL, pipeline completo (11 métricas), latencia DSP medida (n=100 corridas CLOCK_MONOTONIC), conteo de clips, clasificador, características de audio, exportación de diagnóstico.

---

## Cadena DSP — lo que se resolvió

| Bug | Síntoma audible | Fix |
|-----|-----------------|-----|
| `mix=0.70f` default | +2.4 dB antes del EQ siempre | `mix=0.50f` (neutro) |
| Softclip orquestador `x/x = 1.0` | Hard clip = onda cuadrada | Saturación racional C¹ piecewise |
| Biquad state en `float` | Ruido en tails de reverb | Estado en `double` (error uniforme) |
| `nativeSetEQParams` master lineal→dB | Volumen mal calibrado | Conversión `20·log10(v)` con techo 6 dB |
| EQ bands cascada sin compensación | SafetyLimiter en compresión >10 dB = pumping | `eqOutputCompensationDb_` en GainStage |
| ISO 226 `/15f` espurio | Compensación 15× demasiado débil | Eliminada la división |
| Pre-EQ sin peak guard | IIR diverge → NaN → tronido | Guard −1 dBFS por bloque completo |
| `AIInferenceEngine × 1.05f` | +0.4 dB silencioso en cada bloque | Identidad (sin modelo TFLite cargado) |
| `wetNow_` actualizado 2×N veces | Imagen estéreo se corrompe en transiciones | Un loop por par de muestras |
| FFT twiddle recursivo en `float` | Ruido de piso en tails largos | `cos(ang·j)` directo en double |
| Tronidos inter-bloque | Clic periódico a f=SR/BLOCK | `std::isfinite` guard + self-reset |
| `PerceptualSnapshot` defaults altos | UI muestra 97% confianza sin señal | `dataAvailable=false` hasta audio real |
| `durationMin = 0f` siempre | Fatiga temporal nunca contribuye | `sessionStartMs` real |
| `SofaHRTFLoader` 4-byte signature | Acepta binarios que no son SOFA | Firma HDF5 de 8 bytes completa |

---

## Módulo Magisk

```
/system/lib64/libomega_effect_arm64.so   → AudioEffect hook del sistema
/system/bin/ivanna_daemon                → daemon RT con SHM
/system/etc/ivanna/hrtf/                 → perfiles IHR1 (CIPIC/KEMAR)
/system/etc/ivanna/rir/                  → 200 respuestas de impulso de sala
```

### Rutas de audio

```
Ruta A (root + daemon)     → latencia mínima, SHM, Hexagon DSP disponible
Ruta A degradada (root)    → sin daemon, usa AudioEffect directo
Ruta B (sin root)          → DynamicsProcessing API + NEON ARM64
```

---

## Stack tecnológico

```
Capa nativa   C++17 · NEON ARM64 · Biquad double-precision · FFT radix-2 DIT
Audio system  AudioEffect HAL · DynamicsProcessing · AudioFlinger bypass (Ruta A)
IA            Clasificador 4 clases · CMA-ES · EMA bias learning · PerceptualBrain
Espacial      HRTF CIPIC/KEMAR · overlap-save convolver · crossfade espectral
Acústica      200 RIR reales · SAF · RT60 estimado · difusión
Térmico       PowerManager API 29+ · 4 niveles · degradación proactiva
UI            Jetpack Compose · Material 3 · dual mode usuario/experto
Build         GitHub Actions · NDK CMake · artifact SHA256 determinístico
```

---

## Build

```bash
# Clonar
git clone https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git
cd IVANNA-OMEGA-SUPREME

# Build (requiere NDK r27+)
./gradlew assembleDebug

# Tests nativos DSP
cd app/src/main/cpp && cmake -B build && cmake --build build && ./build/ivanna_dsp_tests
```

---

## Estructura del proyecto

```
app/src/main/
├── cpp/
│   ├── dsp/                    ← Compressor, HarmonicExciter, SafetyLimiter, ParametricEQ
│   ├── spatial/                ← RirConvolver, HRTFConvolver, RirDataset
│   ├── jni/                    ← ivanna_omega_jni.cpp (proceso principal)
│   ├── neuromorphic/           ← NPE engine
│   └── tests/                  ← CTest DSP regression suite
└── java/com/ivanna/omega/
    ├── ui/
    │   ├── oem/                ← OemDashboard, Spatial, Acoustic, AI, Thermal, Telemetry
    │   └── ...                 ← pantallas existentes
    ├── audio/                  ← ThermalGovernor, RouteDspCalibrator, UsbAudioProManager
    ├── ai/                     ← PerceptualBrainEngine, PerceptualDecisionEngine
    └── spatial/                ← IvannaSpatialManager, HeadTrackingManager
```

---

<div align="center">

**GORE TNS** · Solo developer · Moto G85 · Termux + GitHub Actions

*"Más motores que pantallas — pero ya no."*

</div>
