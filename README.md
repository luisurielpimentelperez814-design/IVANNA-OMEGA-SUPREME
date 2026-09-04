<div align="center">

```
                    ██████╗  ██████╗ ███╗   ███╗███████╗ ██████╗  █████╗
                   ██╔═══██╗██╔════╝ ████╗ ████║██╔════╝██╔════╝ ██╔══██╗
                   ██║   ██║██║  ███╗██╔████╔██║█████╗  ██║  ███╗███████║
                   ██║   ██║██║   ██║██║╚██╔╝██║██╔══╝  ██║   ██║██╔══██║
                   ╚██████╔╝╚██████╔╝██║ ╚═╝ ██║███████╗╚██████╔╝██║  ██║
                    ╚═════╝  ╚═════╝ ╚═╝     ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝
```

# ⬡ IVANNA OMEGA SUPREME

### El motor de inteligencia de audio para Android — DSP nativo C++17/NEON, IA adaptativa en tiempo real, espacialización binaural con datos medidos y asistente cognitivo integrado

<br>

[![Build](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=github&label=BUILD&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Android](https://img.shields.io/badge/Android-9%20%E2%86%92%2015-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com)
[![Module](https://img.shields.io/badge/Magisk%20Module-v2.3.0-FF3E86?style=for-the-badge&logo=magisk)](magisk_module/)
[![DSP](https://img.shields.io/badge/DSP-C%2B%2B17%20%C2%B7%20NEON%20ARM64-6FF3FF?style=for-the-badge)](app/src/main/cpp/)
[![Kotlin](https://img.shields.io/badge/UI-Kotlin%20%C2%B7%20Jetpack%20Compose-A97FFF?style=for-the-badge&logo=kotlin)](app/src/main/java/)
[![Supply Chain](https://img.shields.io/badge/SLSA-SBOM%20%C2%B7%20Cosign-F7B733?style=for-the-badge&logo=slsa)](.github/workflows/supply-chain.yml)

<br>

**No es un ecualizador. Es un motor de audio de sistema completo,**

**con cerebro propio.**

</div>

---

## ✦ ¿Qué es IVANNA?

IVANNA intercepta **cada muestra de audio** que produce tu dispositivo — Spotify, YouTube, juegos, llamadas, todo — y la procesa con una cadena DSP nativa escrita en **C++17 optimizado a NEON ARM64**, adaptada en tiempo real por un motor de decisión que escucha lo que suena y decide cómo debe sonar.

Dos rutas de procesamiento, un solo cerebro:

```
┌──────────────────────────────────────────────────────────────────────┐
│                         TU DISPOSITIVO                               │
│                                                                      │
│   Spotify · YouTube · Juegos · Sistema              IVANNA App       │
│        │                                          (Ruta A, 48 kHz)   │
│        │ AudioFlinger                                   │            │
│        ▼                                                ▼            │
│  ┌──────────────────┐  SHM seqlock   ┌───────────────────────────┐   │
│  │ omega_effect.so  │◄──────────────►│  ivanna_daemon (root, RT) │   │
│  │ (Ruta B, sistema │   OmegaControl │  command_server · SHM mgr │   │
│  │  global, Magisk) │   Bus 512 B    │  Unix socket + telemetría │   │
│  └────────┬─────────┘                └────────────┬──────────────┘   │
│           │ IvannaFusionCore × sesión             │ Unix socket      │
│           │ processStereo() → RIR → SAF → Limiter │ @omega_daemon    │
│           ▼                                       ▼                  │
│     Audífonos / Altavoz ◄──────────── libivanna_omega.so (JNI, app)  │
│                                      EQ→Comp→Exciter→Widener→PD→Gain │
│                                      →SafetyLimiter (−0.1 dBFS)      │
└──────────────────────────────────────────────────────────────────────┘
```

- **Ruta A (en proceso):** la app corre la cadena DSP completa sobre su propio reproductor y sobre la captura MediaProjection. Latencia de milisegundos, medida en el LAB integrado.
- **Ruta B (system-wide):** `libomega_effect.so` vive dentro de `audioserver` como GlobalEffect Magisk; una instancia `IvannaFusionCore` **por sesión de audio**, controlada cross-process vía `OmegaControlBus` — seqlock sobre memoria compartida de 512 bytes, lock-free en el callback de audio, con MAGIC + VERSION + CRC32.

---

## ✦ La cadena DSP — ocho etapas, cada una defendida

| # | Etapa | Archivo | Qué hace | Defensas de producción |
|---|-------|---------|----------|------------------------|
| 1 | Pre-EQ peak guard | `ivanna_omega_jni.cpp` | Headroom antes del EQ | −1 dBFS preventivo |
| 2 | ParametricEQ | `dsp/ParametricEQ.cpp` | 10 bandas biquad RBJ | Crossfade anti-zipper 15 ms · compensación de headroom por stack de bandas |
| 3 | Compressor | `dsp/Compressor.cpp` | RMS + sidechain HPF | Envolvente suavizada, sin escalones |
| 4 | HarmonicExciter | `dsp/HarmonicExciter.cpp` | Saturación Padé + 2ª/3ª armónica | Oversampling 2× + LPF 14.5 kHz · clamp Padé ±3 · bypass bit-exacto a wet=0 |
| 5 | StereoWidener | `dsp/StereoWidener.cpp` | M/S imaging | Clamp de correlación |
| 6 | PDEngine | `pd_engine.hpp` | NHO + BiquadEnvelopeBank + CueBasedSpatial | Motor no-lineal con inhibición lateral |
| 7 | GainStage | `dsp/GainStage.cpp` | Trim de salida suavizado | One-pole por muestra, sin doble limitación |
| 8 | SafetyLimiter | `dsp/SafetyLimiter.cpp` | Techo −0.1 dBFS | Soft-knee real · ataque/release recalculados por sample rate de sesión (8k–384k) |

> **Cero asignaciones en el hot path.** Los buffers L/R de la Ruta B se preasignan en `EFFECT_CMD_SET_CONFIG` (8 192 frames, passthrough defensivo si el bloque excede). El callback nunca llama a `malloc`, nunca toma locks pesados, nunca parsea JSON.

---

## ✦ Espacialización — datos medidos, no sintetizados

| Dataset | Contenido real shippeado | Formato | Dónde vive |
|---------|--------------------------|---------|------------|
| **HRTF** | 12 datasets IHR1 (KEMAR large/normal pinna, TU-Berlin, CIPIC, Pulse…) | `.ihr1` propio (binario con guard de integridad) | `magisk_module/…/hrtf/` |
| **SOFA** | 39 archivos AES69 (MIT KEMAR, CIPIC, GeneralTF…) | `.sofa` estándar | `magisk_module/…/sofa/` |
| **RIR** | **200 salas medidas reales** | WAV PCM + `metadata.csv` con RT60 | `magisk_module/…/rir/` |
| **SAF** | Modelo total de personalización | `SAF_model_total.json` | `magisk_module/…/` |

**Cadena espacial:** `ObjectRenderer` (12 altavoces virtuales en dodecaedro) → `HRTFConvolver` por speaker con **IDW bilineal** → crossfade morfológico conducido por el vector latente `q[7]` del optimizador Φ_SAF^∞ → `RirConvolver` (overlap-save FFT Radix-2, real-time safe) → selección de sala por RT60 real desde el daemon (`SET_ROOM_RT60` / `GET_ROOM_STATUS`).

**Φ_SAF^∞ cross-process:** el optimizador publica el morph vector completo `saf_q[7]` en el `OmegaDspSnapshot`; la Ruta B lo entrega a `setLatentParams(q)` por bloque. La personalización HRTF no se queda en la app: llega al audio de todo el sistema.

---

## ✦ Inteligencia — tres cerebros, un lazo

```
 audio crudo ──► RawMetricsBus ──► AdaptiveDecisionEngine ──► AdaptiveState
 (Ruta A y B)      (SPSC lock-free)   (controlLoop @ 50 ms)     (seqlock)
                                          │
        ┌─────────────────────────────────┼──────────────────────────┐
        ▼                                 ▼                          ▼
  Clasificador TinyML INT8          CMA-ES evolutivo             ISO 226:2003
  escena: voz/música/               512 bandas · 256 taps FIR    29 frecuencias
  transitorio/ambiente              fitness psicoacústico        equal-loudness
```

- **AdaptiveDecisionEngine:** publica `target_gain`, `comp_amount`, `exciter_reduction`, `spatial_width`, `voice_protection`, `safety_margin` — consumidos por **ambas rutas** con paridad.
- **EvolutionaryEQ (CMA-ES):** ecualización evolutiva con fitness psicoacústico; σ persiste entre sesiones.
- **Fatigue Mitigator:** ajusta el high-cut IIR del daemon en sesiones largas (1er orden, 16–19.5 kHz) para proteger la escucha prolongada.
- **Motor perceptual:** `PerceptualBrainEngine` + `IvannaVoiceProsodyEngine` modulan la experiencia según contenido y contexto.
- **Persistencia total:** cada control (TinyML, CMA-ES σ, Fatigue, perfiles, spatial) sobrevive cierres y reboots vía `PersistedStateRestorer` + stores dedicados.

---

## ✦ IVANNA Assistant — el motor habla

Un asistente cognitivo integrado en la app, con núcleo conversacional propio y respaldo de Gemini (la API key se ingresa en Ajustes, nunca se hardcodea en el build):

- **Reconocimiento de voz** on-device (`IvannaSpeechRecognizer`) + TTS en la nube opcional.
- **Intent Mapper musical:** "dame más aire", "que la voz no fatigue", "modo concierto" → traducidos a parámetros DSP reales.
- **Memoria episódica y semántica** (`IvannaContextMemory`, `IvannaSuperAgentMemory`): aprende tus ajustes por escena y los recuerda.
- **Self-Healing Agent:** detecta estados degradados del pipeline y propone recuperación.

---

## ✦ Daemon & IPC — el plano de control

| Pieza | Detalle |
|-------|---------|
| `ivanna_daemon` | PIE + RELRO + BIND_NOW + `-static-libstdc++` (arranca como root desde Magisk sin depender de libs del APK) · SCHED_FIFO 98 |
| Socket | Unix abstracto `@omega_daemon_socket` — JSON con respuestas ricas (`applied` / `accepted_pending_consumer` / generation) |
| SHM | `OmegaControlBus` en `/data/adb/ivanna_omega/omega_control_snapshot` — seqlock embebido, MAGIC + VERSION + CRC32 |
| Route Arbiter | `OFF / IN_PROCESS / SYSTEM_WIDE` explícito en cada snapshot |
| Telemetría B→A | `raw_rms`, `raw_peak`, `effect_frames` escritos por audioserver y leídos por la app — la UI sabe cuándo la Ruta B está viva |
| ThermalGovernor | 5 niveles de degradación elegante: reduce orden Ambisonics / longitud RIR ante throttling térmico |
| Offloading | Selector Hexagon DSP / FastRPC (Snapdragon) con fallback NEON/CPU instantáneo |

---

## ✦ UI — instrumento de precisión

54 pantallas Jetpack Compose con tema propio **Aurora Obsidiana**, organizadas en pestañas (CONTROL · BRAIN · ADAPTIVE · SPATIAL · SYSTEM) más la suite OEM:

- **Sparklines RMS en vivo** y visualizador FFT de 64 bandas Bark reales (`Bark64VisualizerPanel`, `FftOscilloscopePanel`).
- **Ivanna LAB:** medición THD / IMD / LUFS BS.1770-4 / SNR / True Peak con barrido automatizado.
- **NEON Profiler** y panel de benchmarks on-device.
- **Calibración ISO 226** aplicable a EQ + DSP + daemon en un solo toque.
- **Paneles OEM:** acústica, IA, espacial, telemetría y térmico — grado de diagnóstico de fábrica.
- **Estados honestos:** si un dataset no está desplegado, la UI lo dice en vez de simular.

---

## ✦ Calidad verificada — no declarada

- **Suite CTest nativa (host):** barrido completo del exciter con señales de peor caso (peak ≤ 1.0), bypass bit-exacto, stress del bus de control 15 s, estabilidad del motor adaptativo, dataset RIR validado contra los 200 WAV shippeados, métricas de calidad de audio, cero denormals.
- **CI de artefactos con verificación de integridad:** el build falla si el daemon no es ARM64/PIE/RELRO/BIND_NOW, si el zip Magisk carece de `system/bin/ivanna_daemon` o `sepolicy.rule`, o si los binarios no coinciden.
- **Supply chain:** workflow dedicado con SBOM, firma Cosign keyless y attestations SLSA en cada tag `v*`.
- **Versionado unificado:** `version.properties` es la fuente única de verdad; el build **falla** si `module.prop` diverge de él.
- **Historial de auditoría:** 200+ commits de reparación quirúrgica — Use-After-Free del Engine, aislamiento DSP por sesión AudioFlinger, eliminación de alloc en realtime, lifecycle del fusion core, JNI signatures, STL estática del daemon, crossfade EQ, headroom, bypass exacto. Cada fix: un commit, un push.

---

## ✦ El ecosistema completo

| Componente | Stack | Función |
|------------|-------|---------|
| **App Android** | Kotlin · Jetpack Compose (~40k LOC) | UI, Ruta A, asistente, LAB de medición |
| **DSP nativo** | C++17 · NEON ARM64 (~59k LOC) | Cadena de efectos, clasificador, convolución |
| **Módulo Magisk** | Shell · sepolicy (278 reglas) | Ruta B system-wide, daemon root, datasets |
| **Panel web** | React 19 · Vite · Tailwind 4 | Consola de visualización y export de parámetros |

---

## ✦ Instalación

**Requisitos:** Android 9+ (minSdk 28) · ARM64 (armeabi-v7a incluido como fallback) · Magisk o KernelSU para la Ruta B.

1. Descarga el artefacto `ivanna-magisk-module` del último CI verde → contiene `ivanna_omega_supreme.zip` (módulo) **y el APK**.
2. Flashea el zip en Magisk/KSU → reinicia.
3. Instala el APK → abre IVANNA → concede permisos de captura si quieres Ruta A sobre otras apps.

La app y el módulo van a la par: **v2.3.0 / 2300** en ambos — garantizado por el Unified Version Manager.

---

## ✦ Lo que IVANNA no hace (honestidad de ingeniería)

- **Sin root, la Ruta B no existe:** la app cae a `AudioEffect` por sesión (EQ/DynamicsProcessing de Android) — el DSP profundo custom requiere el módulo.
- Los datasets SOFA/RIR ocupan espacio real en `/system/etc/ivanna_omega/` (montaje Magisk, sin tocar la partición).
- El PMU no es accesible en la mayoría de SoCs de consumo: el throughput GFLOPS se reporta como `N/M` en vez de inventarse.
- El asistente Gemini requiere que el usuario ingrese su propia API key en Ajustes — nunca viaja dentro del binario.

---

<div align="center">

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.**

*Construido muestra a muestra. Auditado commit a commit.*

**⬡ IVANNA OMEGA SUPREME ⬡**

</div>
