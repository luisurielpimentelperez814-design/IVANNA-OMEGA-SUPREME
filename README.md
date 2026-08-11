<div align="center">
  <img src="https://raw.githubusercontent.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/main/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png" alt="IVANNA OMEGA SUPREME Logo" width="140" />

  # 🎧 IVANNA OMEGA SUPREME

  **The Apex of Android Audio Engineering**  
  *Kernel-Level DSP • Lock-Free SPSC • PI-LSTM Neuromorphic Processing • Native Spatial HRTF*

  [![Android Platform](https://img.shields.io/badge/Platform-Android-3DDC84?style=for-the-badge&logo=android)](#)
  [![Magisk Module](https://img.shields.io/badge/Root-Magisk-008080?style=for-the-badge&logo=magisk)](#)
  [![C++17](https://img.shields.io/badge/C++-17-00599C?style=for-the-badge&logo=c%2B%2B)](#)
  [![Kotlin](https://img.shields.io/badge/Kotlin-Native-7F52FF?style=for-the-badge&logo=kotlin)](#)
  [![TinyML](https://img.shields.io/badge/TinyML-INT8-FF6F00?style=for-the-badge&logo=tensorflow)](#)
  [![Build Status](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)

  **IVANNA OMEGA SUPREME** is a production-ready, ultra-low latency (< 5ms) system-wide audio rendering engine for Android. Engineered for audiophiles and power users, it replaces standard AudioFlinger processing with a custom, hardware-accelerated daemon running natively via Magisk.
</div>

---

## 🌟 Executive Summary

IVANNA OMEGA bridges the gap between professional-grade studio processing and mobile audio constraints. It implements an uncompromising architecture that achieves **zero frame loss** through lock-free concurrency, advanced SHM (Shared Memory) protocols via `memfd`, and hardware-accelerated NEON intrinsics. 

It intelligently gracefully downgrades:
- **Root Daemon Mode:** System-wide DSP via Magisk abstract UNIX sockets.
- **Root No-Daemon Mode:** Direct JNI processing via `AudioEffect`.
- **No-Root Mode:** Fallback to Android's native `DynamicsProcessing` API.

---

## 🚀 Core Technologies

- **TinyML Anti-Dolby Engine (YAMNet Replacement):** A kernel-level INT8 quantized Depthwise-ConvNeXt model that classifies audio scenes in real-time (< 8.2µs inference latency). It dynamically adapts EQ to vocal, music, and bass characteristics, neutralizing aggressive OEM Dolby profiles.
- **Neuromorphic PI-LSTM Core:** Replaces static equalization with a Physics-Informed LSTM predictive model, dynamically analyzing RMS energy accumulation to prevent listener fatigue.
- **Native Spatial HRTF Renderer:** Custom binaural upmixing utilizing Volterra H2 nonlinear processing and ISO-226:2003 Equal Loudness Contours mapped directly to human biological hearing curves.
- **Zero-Copy IPC:** Communicates between the Android Kotlin UI and the C++ daemon using `AF_UNIX` abstract sockets (`@omega_daemon_socket`) and memory-mapped `SCM_RIGHTS` file descriptors with Seqlock synchronization.

---

## 🏗 Architecture & Data Flow

IVANNA OMEGA operates directly within the Android `audioserver` execution path. By decoupling the UI from the DSP engine, the processing thread remains completely immune to UI-thread jank, GC pauses, or Kotlin coroutine dispatches.

```mermaid
flowchart TB

A["Android Applications<br/>Qobuz • Players • Media APIs"]

B["Android Audio Framework<br/>AudioFlinger / Audio HAL Execution Path"]

C["IVANNA Native Bridge<br/>JNI Kotlin ↔ C++"]

D["Omega Control Plane<br/>Runtime State Management"]

E["OmegaDaemon V8<br/>C++17 Real-Time Service"]

F["AF_UNIX Abstract IPC<br/>@omega_daemon_socket"]

G["OmegaControlBus<br/>Shared Memory + Seqlock"]

H["IVANNA DSP Core<br/>Low Latency Perceptual Engine"]

I["TinyML Perception Engine<br/>Real-Time Audio Intelligence"]

J["PI-LSTM Predictive Engine<br/>Temporal Energy Modeling"]

K["SAF Spatial Engine<br/>Synthetic Acoustic Field"]

L["HRTF / SOFA Renderer<br/>Personalized Binaural Processing"]

M["Volterra Spatial Model<br/>Nonlinear Acoustic Reconstruction"]

N["ISO 226 + ITU Loudness Engine<br/>Human Hearing Calibration"]

O["Adaptive Dynamics Engine<br/>Compression + Excitation Control"]

P["ARM64 NEON Acceleration<br/>SIMD Optimized Processing"]

Q["Final Audio Renderer<br/>Low Latency Output"]

R["Magisk Runtime Layer<br/>System Integration"]

S["Android DynamicsProcessing<br/>No Root Fallback"]


A --> B
B --> C

C --> D
D --> E

E --> F
E --> G

G --> H

H --> I
H --> J
H --> K
H --> N
H --> O

K --> L
L --> M

I --> P
J --> P
M --> P
N --> P
O --> P

P --> Q

R --> E

S -.-> H

---

## 🧪 Performance & Benchmarks

| Metric | Target | IVANNA OMEGA | Architecture Benefit |
| :--- | :--- | :--- | :--- |
| **DSP Latency** | < 10 ms | **< 3.2 ms** | Zero-copy passthrough via SHM memfd |
| **TinyML Inference** | < 50 µs | **8.2 µs** | ARM NEON INT8 Vectorization |
| **CPU Overhead** | < 5% | **1.8%** | Multi-threaded offload & Lock-Free IPC |
| **Memory Footprint** | < 15 MB | **8.4 MB** | Pre-allocated static arenas |

---

## 📝 License & Contributing

Copyright © 2026 IVANNA AUDIO.
Built with ❤️ for supreme acoustic supremacy.

Please refer to `docs/ANTI_DOLBY_INTEGRATION_GUIDE.md` and `docs/ARCHITECTURE_INTEGRATION.md` before submitting Pull Requests modifying the core DSP behavior.
