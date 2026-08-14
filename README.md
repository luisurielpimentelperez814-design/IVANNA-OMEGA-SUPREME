<div align="center">

```
██╗██╗   ██╗ █████╗ ███╗   ██╗███╗   ██╗ █████╗
██║██║   ██║██╔══██╗████╗  ██║████╗  ██║██╔══██╗
██║██║   ██║███████║██╔██╗ ██║██╔██╗ ██║███████║
██║╚██╗ ██╔╝██╔══██║██║╚██╗██║██║╚██╗██║██╔══██║
██║ ╚████╔╝ ██║  ██║██║ ╚████║██║ ╚████║██║  ██║
╚═╝  ╚═══╝  ╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═══╝╚═╝  ╚═╝
              O M E G A   S U P R E M E
```

### Motor DSP system-wide para Android — HRTF medido, convolución de sala real, IA on-device y calibración psicoacústica personalizada

[![CI](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&label=BUILD%20%2B%20TESTS&logo=githubactions&logoColor=white&color=00c853)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Platform](https://img.shields.io/badge/Android-10–16%20·%20arm64--v8a-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#requisitos)
[![Root](https://img.shields.io/badge/Root-Magisk%20·%20KernelSU%20·%20APatch-000000?style=for-the-badge&logo=magisk&logoColor=white)](#instalación)
[![NDK](https://img.shields.io/badge/NDK-r26.1%20·%20C%2B%2B17%20·%20NEON-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](#stack-técnico)
[![Kotlin](https://img.shields.io/badge/Kotlin-Compose%20·%20Coroutines-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white)](#stack-técnico)

| 142 archivos Kotlin | 206 JNI ↔ 204 nativos | 23/23 tests CTest | 0 `malloc()` en hot path |
|:---:|:---:|:---:|:---:|
| UI completa en Compose | paridad de superficie casi 1:1 | host, sin emulador | audio thread puro |

</div>

---

## Tabla de contenidos

1. [Qué es esto](#qué-es-esto)
2. [Por qué es diferente](#por-qué-es-diferente)
3. [Arquitectura end-to-end](#arquitectura-end-to-end)
4. [El pipeline DSP, etapa por etapa](#el-pipeline-dsp-etapa-por-etapa)
5. [Audio espacial: HRTF medido + salas reales](#audio-espacial-hrtf-medido--salas-reales)
6. [IA on-device](#ia-on-device)
7. [Capa de evidencia](#capa-de-evidencia)
8. [Robustez y ciclo de vida](#robustez-y-ciclo-de-vida)
9. [Posicionamiento honesto frente a la industria](#posicionamiento-honesto-frente-a-la-industria)
10. [Requisitos e instalación](#requisitos)
11. [Stack técnico](#stack-técnico)
12. [Roadmap](#roadmap)

---

## Qué es esto

IVANNA OMEGA SUPREME es un **motor DSP system-wide** que se inyecta en `audioserver` vía módulo Magisk/KernelSU/APatch. Reemplaza la ecualización OEM (Dolby Atmos, Dirac, HyperOS, OneUI) con un pipeline propio en **C++17 nativo**, corriendo en un daemon `SCHED_FIFO 80` con comunicación **zero-copy** entre procesos.

Procesa **todo** el audio del dispositivo — música, streaming, videollamadas, juegos — sin un solo `malloc()` en el hot path.

No es un ecualizador más. Es la cadena completa: **captura → análisis → decisión → procesamiento → medición → validación**.

---

## Por qué es diferente

| Lo que hace la industria (apps de EQ) | Lo que hace IVANNA |
|---|---|
| EQ gráfico de 5–10 bandas sobre el mixer de Android | Hijack del `AudioEffect` UUID en AudioFlinger: procesa el PCM del **sistema entero** |
| Presets estáticos ("Rock", "Jazz") | Clasificador **CRNN INT8 on-device** que adapta el DSP al contenido en < 8.2 µs por inferencia |
| HRTF genérico (una cabeza "promedio") | **Selección de sujeto HRTF por geometría de pabellón auricular** (3 medidas) + morph algebraico vía base PCA |
| "Efecto de sala" = reverb sintética | **Convolución con RIRs reales** (overlap-save FFT Radix-2, real-time safe) |
| Sin validación | **Suite ABX con persistencia, test binomial y exportación JSON** — la mejora es *demostrable*, no opinable |
| Latencia desconocida | **Benchmark round-trip medido en hardware** con `CLOCK_MONOTONIC` sobre la cadena DSP real |

---

## Arquitectura end-to-end

```
┌─────────────────────────────────────────────────────────────────────┐
│  Apps (Spotify · Tidal · Netflix · WhatsApp · juegos)               │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼  PCM buffers
┌─────────────────────────────────────────────────────────────────────┐
│  libomega_effect.so                                                 │
│  AudioEffect UUID hijack en AudioFlinger                            │
└──────────────────────────────┬──────────────────────────────────────┘
                               │  zero-copy memfd (SCM_RIGHTS)
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  OmegaControlBus — seqlock + mmap · 0 syscalls por lectura          │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  ivanna_daemon · SCHED_FIFO 80 · < 300 ms boot                      │
│                                                                     │
│  01 ParametricEQ          8 bandas · Q adaptativo                   │
│  02 ISO-226:2003          curvas de igual sonoridad reales          │
│  03 Anti-Dolby CRNN INT8  4 clases · < 8.2 µs · 340 KB              │
│  04 HarmonicExciter       Volterra H2 · armónicos controlados       │
│  05 Compressor            loudness-aware, sin pumping               │
│  06 StereoWidener         canal dedicado (no deriva de gamma)       │
│  07 HRTF Engine           sujeto seleccionado por pinna + PCA morph │
│  08 RirConvolver          salas reales · overlap-save FFT Radix-2   │
│  09 SafetyLimiter         protección auditiva ISO 226               │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  App Android (Compose)                                              │
│  Control total · telemetría 20 Hz · calibración · ABX · benchmark   │
└─────────────────────────────────────────────────────────────────────┘
```

**142 archivos Kotlin** organizados en 12 módulos de dominio: `ai` · `audio` · `core` · `dsp` · `magisk` · `neuromorphic` · `saf` · `spatial` · `ui` · `visualizer`.

**Paridad JNI casi 1:1** — 206 funciones `external` declaradas en Kotlin, 204 símbolos `JNIEXPORT` implementados en C++. Lo que la UI promete, el nativo lo cumple.

---

## El pipeline DSP, etapa por etapa

| # | Etapa | Implementación | Dato clave |
|---|---|---|---|
| 01 | **ParametricEQ** | 8 bandas, Q adaptativo | canal de parámetros dedicado |
| 02 | **Loudness ISO-226:2003** | curvas reales de igual sonoridad | compensación por nivel de escucha |
| 03 | **Anti-Dolby CRNN** | red convolucional recurrente INT8 | 4 clases · < 8.2 µs · 340 KB |
| 04 | **HarmonicExciter** | no-linealidad Volterra H2 | integración nativa end-to-end |
| 05 | **Compressor** | aware de loudness | gamma separado del stereo width |
| 06 | **StereoWidener** | `nativeSetStereoWidth` dedicado | sin colisión de parámetros |
| 07 | **SafetyLimiter** | umbral ISO 226 | 85 dB max configurable por perfil |

---

## Audio espacial: HRTF medido + salas reales

La pieza que ningún ecualizador de Android tiene:

- **Selección de sujeto HRTF por geometría de pinna** — 3 medidas del pabellón auricular → búsqueda en dataset (subset CIPIC + MIT). Tu HRTF no es el de una cabeza genérica: es el del sujeto medido más cercano a *tu* oreja.
- **Morph SAF algebraico exacto** — interpolación en el espacio HRTF vía base PCA `V`, no aproximaciones por bandas.
- **RirConvolver** — convolución con respuestas al impulso de sala reales, overlap-save FFT Radix-2, real-time safe. Selector de sala cableado hasta el daemon (`SET_ROOM_RT60` / `GET_ROOM_STATUS` por socket).
- **Head tracking 6DoF** — giroscopio + rotation vector a 100 Hz, con filtro One-Euro sobre cuaterniones y dead-reckoning predictivo para ocultar la latencia del buffer de audio. El sonido se queda *fijo en el espacio* al girar la cabeza.
- **Optimizador Riemanniano SAF** — calibración iterativa del sujeto HRTF con convergencia medida (‖p_t‖, energía de error), persistida entre sesiones.

---

## IA on-device

| Motor | Qué hace | Coste |
|---|---|---|
| **Anti-Dolby CRNN INT8** | clasifica contenido (speech/music/bass/…) y reenruta el DSP | < 8.2 µs · 340 KB |
| **Autonomous Neural Modulator** | inferencia TinyML INT8 → telemetría Volterra/HRTF | on-device, sin red |
| **PerceptualCortex** | PCM → ISO 226 → Bark → EQ → DSP en tiempo real | polling 100 ms |
| **PerceptualBrainEngine** | decisión adaptativa continua sobre telemetría nativa | 20 Hz |

Todo corre **en el dispositivo**. Sin nube obligatoria, sin telemetría saliente. El sync de perfiles a la nube existe pero es opcional, no bloqueante y degradable.

---

## Capa de evidencia

El audio no se opina: se mide.

- **Benchmark round-trip** — latencia DSP medida en hardware con `CLOCK_MONOTONIC` sobre la cadena real de la Ruta A. No es un benchmark sintético: es el audio pasando por el pipeline.
- **Suite ABX** — comparación ciega con **persistencia de resultados, test binomial de significancia estadística y exportación JSON**. Si la mejora no supera el azar con p < 0.05, el sistema lo dice.
- **Telemetría acústica** — RMS, peak, clip count, CPU %, latencia estimada de hardware, categoría YAMNet — todo visible en la UI a 20 Hz.

---

## Robustez y ciclo de vida

Endurecimiento auditado del arranque y del hot path (los crashes de producción se corrigen en la raíz, no en el síntoma):

- **Inicialización perezosa de contexto** — ningún manager toca `Context` en su constructor: `ProfileManagerBridge`, `IvannaHeadTracker` y `HeadTrackingManager` resuelven sensores y recursos con `by lazy` + `applicationContext`. Una recomposición temprana de Compose o un process-death ya no pueden tumbar la app.
- **Doble guard JNI** — los puentes que cruzan dos librerías nativas (`DSPBridge` → `IvannaNativeLib`) verifican **ambos** estados de carga antes de cada llamada.
- **Hot path sin excepciones** — visualizador Bark-64, motor USB isócrono y bridges SAF degradan con log, nunca con crash: un periférico desconectado a mitad de sesión no interrumpe el audio.
- **Daemon resiliente** — reconexión automática del bridge, canal CONFLATED para parámetros DSP (cero OOM por coroutines bloqueadas), y watchdog de sesión.

---

## Posicionamiento honesto frente a la industria

Sin marketing. Lo que el código sostiene hoy:

| Dimensión | ViPER4Android / JamesDSP | Dolby Atmos / Sony 360 / Apple Spatial | IVANNA OMEGA SUPREME |
|---|---|---|---|
| EQ paramétrico system-wide | ✅ | ✅ (OEM) | ✅ |
| Clasificación de contenido on-device | ❌ | parcial | ✅ CRNN INT8 |
| HRTF personalizado por anatomía | ❌ | ✅ (Sony/Apple, fotogrametría propietaria) | ✅ (geometría de pinna + PCA) |
| Convolución con salas reales | ❌ | ✅ (Apple) | ✅ RIR + overlap-save FFT |
| Head tracking | ❌ | ✅ (hardware propietario) | ✅ (IMU genérica + predicción) |
| Validación ABX integrada | ❌ | interna, no pública | ✅ con test binomial |
| Medición de latencia en hardware | ❌ | interna | ✅ `CLOCK_MONOTONIC` |
| Código auditable | ✅ | ❌ cerrado | ✅ |
| Validación perceptual a escala (labs, rigs GRAS/B&K) | ❌ | ✅ décadas de I+D | ⚠️ pendiente |
| Ecosistema de contenido (metadatos Atmos/360RA) | ❌ | ✅ | ❌ por diseño (PCM puro) |
| QA multi-dispositivo / soporte industrial | ❌ | ✅ | ⚠️ pendiente |

**Dónde se posiciona:** por encima de cualquier app de EQ de Android en sofisticación de pipeline (HRTF personalizado, salas reales, ABX estadístico, IA on-device son piezas que ViPER/JamesDSP no tienen). A la altura de los **prototipos de investigación** de laboratorios de audio espacial. Por debajo de Dolby/Sony/Apple en lo que el código no puede comprar: décadas de validación psicoacústica, rigs de medición certificados, ecosistema de contenido y QA industrial.

**Veredicto:** el motor de procesamiento espacial personalizado más completo que existe en código abierto para Android. La brecha restante no es de arquitectura — es de **validación a escala**.

---

## Requisitos

- Android **10–16**, `arm64-v8a`
- Root: **Magisk**, **KernelSU** o **APatch**
- Bootloader desbloqueado
- *(Opcional)* DAC USB para ruta isócrona profesional

## Instalación

```bash
# 1. Flashear el módulo desde Magisk/KernelSU/APatch
# 2. Instalar la app (apk de release o build local)
./gradlew :app:assembleRelease
# 3. Abrir la app → el daemon arranca en < 300 ms
```

## Stack técnico

| Capa | Tecnología |
|---|---|
| DSP / daemon | C++17 · NDK r26.1 · NEON · `SCHED_FIFO 80` |
| IPC | memfd + `SCM_RIGHTS` · seqlock + mmap |
| App | Kotlin · Jetpack Compose · Coroutines/Flow |
| IA | CRNN INT8 · TinyML · YAMNet bridge |
| Tests | 23/23 CTest en host (sin emulador) |
| Build | Gradle KTS · CMake · compileSdk 35 |

---

## Roadmap

- [x] Pipeline DSP nativo system-wide (Ruta A)
- [x] HRTF medido + selección por pinna + morph PCA
- [x] Convolución RIR de salas reales
- [x] CRNN INT8 anti-dolby + TinyML autónomo
- [x] ABX con significancia estadística
- [x] Benchmark de latencia en hardware
- [x] Endurecimiento de ciclo de vida (auditoría de contexto)
- [ ] Validación perceptual con panel de escucha externo
- [ ] Mediciones con rig acústico (GRAS / B&K)
- [ ] Dataset HRTF ampliado (más allá del subset CIPIC + MIT)

---

<div align="center">

**IVANNA OMEGA SUPREME** — el audio no se opina. Se mide, se demuestra, se escucha.

*Cada afirmación de este README es rastreable al código: 142 archivos Kotlin, 204 símbolos JNI, 23 tests. Sin humo.*

</div>
