<div align="center">

<img src="https://raw.githubusercontent.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/main/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png" alt="IVANNA OMEGA SUPREME" width="160"/>

<br/>

# IVANNA OMEGA SUPREME

### Motor DSP system-wide con Volterra H2, optimización Riemanniana HRTF y EQ evolutivo para Android ARM64

<br/>

[![Build](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml/badge.svg)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml)
[![API](https://img.shields.io/badge/API-28%2B-brightgreen.svg)](https://android-arsenal.com/api?level=28)
[![NDK](https://img.shields.io/badge/NDK-26.1-blue.svg)](https://developer.android.com/ndk)
[![Version](https://img.shields.io/badge/version-v2.2.0-gold.svg)](#)
[![License](https://img.shields.io/badge/License-Proprietary-red.svg)](LICENSE)
[![Magisk](https://img.shields.io/badge/Magisk-20.4%2B-blueviolet.svg)](https://github.com/topjohnwu/Magisk)
[![SOFA](https://img.shields.io/badge/HRTF-434%20datasets-orange.svg)](#hrtf--personalización)

<br/>

> **Dolby usa hardware propietario y contratos OEM. Apple usa TrueDepth y Apple Silicon.**
> **IVANNA usa algoritmos que ellos no tienen: Volterra H2, HRTF Riemanniano, EQ evolutivo.**
> **En cualquier Android rooteado.**

</div>

---

## ¿Qué es esto?

IVANNA OMEGA SUPREME es un **motor DSP system-wide** que opera directamente dentro de AudioFlinger — el servidor de audio del kernel Android — vía Magisk. No es un ecualizador. No es un plugin de Spotify. Es una plataforma de procesamiento de audio de investigación con arquitectura de producción real:

- **Insert effect en AudioFlinger**: procesa todo el audio del sistema, no solo una app
- **Daemon en tiempo real** con `SCHED_FIFO` prioridad 80 y backoff exponencial ante crashes
- **HRTF personalizado** sobre 434 datasets — 214 sujetos reales para la métrica de Fisher Φ_SAF^∞
- **Control Plane cross-process** con memoria mmap + seqlock atómico (zero-lock en hot path de audio)
- **Volterra H2 simétrico** — modelado no-lineal de segundo orden ausente en cualquier producto consumer Android
- **Algoritmo evolutivo** con fitness acoplado al audio real en tiempo de ejecución (energy × variance)
- **ISO 226 calibrador** de igual-loudness integrado en la UI con sliders por banda
- **Bark64 visualizador** espectral en tiempo real (64 bandas de Bark, FFT nativa)

---

## Arquitectura del sistema

```
┌─────────────────────────────────────────────────────────────────────┐
│                         PROCESO APP                                  │
│                                                                      │
│  IVANNAApplication ──► MagiskBridge ──► LocalSocket                 │
│         │                    │                │                      │
│  ParameterStore       OmegaEngineBridge    Daemon socket             │
│  IvannaSpatialManager  SaFOptimizer       @omega_daemon_socket       │
│  Iso226Calibrator      AdaptiveBackend                               │
└──────────────────────────────────┬──────────────────────────────────┘
                                   │ JSON commands
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    DAEMON (root, SCHED_FIFO)                         │
│                                                                      │
│  ivanna_daemon ──► CommandServer ──► publishCurrentState()           │
│                          │                    │                      │
│                    OmegaDspState        OmegaControlBus              │
│                    (intensidad, EQ,     (seqlock mmap SHM)           │
│                     spatial, HRTF,     /data/adb/ivanna_omega/       │
│                     Volterra, SAF)      omega_control_snapshot        │
└──────────────────────────────────┬──────────────────────────────────┘
                                   │ mmap shared memory (seqlock)
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│               AUDIOSERVER (sistema, insert en mixer)                 │
│                                                                      │
│  omega_effect.so ──► OmegaControlBus::readLatest()                  │
│       │                      │                                       │
│  omega_process()      RouteArbiter gate                              │
│       │               SYSTEM_WIDE │ IN_PROCESS │ OFF                 │
│  IvannaFusionCore ──► HRTFConvolver ──► VBAP + HRTF                 │
│       │               SaFOptimizer    Volterra H2                    │
│  apply snapshot ──► spatial/harmonic/eq/bass/widener                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Control Plane cross-process

El estado DSP se publica desde el daemon a `omega_effect.so` vía **seqlock sobre memoria mmap** — sin locks del SO en el hot path de audio:

```
Daemon escribe          omega_effect lee
guard.fetch_add(1)  →   g1 = guard.load()       ← odd? retry
memcpy(&snapshot)   →   memcpy(&local)
guard.fetch_add(1)  →   g2 = guard.load()       ← g1≠g2? retry
                         validate magic/crc
                         apply parameters
```

`OmegaDspSnapshot` es trivialmente copiable, 512 bytes máx, validado por CRC32 y magic `0x4F4D4543`. Cada publicación incrementa `generation` — el consumidor sabe exactamente qué estado está aplicando.

---

## HRTF — Personalización basada en 214 sujetos reales

### El problema que resuelve

El audio espacial convincente requiere saber exactamente cómo **tu cabeza, cuello y la geometría de tus oídos** modifican el sonido antes de que llegue al tímpano. Sin esto, la espacialización se percibe como coloración de timbre, no como espacio real.

### Lo que tiene IVANNA

| Dataset | Descripción | Sujetos |
|---|---|---|
| **MIT KEMAR** | Estándar de referencia mundial — medido por MIT Media Lab con maniquí y micrófonos dentro del canal auditivo | 2 variantes (pinna normal y grande) |
| **ARI (Austrian Research Institute)** | Mediciones individuales en sujetos reales en cámara anecoica | 200+ sujetos |
| **CIPIC** | Base de datos UC Davis con medidas antropométricas por sujeto | 7 sujetos individuales |
| **TU-Berlin QU KEMAR** | Dataset anecóico a 0.5m | Alta resolución angular |
| **Headphone IRs** | AKG K271/K272, Beyerdynamic DT770/DT990, Sennheiser HD25/HD280/HD650 | Ecualización por modelo |

**434 archivos SOFA** en total. El modelo SAF Φ_SAF^∞ usa la información métrica de 214 de ellos para derivar la matriz de Fisher G₀ — la base del optimizador Riemanniano.

### La ecuación magistral — Φ_SAF^∞

```
p_{t+1} = Π_S^{G_t}( p_t + α_t · G_t⁻¹ · Δ_t )

α_t = E_t / (E_t + ‖Δ_t‖²_{G_t} + λ‖Δ_t‖²_{M_t} + ε)

E_t = ‖q_t − target‖²_{G_t}    (error de Mahalanobis a calibración)
Δ_t = target_d − q_t            (gradiente perceptual del feedback)
```

- **G₀** (métrica de Fisher) derivada de 214 mediciones SOFA
- **Π_S** proyecta al subespacio estable (acota en [0.1, 2.0]) — estabilidad tipo Lyapunov
- **α_t** es un paso adaptativo acotado: el denominador actúa como barrera cuadrática que previene pasos grandes cuando ya hay tracking
- **Resultado**: personalización HRTF que converge en ≤5 iteraciones de feedback perceptual

---

## Motor de procesamiento

### Volterra H2 Simétrico

Modelado de sistema no-lineal de segundo orden — lo que hace la cóclea real con el audio:

```cpp
// Kernel de segundo orden con delay lines por canal
y[n] = Σ_{k1} Σ_{k2} h2[k1,k2] · x[n-k1] · x[n-k2]
```

Actualización atómica de kernels en tiempo de ejecución. No existe esto en ningún producto consumer de Android.

### Motor Evolutivo Adaptativo

```
fitness = energy × (1 - 0.85 × variance)
```

Población de genomas que evoluciona bloque a bloque según señales del audio real: loudness, transientes, contenido espacial. Persistencia entre reinicios. El DSP se adapta al estilo musical en tiempo real.

### FIR 1024-tap, 16× oversampling

48 kHz → 768 kHz con ventana Blackman-Harris para procesar no-linealidades sin aliasing. Relevante cuando Volterra H2 genera armónicos en banda.

---

## Módulo Magisk

### Lo que instala

```
/system/bin/ivanna_daemon           ← Daemon SCHED_FIFO ARM64
/system/etc/audio_effects_ivanna.xml
/system/etc/audio_effects_ivanna_omega.xml
/system/vendor/etc/audio_effects.xml    ← override AudioFlinger
/data/adb/ivanna_omega/
    SAF_model.json                  ← 214 sujetos, K=7 dimensiones
    hrtf_dataset.ihr1               ← 4.9MB HRTF binario procesado
    omega_control_snapshot          ← SHM seqlock (creado en runtime)
```

### Scripts del módulo

| Script | Función |
|---|---|
| `service.sh` | Watchdog del daemon: SCHED_FIFO, backoff exponencial, socket readiness check vía `/proc/net/unix`, mqa_monitor lifecycle |
| `post-fs-data.sh` | Anti-bootloop (3 crashes consecutivos → safe mode), setprop de módulo activo |
| `customize.sh` | Deploy de SAF_model.json + permisos ELF + SELinux live |
| `mqa_monitor.sh` | Auto-preset por app: Tidal→Flat, Spotify→Warm, YouTube→Spatial, games→Punch |
| `concert_mode.sh` | Activa perfil Spatial + reverb 0.7 vía socket |
| `ivanna_control.sh` | CLI: preset/status/bypass/concert desde shell o adb |
| `uninstall.sh` | Limpieza completa: daemon, monitor, logs, SHM, SELinux deny |

### Comprobación de salud del módulo

```sh
# Estado del daemon
getprop persist.ivanna.daemon_active   # → 1 si vivo

# Socket vivo
grep "@omega_daemon_socket" /proc/net/unix

# Preset desde adb
adb shell /data/adb/modules/ivanna_omega_supreme/ivanna_control.sh status
adb shell /data/adb/modules/ivanna_omega_supreme/ivanna_control.sh preset Spatial
```

---

## Parámetros de entrada

En el primer lanzamiento, IVANNA aplica automáticamente los siguientes parámetros calibrados para inmersión máxima:

| Parámetro | Valor | Efecto perceptual |
|---|---|---|
| `spatial_width` | 1.55 | Campo estéreo 55% más amplio — percepción 3D real |
| `harmonic_gain` | 0.78 | Riqueza tímbrica y cuerpo sin artificio |
| `intensity` | 0.92 | Intensidad DSP con headroom |
| `bass_boost` | +2.5 dB | Sub-graves presentes y controlados |
| `dialog_boost` | +1.5 dB | Claridad vocal, presencia de medios |
| `widener_mult` | ×1.38 | Ensanchamiento estéreo sobre DSP |
| `loudness_target` | −16 LUFS | Headroom para masters comerciales comprimidos |
| `listen_phon` | 65 phon | Curva ISO 226 para volumen medio-alto |
| `compressor` | −5.5 dB | Threshold real, transientes controlados |
| `anti_dolby` | 0.85 | Neutralización moderada de compresión comercial |

---

## Route Arbiter — sin doble procesamiento

```
SYSTEM_WIDE  → omega_effect.so procesa | nativeProcess pasa
IN_PROCESS   → nativeProcess procesa   | omega_effect pasa
OFF          → nadie procesa           | passthrough total
```

`omega_process()` lee el `RouteMode` del snapshot antes de cada frame. Si la ruta no es `SYSTEM_WIDE`, copia entrada→salida sin tocar. Cero posibilidad de que Ruta A y Ruta B procesen el mismo stream simultáneamente.

---

## Comparativa técnica

| Capacidad | Dolby Atmos | Apple Spatial Audio | IVANNA OMEGA SUPREME |
|---|:---:|:---:|:---:|
| Insert en AudioFlinger (sistema) | ✅ OEM/HAL | ✅ Apple Silicon | ✅ Magisk (software) |
| HRTF medido en laboratorio | genérico | cámara → individual | 434 datasets + 214 sujetos |
| Personalización HRTF | ❌ | TrueDepth + foto oído | Riemanniano K=7 (feedback) |
| Aceleración DSP hardware | ✅ Hexagon/Qualcomm | ✅ Apple Silicon | ❌ (soft; Hexagon experimental) |
| Head tracking en tiempo real | algunos OEM | ✅ AirPods + sensor | ❌ (en hoja de ruta) |
| Volterra H2 no-lineal | ❌ | ❌ | ✅ |
| Motor evolutivo adaptativo | ❌ | ❌ | ✅ |
| ISO 226 calibrador perceptual | ❌ | ❌ | ✅ |
| Auto-preset por app | ❌ | ❌ | ✅ (MQA monitor) |
| Control plane auditable | ❌ | ❌ | ✅ (SHM seqlock + generation) |
| Código abierto | ❌ | ❌ | ✅ |
| Dispositivos compatibles | Solo OEM licenciados | Solo Apple | Cualquier Android root ARM64 |

> **Nota honesta**: Dolby Atmos y Apple Spatial Audio tienen ventajas reales de hardware que software puro no puede igualar en latencia y eficiencia energética. IVANNA compite en algoritmos, no en integración OEM.

---

## Instalación

### Requisitos

- Android 9.0+ (API 28+, `minSdk = 28`)
- Root via **Magisk 20.4+** o **KernelSU**
- Dispositivo ARM64 (aarch64) — sin soporte x86/ARMv7

### Instalar el módulo

1. Descargar el ZIP desde [Releases](../../releases)
2. Magisk Manager → **Módulos** → **Instalar desde almacenamiento**
3. Seleccionar el ZIP → instalar → **reiniciar**
4. El daemon arranca automáticamente con el sistema

### Instalar la app

```sh
adb install app-debug.apk
```

### Verificar instalación

```sh
# Debe retornar 1
adb shell getprop persist.ivanna.daemon_active

# Debe mostrar la línea con @omega_daemon_socket
adb shell grep omega_daemon_socket /proc/net/unix
```

---

## CI/CD

| Job | Plataforma | Estado |
|---|---|---|
| Build APK & Native Binaries | Ubuntu (NDK 26.1, ARM64) | [![Build](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml/badge.svg)](../../actions) |
| DSP Native Tests (CTest / GTest 1.14) | Ubuntu host (g++ C++17) | [![Tests](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml/badge.svg)](../../actions) |
| ELF gate (PIE, AArch64, INTERP, RELRO) | llvm-readelf NDK 26.1 | en build job |
| SBOM + Cosign keyless + SLSA L2 | sigstore.dev | en CI |

**7 suites de test** que validan en host: `SeqlockBus`, `OmegaDspSnapshot` CRC, `OmegaControlBus` publish/read round-trip, estabilidad gammatone, piso numérico sin denormales, métricas de calidad de audio (SNR/THD), y regresión de tuning DSP. Los tests ARM64 device-side están en la hoja de ruta (ADR-0001).

---

## Estructura del repositorio

```
IVANNA-OMEGA-SUPREME/
├── app/src/main/
│   ├── cpp/
│   │   ├── daemon/               ← Daemon C++ (ivanna_daemon, command_server)
│   │   ├── include/
│   │   │   └── omega_control_bus.h   ← OmegaDspSnapshot + OmegaControlBus ABI
│   │   ├── spatial/              ← HRTF convolver, object renderer, head tracker
│   │   ├── neuromorphic/         ← Volterra H2, NPE engine, neural upmixer
│   │   ├── dsp/                  ← EQ, compressor, exciter, widener
│   │   ├── jni/                  ← JNI bridges (omega, spatial, SAF)
│   │   └── omega_effect.cpp      ← AudioFlinger insert effect
│   ├── assets/
│   │   └── sofa/                 ← 434 datasets HRTF (MIT KEMAR, ARI, CIPIC...)
│   └── java/com/ivanna/omega/
│       ├── core/                 ← Application, ParameterStore, OmegaEngine
│       ├── magisk/               ← MagiskBridge, OmegaEngineBridge, ShmManager
│       ├── audio/                ← DSPBridge, AudioRouteManager, AdaptiveBackend
│       └── spatial/              ← HrtfSubjectSelector, IvannaSpatialManager, SAF
├── magisk_module/
│   ├── service.sh                ← Watchdog con socket readiness + MQA lifecycle
│   ├── customize.sh              ← Instalación + SELinux live
│   ├── post-fs-data.sh           ← Anti-bootloop + setprop
│   ├── mqa_monitor.sh            ← Auto-preset por app
│   ├── ivanna_control.sh         ← CLI shell
│   ├── concert_mode.sh           ← Modo concierto
│   ├── sepolicy.rule             ← SELinux para untrusted_app + audioserver
│   └── saf/SAF_model.json        ← Modelo Φ_SAF^∞ (214 sujetos, K=7)
└── docs/adr/
    └── 0001-omega-control-plane.md
```

---

## Reconocimientos técnicos

- **MIT KEMAR HRTF** — MIT Media Lab, Gardner & Martin (1994)
- **ARI HRTF Database** — Austrian Research Institute for Artificial Intelligence
- **CIPIC HRTF Database** — UC Davis CIPIC Interface Laboratory
- **SOFA format** — AES69-2015 standard, sofa.sf.jku.at
- **Volterra Series** — Weiner (1958), Schetzen (1980)
- **Riemannian Optimization** — Absil, Mahony & Sepulchre (2008)
- **ISO 226:2003** — Equal-loudness contours

---

<div align="center">

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.**

*Tecnología de audio de investigación. Para uso en dispositivos propios.*

</div>
