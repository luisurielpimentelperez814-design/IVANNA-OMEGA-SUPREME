# IVANNA OMEGA SUPREME



![IVANNA OMEGA SUPREME](https://img.shields.io/badge/Android-Audio%20Engine-green)




![Version](https://img.shields.io/github/v/release/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME)




![License](https://img.shields.io/github/license/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME)



## Motor de Audio Neural para Android — Módulo Magisk

IVANNA OMEGA SUPREME es una plataforma de procesamiento de audio en tiempo real para dispositivos Android con root (Magisk). Combina DSP nativo en C++, análisis de audio con IA (YAMNet), procesamiento espacial binaural y un daemon de sistema que opera a nivel kernel de audio.

---

## Arquitectura
Aplicación Android (Kotlin/Compose)
│
├── IvannaControlPanel — UI principal (OPE DSP, NPE, Spatial, Perfiles)
├── AdaptiveEngine — Motor adaptativo en tiempo real (10Hz telemetría)
├── AntiDolbyController — Clasificador YAMNet + EMA blend v2
└── PlaybackCaptureService — Captura PCM float32 hi-res (MediaProjection)
│
▼
DSP Nativo C++ (libivanna_omega.so)
├── OPE DSP — EQ / Compresor / Exciter / Widener
├── Motor NPE Neuromórfico — NHO + LIF + BiquadEnvelopeBank
├── Motor Binaural — 32 objetos HRTF
├── Algoritmo Evolutivo — Optimización genética de parámetros
└── IvannaUnifiedPipeline — Fuente de verdad telemetría Ruta B
│
▼
ivanna_daemon (proceso root, /dev/socket/ivanna_omega)
└── omega_effect.so — Plugin AudioEffect system-wide (Magisk)
---

## Componentes

### Motor OPE DSP
EQ paramétrico, compresor multibanda, exciter armónico y widener estéreo. Procesamiento en tiempo real vía `DSPState` → `pushToNative()`.

### Motor NPE Neuromórfico
NHO (Non-linear Harmonic Oscillator) + neuronas LIF + BiquadEnvelopeBank + AutonomousBrain. Clasificación de género musical, control de aspereza y compresión OHC.

### Anti-Dolby Adaptativo
Clasificador YAMNet con doble suavizado EMA (entrada α=0.25, salida α=0.18). Mezcla continua ponderada por tipo de contenido (voz/música/bajos/silencio) sin bucketeo discreto.

### Motor Binaural · 32 Objetos
HRTF espacial, COCLEAR y ADAPT/LIF. Procesamiento binaural con ángulo y ancho configurables.

### Adaptive Engine
Motor de decisión en tiempo real. Modos: NATURAL / STUDIO / EXTREME. Telemetría a 10Hz: RMS, Peak, GR dB, compresión, ancho espacial, protección de voz.

### Daemon Magisk (ivanna_daemon)
Proceso nativo root que crea `/dev/socket/ivanna_omega`. Aplica procesamiento system-wide a todas las apps (Spotify, YouTube, Tidal) vía `libomega_effect.so` como plugin AudioEffect del framework de audio de Android.

---

## Requisitos

- Android 10+ (API 29)
- Root con Magisk
- Permiso de captura de audio (MediaProjection)
- ARM64 (aarch64)

---

## Instalación

### 1. Módulo Magisk
Flashear `ivanna_omega_supreme` desde Magisk Manager. El daemon arranca automáticamente en boot vía `service.sh`.

### 2. APK
Instalar desde [Releases](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases/latest).

Paquetes disponibles:
- `app-release.apk` — Build de producción
- `app-debug.apk` — Build de desarrollo

### 3. Verificación post-instalación
```bash
# Desde Termux como root
getprop persist.ivanna.daemon_active   # debe ser 1
ls -la /dev/socket/ivanna_omega        # debe existir
ps -A | grep ivanna_daemon             # debe aparecer
Desarrollo
Stack:
Kotlin + Jetpack Compose (UI)
C++ NDK (DSP nativo)
Magisk Module API
Android AudioEffect framework
MediaProjection API (captura PCM)
TFLite (YAMNet)
Build:
./gradlew assembleDebug
./gradlew assembleRelease
GitHub Actions compila automáticamente en cada push a main.
Estado del proyecto
Componente
Estado
DSP Nativo (OPE)
✅ Activo
Motor NPE Neuromórfico
✅ Activo
Anti-Dolby EMA v2
✅ Conectado
Adaptive Engine telemetría
✅ Conectado
ivanna_daemon socket
✅ Persistente en boot
PlaybackCaptureService
✅ Conectado (requiere permiso MediaProjection)
Motor Binaural 32 obj
✅ Activo
Algoritmo Evolutivo
✅ Activo
Perfiles de sonido
✅ Activo
HRTF / COCLEAR / ADAPT-LIF
✅ Activo
Autor
Luis Uriel Pimentel Pérez
Estado de México, México
GitHub: @luisurielpimentelperez814-design
