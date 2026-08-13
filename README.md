<div align="center">

```
██╗██╗   ██╗ █████╗ ███╗   ██╗███╗   ██╗ █████╗
██║██║   ██║██╔══██╗████╗  ██║████╗  ██║██╔══██╗
██║██║   ██║███████║██╔██╗ ██║██╔██╗ ██║███████║
██║╚██╗ ██╔╝██╔══██║██║╚██╗██║██║╚██╗██║██╔══██║
██║ ╚████╔╝ ██║  ██║██║ ╚████║██║ ╚████║██║  ██║
╚═╝  ╚═══╝  ╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═══╝╚═╝  ╚═╝
```

# IVANNA OMEGA SUPREME

### Motor DSP neuromorfico system-wide para Android ARM64

*16,654 líneas de C++ · 25,459 de Kotlin · 434 datasets HRTF · 1,218 commits*

---

[![CI](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/version-v2.2.0-FFD700?style=flat-square)](#)
[![API](https://img.shields.io/badge/API-28%2B-3DDC84?style=flat-square&logo=android)](https://developer.android.com/about/versions/pie)
[![NDK](https://img.shields.io/badge/NDK-25.1-0075C4?style=flat-square)](https://developer.android.com/ndk)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus)](https://isocpp.org/)
[![Kotlin](https://img.shields.io/badge/Kotlin-1.9-7F52FF?style=flat-square&logo=kotlin)](https://kotlinlang.org/)
[![Magisk](https://img.shields.io/badge/Magisk-20.4%2B-blueviolet?style=flat-square)](https://github.com/topjohnwu/Magisk)
[![HRTF](https://img.shields.io/badge/HRTF-434_datasets-FF6B35?style=flat-square)](#hrtf)
[![Tests](https://img.shields.io/badge/tests-8_suites-success?style=flat-square)](#tests)

---

| | | | | | |
|:---:|:---:|:---:|:---:|:---:|:---:|
| **16,654** | **25,459** | **434** | **214** | **8** | **1,218** |
| líneas C++ | líneas Kotlin | datasets HRTF SOFA | sujetos Fisher Φ_SAF∞ | suites de test | commits |

</div>

---

## ¿Qué es IVANNA?

IVANNA es un motor de procesamiento de señal de audio que opera a nivel de sistema en Android — no a nivel de app, sino interceptando **AudioFlinger** vía insert effect para procesar **todo el audio del dispositivo** en tiempo real, de cualquier aplicación, sin que ellas lo sepan ni lo soliciten.

Mientras Dolby Atmos requiere hardware propietario y acuerdos OEM, y Apple Spatial Audio necesita Apple Silicon y sensores TrueDepth, IVANNA corre en **cualquier Android ARM64 rooteado** con una pila de algoritmos que ningún producto consumer implementa.

```
┌──────────────────────────────────────────────────┐
│          APLICACIONES (todas, sin excepción)     │
│      Spotify · YouTube · Llamadas · Juegos       │
└────────────────────┬─────────────────────────────┘
                     │  PCM sin procesar
                     ▼
┌──────────────────────────────────────────────────┐
│              AUDIOFLINGER (Android)              │
│         insert effect → libomega_effect.so       │
└────────────────────┬─────────────────────────────┘
                     │
          ┌──────────▼──────────┐
          │   PIPELINE IVANNA   │
          │                     │
          │  Phase Oracle       │
          │  Anti-Dolby CRNN    │
          │  Volterra H2        │
          │  Evo EQ (128 ind.)  │
          │  SAF Φ_SAF∞         │
          │  HRTF 434 SOFA      │
          │  RIR Salas reales   │
          │  PI-LSTM CT-RK4     │
          │  ISO 226 Loudness   │
          │  Head Tracker 6DoF  │
          └──────────┬──────────┘
                     │  PCM espacial personalizado
                     ▼
             ┌───────────────┐
             │  AURICULARES  │
             └───────────────┘
```

---

## Pipeline de procesamiento

El audio atraviesa 9 etapas en cadena, todas en C++17 ARM64 con NEON SIMD:

### [1] Phase Oracle — `phase_oracle.cpp`

Predictor de muestras basado en **Kalman cúbico** + **embedding de Takens** a Δt = 1/384.000 s.
Análisis tiempo-frecuencia instantáneo via **Stockwell-256**.
Predice muestras antes de que lleguen físicamente al buffer — latencia subjetiva reducida antes de cualquier procesamiento.

### [2] Anti-Dolby CRNN — `anti_dolby.cpp`

Clasificador que detecta patrones de compresión comercial (Dolby, DTS, propietarios).
Invierte el procesamiento detectado con `AtomicWidenerMultiplier` atómico.
Transiciones sin glitch: mutex-guarded smoothing sobre `targetWidener` → `smoothedWidener`.

### [3] Volterra H2 — `omega_effect.cpp`

Kernel simétrico de **segundo orden** para modelado no-lineal.
Reproduce la distorsión armónica que introduce la membrana timpánica humana.
**Ausente en cualquier producto consumer Android del mercado.**

### [4] Evolutionary EQ — `evolutionary_kernel.cpp`

```
Población:    128 individuos
Genoma:       256 genes uint8
Élite:        4 individuos preservados
Fitness:      energy_mean × (1 − 0.85 × variance)
Mutación:     1% por generación
Persistencia: auto-save cada 25 gen. (EVO_SAVE_MAGIC = 0x494F4B31)
```

El EQ **se adapta al audio real que está sonando**, no a una curva predefinida.
Persiste entre reinicios de la app.

### [5] SAF Φ_SAF∞ — `saf_optimizer.cpp`

Optimizador de HRTF por **gradiente Riemanniano** sobre la variedad de respuestas de cabeza individuales.

- Métrica de Fisher **G₀** construida con **214 sujetos reales**
- Modelo latente: `SAF_model.json` (1.3 MB, espacio PCA)
- **K=7 vecinos** más cercanos para personalización continua
- Se adapta al perfil de escucha del usuario sin necesidad de foto de oído ni hardware especial

### [6] HRTF Convolver — `hrtf_convolver.cpp`

```
Datasets:   434 archivos SOFA (medidos en cámara anecoica)
Posiciones: 1.250 posiciones esféricas por dataset
Taps:       512 coeficientes por filtro (L + R)
Formato:    float32 precisión completa
```

Convolución en dominio frecuencial para HRTF de fase lineal.

### [7] RIR Convolution — `RirConvolver.cpp` + `RirDataset.cpp`

Room Impulse Response — inyecta la acústica de salas reales medidas al campo HRTF.
Sin reverberación sintética — impulsos de salas reales.

### [8] PI-LSTM Milenio v2.0 — `pi_lstm_bridge_jni.cpp`

```
96 kHz → ×4 upsample → CT-LSTM RK4 → HRTF → ×4 downsample → salida
```

Procesamiento neuromorfico en dominio de tiempo continuo (**CT-LSTM**, Runge-Kutta 4).
Opera a 384 kHz internamente para capturar detalles de fase que a 48 kHz se pierden.

### [9] ISO 226 Calibración — `Psychoacoustics.cpp`

Curvas de **igual-loudness perceptual** por banda.
Calibración desde la UI con sliders en tiempo real.
Compensa la respuesta no-lineal del oído humano según la norma ISO 226.

---

## Control Plane

```
┌─────────────────┐   AbstractSocket   ┌──────────────────────┐
│   APP (Kotlin)  │ ◄────────────────► │   ivanna_daemon      │
│  MagiskBridge   │  @omega_daemon_    │   SCHED_FIFO P80     │
│  OmegaEngine    │  socket            │   Backoff 2-32s      │
│  Bridge         │                   │   Abstract namespace  │
└────────┬────────┘                   └──────────┬───────────┘
         │                                        │
         └────────────────┬───────────────────────┘
                          │
              ┌───────────▼────────────┐
              │      SHM HYPERPLANE    │
              │  OmegaDspSnapshot      │
              │  (CRC32 + generation)  │
              │  OmegaControlBus       │
              │  Seqlock atómico       │
              │  Zero-lock hot path    │
              └────────────────────────┘
```

El **seqlock** garantiza que el hilo de audio nunca bloquea esperando parámetros de la UI.
Cada cambio de parámetro incrementa un contador de generación — trazabilidad completa.

---

## Módulo Magisk

### Ciclo de vida

```
BOOT
  ├─ post-fs-data.sh
  │    Contador anti-bootloop (LAST_OK file)
  │    Validación ELF: verifica INTERP, DYN, AArch64, PIE antes de ejecutar
  │    Setup: persist.ivanna.daemon_active, persist.ivanna.magisk_active
  │
  ├─ service.sh
  │    Deploy SAF_model.json   → /data/adb/ivanna_omega/  (siempre actualizado)
  │    Deploy hrtf_dataset.ihr1 → /data/adb/ivanna_omega/ (siempre actualizado)
  │    exec ivanna_daemon --socket @omega_daemon_socket --realtime
  │    Watchdog: backoff exponencial 2→4→8→16→32s
  │    mqa_monitor.sh en background (PID file para gestión limpia)
  │    Verificación de socket vía grep /proc/net/unix
  │
  └─ RUNTIME
       @omega_daemon_socket  → DSP commands (MagiskBridge.kt)
       @omega_command_socket → Admin control (OmegaEngineBridge.kt)
       SHM seqlock           → Control plane cross-process

UNINSTALL
  └─ uninstall.sh
       kill daemon (PID file)    kill mqa_monitor (PID file)
       magiskpolicy --live deny  (cleanup SELinux en vivo)
       rm -rf /data/adb/ivanna_omega/logs/
       ui_print "REINICIA para limpiar SELinux del kernel"
```

### SELinux — 137 líneas de política

Cubre `untrusted_app`, `isolated_app`, `platform_app`, `priv_app` con permisos mínimos:
solo lo necesario para que la app conecte al daemon via socket abstract y SHM.

---

## Suites de test

```
8 suites CTest / GTest 1.14 — compiladas en host Ubuntu x86_64 (C++17)

✅ dsp_core_stability           Estabilidad del pipeline DSP bajo carga sostenida
✅ gammatone_numerical_stability Banco de filtros Gammatone — cero divergencia numérica
✅ no_denormals_low_level       Cero denormales en hot path (verifica FTZ/DAZ ARM64)
✅ test_audio_bus               OmegaControlBus publish/read round-trip correcto
✅ test_audio_quality_metrics   SNR · THD · latencia · piso numérico (6 métricas)
✅ test_control_frame_bus_stress Seqlock bajo acceso concurrente (stress)
✅ test_regression_tuning       Regresión de parámetros de tuning DSP
✅ test_rir_dataset             Carga y convolución de Room Impulse Response
```

---

## Comparativa técnica

| Capacidad | Dolby Atmos | Apple Spatial | Sony 360RA | **IVANNA** |
|-----------|:-----------:|:-------------:|:----------:|:----------:|
| Insert system-wide (todo el audio) | ✅ OEM | ✅ Apple hw | ✅ Sony hw | ✅ Magisk |
| Volterra H2 no-lineal | ❌ | ❌ | ❌ | **✅** |
| HRTF Riemanniano (214 sujetos) | ❌ | ❌ | ❌ | **✅** |
| Phase Oracle Kalman cúbico | ❌ | ❌ | ❌ | **✅** |
| Motor evolutivo fitness real | ❌ | ❌ | ❌ | **✅** |
| RIR de salas reales | ❌ | ❌ | ❌ | **✅** |
| ISO 226 equal-loudness | ❌ | ❌ | ❌ | **✅** |
| Head tracking 6DoF | Algunos OEM | ✅ AirPods | ✅ WH-1000XM5 | **✅ Cualquier Android** |
| Aceleración hw DSP | ✅ Hexagon | ✅ Apple Silicon | ✅ LDAC hw | ❌ soft NEON |
| Sin hardware propietario | ❌ | ❌ | ❌ | **✅** |
| Código abierto | ❌ | ❌ | ❌ | **✅** |
| Dispositivos | Solo OEM | Solo Apple | Solo Sony | **Todo Android ARM64** |

> **Nota real:** Dolby, Apple y Sony tienen ventajas de hardware que software puro no iguala en eficiencia energética y latencia de silicio. IVANNA compite en algoritmos y universalidad, no en integración OEM.

---

## Instalación

**Requisitos:** Android 9+ (API 28) · Root (Magisk 20.4+ o KernelSU) · ARM64

```bash
# 1. Descarga ivanna-omega-magisk.zip desde Releases
# 2. Magisk Manager → Módulos → Instalar desde almacenamiento
# 3. Reinicia

# Verificar que todo está activo:
su -c "getprop persist.ivanna.daemon_active"           # → 1
su -c "grep '@omega_daemon_socket' /proc/net/unix"     # → línea visible
su -c "tail -20 /data/adb/ivanna_omega/daemon.log"    # → log del daemon
```

---

## Estructura

```
IVANNA-OMEGA-SUPREME/
├── app/src/main/
│   ├── cpp/
│   │   ├── evolutionary_kernel.cpp  ← EQ evolutivo (GENOME_SIZE=256, P=128)
│   │   ├── phase_oracle.cpp         ← Kalman cúbico + Takens 384kHz
│   │   ├── anti_dolby.cpp           ← Clasificador + inversión compresión
│   │   ├── omega_effect.cpp         ← AudioFlinger insert (unity-build)
│   │   ├── saf_optimizer.cpp        ← Gradiente Riemanniano Φ_SAF∞
│   │   ├── pi_lstm_bridge_jni.cpp   ← CT-LSTM RK4 (96kHz continuo)
│   │   ├── spatial/
│   │   │   ├── RirConvolver.cpp     ← Room Impulse Response real
│   │   │   ├── hrtf_convolver.cpp   ← 434 SOFA, 512 taps
│   │   │   └── ivanna_head_tracker.cpp
│   │   ├── daemon/
│   │   │   ├── ivanna_daemon.cpp    ← SCHED_FIFO P80, abstract socket
│   │   │   └── control/command_server.cpp
│   │   ├── jni/ (9 bridges)
│   │   └── tests/ (8 suites CTest)
│   ├── java/com/ivanna/omega/
│   │   ├── magisk/MagiskBridge.kt   ← Socket abstract, retry logic
│   │   ├── spatial/IvannaHeadTracker.kt ← 6DoF sensor fusion
│   │   └── audio/AudioEngine.kt    ← runCatching en 5 JNI entry points
│   └── assets/sofa/ (434 datasets)
├── magisk_module/
│   ├── service.sh · customize.sh · post-fs-data.sh · uninstall.sh
│   ├── mqa_monitor.sh · ivanna_control.sh
│   ├── sepolicy.rule (137 líneas)
│   └── saf/SAF_model.json (1.3 MB)
└── .github/workflows/build.yml  ← CI: build + 8 tests + SBOM + Cosign SLSA L2
```

---

<div align="center">

**IVANNA OMEGA SUPREME v2.2.0**

*© 2025–2026 Luis Uriel Pimentel Pérez — Gore TNS*

*Construido en un Moto G85 desde Termux.*
*Corre en cualquier Android ARM64 rooteado.*

</div>
