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

### El motor de inteligencia de audio para Android — DSP nativo C++17/NEON, IA adaptativa en tiempo real, espacialización binaural con datos medidos y asistente cognitivo con Gemini 2.5

<br>

[![Build](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=github&label=BUILD&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Android](https://img.shields.io/badge/Android-9%20%E2%86%92%2015-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com)
[![Module](https://img.shields.io/badge/Magisk%20Module-v2.3.2-FF3E86?style=for-the-badge&logo=magisk)](magisk_module/)
[![DSP](https://img.shields.io/badge/DSP-C%2B%2B17%20%C2%B7%20NEON%20ARM64-6FF3FF?style=for-the-badge)](app/src/main/cpp/)
[![Kotlin](https://img.shields.io/badge/UI-Kotlin%20%C2%B7%20Jetpack%20Compose-A97FFF?style=for-the-badge&logo=kotlin)](app/src/main/java/)
[![Gemini](https://img.shields.io/badge/Asistente-Gemini%202.5%20Flash-8E75FF?style=for-the-badge&logo=googlegemini)](app/src/main/java/com/ivanna/omega/ai/gemini/)
[![Supply Chain](https://img.shields.io/badge/SLSA-SBOM%20%C2%B7%20Cosign-F7B733?style=for-the-badge&logo=slsa)](.github/workflows/supply-chain.yml)

<br>

**No es un ecualizador. Es un motor de audio de sistema completo,**

**con cerebro propio y voz propia.**

</div>

---

## ✦ ¿Qué es IVANNA?

IVANNA intercepta **cada muestra de audio** que produce tu dispositivo — Spotify, YouTube, juegos, llamadas, todo — y la procesa con una cadena DSP nativa escrita en **C++17 optimizado a NEON ARM64**, adaptada en tiempo real por un motor de decisión que escucha lo que suena y decide cómo debe sonar. Y si le hablas, te responde.

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
│  │  global, Magisk) │   Bus 512 B    │  Unix socket + TCP fallback│  │
│  └────────┬─────────┘                └────────────┬──────────────┘   │
│           │ IvannaFusionCore × sesión             │ Unix socket      │
│           │ processStereo() → RIR → SAF → Limiter │ + 127.0.0.1:12121│
│           ▼                                       ▼                  │
│     Audífonos / Altavoz ◄──────────── libivanna_omega.so (JNI, app)  │
│                                      EQ→Comp→Exciter→Widener→PD→Gain │
│                                      →SafetyLimiter (−0.1 dBFS)      │
└──────────────────────────────────────────────────────────────────────┘
```

- **Ruta A (en proceso):** la app corre la cadena DSP completa sobre su propio reproductor y sobre la captura MediaProjection. Latencia de milisegundos, medida en el LAB integrado.
- **Ruta B (system-wide):** `libomega_effect.so` vive dentro de `audioserver` como GlobalEffect Magisk; una instancia `IvannaFusionCore` **por sesión de audio**, controlada cross-process vía `OmegaControlBus` — seqlock sobre memoria compartida de 512 bytes, lock-free en el callback de audio, con MAGIC + VERSION + CRC32.
- **Doble vía de control al daemon:** socket Unix abstracto `@omega_daemon_socket` (primario) **con fallback TCP loopback `127.0.0.1:12121`** — si SELinux o la ROM bloquean el socket abstracto, la app conecta por TCP. La política SELinux se reaplica en cada boot desde `service.sh` (antes solo se aplicaba en instalación y se perdía al reiniciar).

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

**Anti-artefactos auditados en producción:** cambio de sala RIR con crossfade en frecuencia (~43 ms, la cola vieja muere sola en vez de cortarse en seco), carga de IR fuera del hot path (worker de control con condition variable — la lectura de WAV de disco no ocurre nunca en el callback de audio), mezcla de protección de voz con EMA por muestra, y la alocación del DSP por instancia de sesión (sin estado global compartido entre sesiones de AudioFlinger).

---

## ✦ Espacialización — datos medidos, no sintetizados

| Dataset | Contenido real shippeado | Formato | Dónde vive |
|---------|--------------------------|---------|------------|
| **HRTF** | 12 datasets IHR1 (KEMAR large/normal pinna, TU-Berlin, CIPIC, Pulse…) | `.ihr1` propio (binario con guard de integridad) | `magisk_module/…/hrtf/` |
| **SOFA** | 216 archivos AES69 (MIT KEMAR, CIPIC, GeneralTF, ARI HpIR de auriculares) | `.sofa` estándar — firma HDF5 verificada | `app/src/main/assets/` + `magisk_module/…/sofa/` |
| **RIR** | **200 salas medidas reales** (RIR_Local) | WAV PCM 16-bit + `metadata.csv` con RT60 | `magisk_module/…/rir/` |
| **SAF** | Modelo total de personalización (Φ_SAF∞) | `SAF_model_total.json` | `magisk_module/…/` |

- **Selector de sujeto HRTF por antropometría:** mides tu oreja (concha / hélix / fosa triangular en mm) y `HrtfSubjectSelector` hace matching 1-NN euclídeo normalizado contra la tabla CIPIC de 214 sujetos — eliges la HRTF de la persona cuya anatomía más se parece a la tuya.
- **AutoEq de auriculares con mediciones reales:** 23+ perfiles (Sennheiser HD650, Beyerdynamic DT770 Pro…) extraídos de los HpIR SOFA medidos — FFT del impulso → respuesta promediada → compensación con target Harman, no inversión a plano.
- **Cambio de sujeto en caliente:** sin reiniciar el audio, con liberación de memoria del dataset anterior.

---

## ✦ Inteligencia — los cerebros que escuchan

La capa de decisión opera sobre **mediciones reales del contenido**, no sobre heurísticas fijas:

| Subsistema | Motor | Qué decide |
|------------|-------|-----------|
| **AdaptiveDecisionEngine** (C++, hilo de control 50 ms) | crest factor, margen de headroom al limiter, EMA de sibilancia, `voice_score` real del clasificador | target_gain, compresión, reducción de exciter, ancho espacial — con convergencia suavizada (sin escalones audibles) |
| **Kernel Evolutivo** (`evolutionary_kernel.cpp`) | población de 128 genomas × 256 genes, elitismo ordenado por fitness, crossover + mutación | ajusta NHO y parámetros espaciales contra una función de fitness acoplada al audio real (loudness/transientes/espacialidad del contenido que suena) |
| **PsychoacousticAnalyzer** | FFT 1024 real + 24 bandas críticas Bark + K-weighting IIR (BS.1770) | espectro real del contenido (no simulado), umbral de enmascaramiento por spreading function entre bandas, sonoridad LUFS verdadera |
| **Clasificador CRNN** (`AntiDolbyCrnnClassifier`) | TFLite, log-mel 32×40 @ 16 kHz, EMA temporal por clase | voz / música / bajos / silencio + detección de transientes por onset — alimenta la protección de voz y el motor de género |
| **QLearning** (`HybridDecisionEngine`) | bandido contextual ε-greedy sobre estado (emoción × fatiga) | aprende qué ajustes prefiere el usuario en cada contexto emocional |
| **LearningBias** | EMA del delta (usuario − autónomo) por (contexto, parámetro) | el sistema aprende tus correcciones manuales y las aplica la próxima vez — persistido en JSON-lines auditable |
| **CMA-ES** | optimización evolutiva psicoacústica ISO 226 | calibración del perfil auditivo personal |

**Fatiga auditiva real:** modelo de dosis acumulada (OMS/ITU) sobre el tiempo de escucha y nivel — atenúa agudos gradualmente tras exposición prolongada, con rampa suave y recuperación.

---

## ✦ IVANNA Assistant — el motor habla, ahora con Gemini 2.5

Un asistente cognitivo integrado en la app, con núcleo conversacional propio **más el respaldo de Gemini 2.5 Flash** cuando hay red:

- **Gemini 2.5 Flash** (`ai/gemini/IvannaGeminiAgent` + `GeminiOrchestrator`): LLM en la nube con instrucción de sistema experta en DSP. Detección de red automática (WiFi / datos celulares) — cae al motor offline sin red. La API key se ingresa en la pantalla del asistente (panel con botón **PROBAR CONEXIÓN**), se persiste cifrada por `SecureConfigurationManager`, y puede inyectarse en build vía `BuildConfig.GEMINI_API_KEY` desde CI. **Nunca viaja hardcodeada en el binario.**
- **Motor offline siempre disponible:** sin key o sin red, el agente agéntico local responde y ejecuta los mismos comandos DSP.
- **Comandos de voz → DSP real:** "dame más aire", "que la voz no fatigue", "modo concierto" → `[CMD:...]` parseados a parámetros nativos reales (EQ, compresión, RIR, SAF, ancho espacial).
- **Reconocimiento de voz** on-device + TTS en la nube opcional.
- **Memoria episódica y semántica** (`IvannaContextMemory`, `IvannaSuperAgentMemory`, `MemoryRetrievalEngine`): aprende tus ajustes por escena y los recuerda entre sesiones.
- **Self-Healing Agent:** detecta estados degradados del pipeline y propone recuperación (con guardia contra bucles de re-reparación).

---

## ✦ Motores de fase y dinámica no-lineal

- **Phase Oracle (Pi-LSTM):** `phase_oracle.cpp` — red recurrente que predice la evolución de fase de la señal para anticipar la decisión del DSP en vez de reaccionar a ella.
- **Motor coclear Volterra H2:** `IvannaNpeEngine` — modelado no-lineal de la cóclea (memoria de Volterra de 2º orden) con upsampling polifásico, compresión OHC e inhibición lateral.
- **Neuromorphic Processing Engine (NPE):** spike-based con detección de género (`nativeGetDetectedGenre`) y firma espectral por bandas (`nativeGetSynthSignature`) — expone clasificación al sistema adaptativo.
- **OmegaVibratoryProcessor:** modelado físico de la respuesta vibratoria del transductor.

---

## ✦ Daemon & IPC — el plano de control

| Pieza | Detalle |
|-------|---------|
| `ivanna_daemon` | PIE + RELRO + BIND_NOW + `-static-libstdc++` (arranca como root desde Magisk sin depender de libs del APK) · SCHED_FIFO 98 · anclado al cluster LITTLE en big.LITTLE |
| Socket primario | Unix abstracto `@omega_daemon_socket` — JSON con respuestas ricas (`applied` / `accepted_pending_consumer` / generation), framing por balance de llaves, un hilo por conexión |
| Socket control | `@omega_command_socket` — canal de comandos dedicado |
| **Fallback TCP** | `127.0.0.1:12121` (loopback) — la app conecta por aquí si el socket abstracto está bloqueado por SELinux/ROM |
| SHM | `OmegaControlBus` en `/data/adb/ivanna_omega/omega_control_snapshot` — seqlock embebido, MAGIC + VERSION + CRC32 |
| Route Arbiter | `OFF / IN_PROCESS / SYSTEM_WIDE` explícito en cada snapshot |
| Telemetría B→A | `raw_rms`, `raw_peak`, `effect_frames` escritos por audioserver y leídos por la app — la UI sabe cuándo la Ruta B está viva |
| ThermalGovernor | 5 niveles de degradación elegante: reduce orden Ambisonics / longitud RIR ante throttling térmico |
| Offloading | Selector Hexagon DSP / FastRPC (Snapdragon) con fallback NEON/CPU instantáneo |
| SELinux | `sepolicy.rule` (278 reglas) aplicada en instalación **y reaplicada en cada boot** desde `service.sh` — el socket sobrevive reinicios |

---

## ✦ UI — instrumento de precisión

42+ pantallas Jetpack Compose con tema propio **Aurora Obsidiana**, organizadas en pestañas (CONTROL · BRAIN · ADAPTIVE · SPATIAL · SYSTEM) más la suite OEM:

- **Sparklines RMS en vivo** y visualizador FFT de 64 bandas Bark reales (`Bark64VisualizerPanel`, `FftOscilloscopePanel`).
- **Ivanna LAB:** medición THD / IMD / LUFS BS.1770-4 / SNR / True Peak con barrido automatizado.
- **NEON Profiler** y panel de benchmarks on-device.
- **Calibración ISO 226** aplicable a EQ + DSP + daemon en un solo toque.
- **Panel SOFA·AF·RIR·SAF** (`SofaAfRirSafPanelScreen`): control unificado de sujeto HRTF, sala, y optimizador latente con telemetría en vivo.
- **Panel de conexión Gemini** con estado en vivo y prueba de conectividad.
- **Test ABX** integrado para comparación ciega de presets.
- **Paneles OEM:** acústica, IA, espacial, telemetría y térmico — grado de diagnóstico de fábrica.
- **Estados honestos:** si un dataset no está desplegado o un motor está offline, la UI lo dice en vez de simular.

---

## ✦ Calidad verificada — no declarada

- **Suite CTest nativa (host):** barrido completo del exciter con señales de peor caso (peak ≤ 1.0, wet=0 bit-exacto), stress del bus de control 15 s, estabilidad del motor adaptativo, dataset RIR validado contra los 200 WAV shippeados, métricas de calidad de audio, cero denormals.
- **CI de artefactos con verificación de integridad:** el build falla si el daemon no es ARM64/PIE/RELRO/BIND_NOW, si el zip Magisk carece de `system/bin/ivanna_daemon` o `sepolicy.rule`, o si los binarios no coinciden.
- **Integridad de datasets:** los `.sofa` se validan por firma HDF5 (`894844460d0a1a0a`) en build — los 216 archivos del árbol SAF fueron reemplazados por copias verificadas tras detectarse corrupción UTF-8 en la importación original (protegido con `.gitattributes` binary).
- **Supply chain:** workflow dedicado con SBOM, firma Cosign keyless y attestations SLSA en cada tag `v*`.
- **Versionado unificado:** `version.properties` es la fuente única de verdad; el build **falla** si `module.prop` diverge de él.
- **Historial de auditoría:** 250+ commits de reparación quirúrgica — Use-After-Free del Engine, aislamiento DSP por sesión AudioFlinger, eliminación de alloc en realtime, lifecycle del fusion core, JNI signatures, STL estática del daemon, crossfade EQ, headroom, bypass exacto, race UAF en NPE, trust region del optimizador SAF, espectro Bark real. Cada fix: un commit, un push.

---

## ✦ El ecosistema completo

| Componente | Stack | Función |
|------------|-------|---------|
| **App Android** | Kotlin · Jetpack Compose (200 archivos, ~40k LOC) | UI, Ruta A, asistente Gemini, LAB de medición |
| **DSP nativo** | C++17 · NEON ARM64 (248 archivos, ~59k LOC) | Cadena de efectos, clasificador, convolución, motores de decisión |
| **Módulo Magisk** | Shell · sepolicy (278 reglas) | Ruta B system-wide, daemon root, datasets (12 IHR1 + 200 RIR + SOFA + SAF) |
| **Panel web** | React 19 · Vite · Tailwind 4 | Consola de visualización y export de parámetros |

---

## ✦ Instalación

**Requisitos:** Android 9+ (minSdk 28) · ARM64 (armeabi-v7a incluido como fallback) · Magisk o KernelSU para la Ruta B.

1. Descarga el artefacto `ivanna-magisk-module` del último CI verde → contiene `ivanna_omega_supreme.zip` (módulo) **y el APK**.
2. Flashea el zip en Magisk/KSU → reinicia.
3. Instala el APK → abre IVANNA → concede permisos de captura si quieres Ruta A sobre otras apps.
4. **(Opcional)** Para activar el asistente con Gemini 2.5: pega tu API key en el panel del asistente → toca **PROBAR CONEXIÓN**. Sin key, el asistente funciona con su motor offline completo.

La app y el módulo van a la par: **v2.3.2 / 2302** en ambos — garantizado por el Unified Version Manager.

---

## ✦ Lo que IVANNA no hace (honestidad de ingeniería)

- **Sin root, la Ruta B no existe:** la app cae a `AudioEffect` por sesión (EQ/DynamicsProcessing de Android) — el DSP profundo custom requiere el módulo.
- Los datasets SOFA/RIR ocupan espacio real en `/system/etc/ivanna_omega/` (montaje Magisk, sin tocar la partición).
- El PMU no es accesible en la mayoría de SoCs de consumo: el throughput GFLOPS se reporta como `N/M` en vez de inventarse.
- El asistente Gemini requiere que el usuario ingrese su propia API key — nunca viaja dentro del binario. Sin red (WiFi o datos) cae al motor offline sin degradación de la cadena DSP.
- El cambio de sujeto HRTF aplica crossfade (~43 ms) para no cortar la cola de reverberación — no es instantáneo a propósito.

---

<div align="center">

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.**

*Construido muestra a muestra. Auditado commit a commit.*

**⬡ IVANNA OMEGA SUPREME ⬡**

</div>
