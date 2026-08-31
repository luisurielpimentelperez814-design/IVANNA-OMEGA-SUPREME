<div align="center">

# ⬡ IVANNA OMEGA SUPREME

### El motor de audio Android de grado OEM++ — DSP nativo C++17/NEON, IA adaptativa en tiempo real, y espacialización binaural medida

[![Build](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=github&label=BUILD&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Android](https://img.shields.io/badge/Android-10%20%E2%86%92%2015-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com)
[![Module](https://img.shields.io/badge/Magisk%20Module-v2.3.0-FF3E86?style=for-the-badge&logo=magisk)](magisk_module/)
[![DSP](https://img.shields.io/badge/DSP-C%2B%2B17%20%C2%B7%20NEON%20ARM64-6FF3FF?style=for-the-badge)](app/src/main/cpp/)
[![Tests](https://img.shields.io/badge/CTest-27%2F27%20verdes-F7B733?style=for-the-badge)](app/src/main/cpp/tests/)
[![License](https://img.shields.io/badge/%C2%A9-2026%20GORE%20TNS-57708F?style=for-the-badge)](LICENSE)

**No es un ecualizador. Es un motor de audio de sistema completo.**

</div>

---

## ✦ ¿Qué es IVANNA?

IVANNA intercepta **cada muestra de audio** que produce tu dispositivo — Spotify, YouTube, juegos, llamadas, todo — y la procesa con una cadena DSP nativa escrita en C++17 optimizado a NEON ARM64, adaptada en tiempo real por un motor de decisión que escucha lo que suena y decide cómo debe sonar.

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
│  │  global, Magisk) │   Bus 512 B    │  publish @ ~cada comando  │   │
│  └────────┬─────────┘                └────────────┬──────────────┘   │
│           │ IvannaFusionCore × sesión             │ Unix socket      │
│           │ processStereo() → RIR → SAF → Limiter │ @omega_daemon    │
│           ▼                                       ▼                  │
│     Audífonos / Altavoz ◄──────────── libivanna_omega.so (JNI, app)  │
│                                      EQ→Comp→Exciter→Widener→PD→Gain │
│                                      →SafetyLimiter (−0.1 dBFS)      │
└──────────────────────────────────────────────────────────────────────┘
```

- **Ruta A (en proceso):** la app corre la cadena DSP completa sobre su propio reproductor y sobre la captura MediaProjection. Latencia ~2.8 ms medida en LAB.
- **Ruta B (system-wide):** `libomega_effect.so` vive dentro de `audioserver` como GlobalEffect Magisk; una instancia `IvannaFusionCore` **por sesión de audio** (aislamiento auditado y reparado), controlada cross-process vía `OmegaControlBus` — seqlock sobre SHM, 512 bytes, lock-free en el callback de audio, con CRC32 y versioning de ABI.

---

## ✦ La cadena DSP — cada etapa auditada

| # | Etapa | Archivo | Qué hace | Defensas de producción |
|---|-------|---------|----------|------------------------|
| 1 | Pre-EQ peak guard | `ivanna_omega_jni.cpp` | Headroom antes del EQ | −1 dBFS preventivo |
| 2 | ParametricEQ | `dsp/ParametricEQ.cpp` | 10 bandas biquad RBJ | Crossfade anti-zipper 15 ms · **compensación de headroom por stack de bandas** aplicada al master en los 4 puntos de `setParams` |
| 3 | Compressor | `dsp/Compressor.cpp` | RMS + sidechain HPF | Envolvente suavizada, sin escalones |
| 4 | HarmonicExciter | `dsp/HarmonicExciter.cpp` | Saturación Padé + 2ª/3ª armónica | **Oversampling 2× + LPF 14.5 kHz** · clamp Padé ±3 · techo interno `excScale_` con ataque inmediato / release 20 ms · bypass **bit-exacto** a wet=0 |
| 5 | StereoWidener | `dsp/StereoWidener.cpp` | M/S imaging | Clamp de correlación |
| 6 | PDEngine | `pd_engine.hpp` | NHO + BiquadEnvelopeBank + CueBasedSpatial | Motor no-lineal con inhibición lateral |
| 7 | GainStage | `dsp/GainStage.cpp` | Trim de salida suavizado | One-pole por muestra, sin doble limitación |
| 8 | SafetyLimiter | `dsp/SafetyLimiter.cpp` | Techo −0.1 dBFS | Soft-knee real (threshold −4 dBFS) · ataque/release recalculados por SR de sesión (8k–384k) |

> **Cero asignaciones en el hot path.** Los buffers L/R de la Ruta B se preasignan en `EFFECT_CMD_SET_CONFIG` (8 192 frames, passthrough defensivo si el bloque excede). El callback nunca llama a `malloc`, nunca toma locks pesados, nunca parsea JSON.

---

## ✦ Espacialización — datos medidos, no sintetizados

| Dataset | Contenido real shippeado | Formato | Dónde vive |
|---------|--------------------------|---------|------------|
| **HRTF** | 12 datasets IHR1 (KEMAR large/normal pinna, TU-Berlin, CIPIC, Pulse…) | `.ihr1` propio (binario, guard de integridad) | `magisk_module/…/hrtf/` |
| **SOFA** | 39 archivos AES69 (MIT KEMAR, CIPIC, GeneralTF…) | `.sofa` estándar | `magisk_module/…/sofa/` |
| **RIR** | **200 salas medidas** reales | WAV PCM + `metadata.csv` con RT60 | `magisk_module/…/rir/` |
| **SAF** | Modelo total de personalización | `SAF_model_total.json` | `magisk_module/…/` |

**Cadena espacial:** `ObjectRenderer` (12 altavoces virtuales en dodecaedro) → `HRTFConvolver` por speaker con **IDW bilineal** → crossfade morfológico conducido por el vector latente `q[7]` del optimizador Φ_SAF^∞ → `RirConvolver` (overlap-save FFT Radix-2, real-time safe) → selección de sala por RT60 real desde el daemon (`SET_ROOM_RT60` / `GET_ROOM_STATUS`).

**Φ_SAF^∞ cross-process (ABI v2):** el optimizador publica el morph vector completo `saf_q[7]` en el `OmegaDspSnapshot`; la Ruta B lo entrega a `setLatentParams(q)` por bloque. La personalización HRTF ya no se queda en la app: llega al audio de todo el sistema.

---

## ✦ Inteligencia — tres cerebros, un lazo

```
 audio crudo ──► RawMetricsBus ──► AdaptiveDecisionEngine ──► AdaptiveState
 (Ruta A y B)      (SPSC lock-free)   (controlLoop @ 50 ms)     (seqlock)
                                          │
        ┌─────────────────────────────────┼──────────────────────────┐
        ▼                                 ▼                          ▼
  TinyML ConvNeXt INT8            CMA-ES evolutivo (512 bandas)  ISO 226:2003
  4 clases · softmax              256 taps FIR · smooth phase     29 frecuencias
  escena: voz/música/trans./amb.  fitness psicoacústico           equal-loudness
```

- **AdaptiveDecisionEngine:** publica `target_gain`, `comp_amount`, `exciter_reduction`, `spatial_width`, `voice_protection`, `safety_margin` — consumidos por **ambas rutas** (paridad auditada: antes las decisiones de Spotify nunca volvían a Spotify).
- **Fatigue Mitigator:** ajusta el high-cut IIR del daemon en sesiones largas (1er orden, 16–19.5 kHz).
- **Persistencia total:** cada control (TinyML, CMA-ES σ, Fatigue, perfiles, spatial) sobrevive cierres y reboots vía `PersistedStateRestorer` + stores dedicados.

---

## ✦ IVANNA PERSONAL ACOUSTIC INTELLIGENCE ENGINE

**Dolby entiende contenido. Apple entiende dispositivo. Sony entiende espacio. IVANNA entiende al oyente.**

No es un asistente genérico con un plugin de audio encima: es una capa de inteligencia acústica personal, construida enteramente sobre el motor DSP propio descrito arriba — sin Gemini, sin ningún servicio de IA externo. ASR, TTS, razonamiento y memoria corren en el dispositivo, sobre datos que nunca salen de él.

```
 Voz / texto del usuario
        │
        ▼
 SpeechInputProvider (ASR modular — intercambiable sin tocar el núcleo)
        │
        ▼
 IvannaLanguageCore ─────────── lenguaje natural → intención acústica estructurada
        │                        + comprensión de frases musicales ("más alma",
        │                          "que respire", "más pegada", "sentir el escenario",
        │                          "como disco de colección")
        ▼
 IvannaMusicalIntentEngine ──── motor de comprensión musical especializado
        │                        + 12 presets canónicos (Épico, Abbey Road, Vinilo,
        │                          Cinematográfico, Analógico, Estudio Pro, etc.)
        │                        + detección de intenciones combinadas ("épico + estadio")
        │                        + traducción directa a parámetros IvannaEffectProfile
        ▼
 IvannaCognitiveCore ─────────── razona la intención contra el estado real del DSP
        │                        (clipping, térmico, escena) y decide: ejecutar / adaptar / rechazar
        ▼
 IvannaDSPOrchestrator ─────── aplica el preset coordinadamente:
        │                        1. IvannaGlobalEffectManager.applyProfile() — EQ/bass/virt/comp
        │                        2. VoiceController.executeCommand() — concert_mode, spatial, etc.
        │                        3. IvannaConversationalCore.recordAdjustment() — memoria de sesión
        ▼
 IvannaConversationalCore ───── memoria conversacional de sesión:
        │                        · canción actual y artista (encadenamiento: "hazla más épica")
        │                        · último preset aplicado y últimos cambios DSP
        │                        · preferencias temporales del usuario ("no me gustan los bajos")
        │                        · historial de ajustes para el reporte hablado
        ▼
 IvannaAcousticBrain ─────────── fusiona percepción + salud + IvannaListenerProfile +
        │                         duración real de sesión en recomendación explicable
        ▼
 IvannaAgentCore ─────────────── cinco agentes especializados a cadencia ~1 Hz
        ▼
 OmegaEngineBridge → daemon / DSPBridge → cadena DSP nativa (ver arriba)
        │
        ▼
 IvannaVoiceEngine ───────────── responde con prosodia adaptada a la intención:
                                  · SIMPLE: confirmación directa ("He subido el volumen.")
                                  · MUSICAL: ritmo levemente más lento, énfasis descriptivo
                                    ("He creado una configuración épica para Frankenstein:
                                     abrí la escena estéreo, reforcé el impacto de batería
                                     y conservé la energía original de la grabación.")
                                  · TECHNICAL: cadencia reducida para claridad en parámetros
                                  · segmentación natural por frases para pausas reales entre ideas
```

- **Musical Intent Engine.** `IvannaMusicalIntentEngine` convierte lenguaje musical humano a parámetros DSP medibles sin LLM externo. Reconoce frases naturales ("más alma", "que respire", "más pegada", "sentir el escenario", "como disco de colección") además de nombres canónicos (Abbey Road, vinilo, épico). Detecta intenciones combinadas: "Frankenstein suena brutal pero quiero más escenario" activa ÉPICO + CONCIERTO MASIVO como intenciones encadenadas.
- **Conversational Memory.** `IvannaConversationalCore` mantiene en RAM el contexto completo de la sesión: canción actual, artista, último preset aplicado, lista de cambios DSP recientes y preferencias temporales del usuario ("no me gustan los bajos muy fuertes"). Permite encadenar órdenes sin repetir contexto: "pon Frankenstein de Edgar Winter" → "ahora hazla como si estuviera en un estadio" → IVANNA sabe que sigue hablando de la misma canción.
- **DSP Orchestration Layer.** `IvannaDSPOrchestrator` aplica un `MusicalPreset` sobre los tres canales coordinados: perfil EQ/bass/virtualizer/compresor vía `IvannaGlobalEffectManager`, comando extra vía `VoiceController` (concert_mode, spatial, etc.), y registro inmediato en el núcleo conversacional para los reportes. Nunca toca el hilo de audio directamente; orquesta los canales existentes en el orden correcto.
- **Voice Intelligence.** `IvannaLanguageCore` comprende frases de estado emocional ("más feeling", "que emocione"), preferencias de espacio ("que respire"), ritmo ("más pegada", "que mueva"), separación instrumental ("que se distingan") y referencias de colección ("como disco de colección"). Todas se traducen a parámetros DSP reales, sin fabricar APIs ni presets vacíos.
- **Natural Prosody Engine.** `IvannaVoiceEngine` adapta la prosodia de cada respuesta a su complejidad: confirmaciones simples son directas y rápidas; descripciones de configuraciones musicales suenan más lentas y enfáticas para que el usuario procese los detalles técnicos. La segmentación de frases genera pausas reales entre ideas en respuestas largas — no el flujo continuo artificial del TTS estándar.
- **Agentes acústicos, no reglas sueltas.** Percepción de escena (voz/música/dinámico/silencio) desde telemetría real, política de decisión por escena y por salud, aplicación por los mismos canales seguros que ya usa el DSP, monitoreo de salud y optimización automática reversible — todo con ring buffer de decisiones para explicabilidad.
- **TinyML propio.** La clasificación de escena y contenido corre en el SoC (ConvNeXt INT8, 4 clases), no en la nube — la misma filosofía que la clasificación de audio de la Ruta A/B.
- **HRTF / SOFA / SAF / RIR al servicio del oyente.** El `IvannaAudioKnowledgeBase` conoce el mismo vocabulario técnico que describe la cadena espacial (CIPIC, AES69, overlap-save FFT, RT60 medido) y lo usa para explicar decisiones en lenguaje humano, no solo para procesarlas.
- **Listener Intelligence.** `IvannaListenerProfile` aprende localmente (SharedPreferences, nunca la nube): modo preferido, ancho espacial, sensibilidad a fatiga, comandos frecuentes — y se lo dice al usuario ("tu ajuste habitual es…") en vez de tratarlo como usuario nuevo cada vez.
- **Memoria acústica, con límites por diseño.** `IvannaContextMemory` + `IvannaListenerProfile` + `IvannaAcousticBrain` guardan preferencias, historial de ajustes y contexto de sesión — nunca audio ni transcripciones largas. `clearMemory()` borra las tres en cascada sin tocar la configuración DSP permanente.
- **Decisiones explicables, no una caja negra.** Cada acción queda en el `DecisionRecord` ring buffer de `IvannaAgentCore` con su razón; `explainLastDecision()` la traduce a lenguaje humano combinando ese historial con el `IvannaAudioKnowledgeBase`. Ejemplo real de lo que puede decir IVANNA sin que se lo pidan: *"Reduje la agresividad de agudos porque detecté fatiga auditiva después de 40 minutos de escucha."*
- **Voz propia, arquitectura intercambiable.** `SpeechInputProvider` (ASR) e `IvannaVoiceEngine` (TTS) son interfaces modulares: el motor de voz se puede cambiar sin tocar `IvannaLanguageCore`, `IvannaCognitiveCore` ni `IvannaAcousticBrain`. La identidad de IVANNA — su personalidad, su vocabulario acústico, su forma de razonar — no depende del proveedor de voz.

---

## ✦ Daemon & IPC — el plano de control

| Pieza | Detalle |
|-------|---------|
| `ivanna_daemon` | PIE + RELRO + BIND_NOW + `-static-libstdc++` (arranca como root desde Magisk sin depender de libs del APK) · SCHED_FIFO 98 |
| Socket | Unix abstracto `@omega_daemon_socket` — JSON con respuestas ricas (`applied` / `accepted_pending_consumer` / generation) |
| SHM | `OmegaControlBus` en `/data/adb/ivanna_omega/omega_control_snapshot` — seqlock embebido, MAGIC + VERSION + CRC32 |
| Route Arbiter | `OFF / IN_PROCESS / SYSTEM_WIDE` explícito en cada snapshot |
| Telemetría B→A | `raw_rms`, `raw_peak`, `effect_frames` escritos por audioserver y leídos por la app — la UI sabe cuándo la Ruta B está viva |

---

## ✦ UI — instrumento de precisión

5 pestañas (CONTROL · BRAIN · ADAPTIVE · SPATIAL · SYSTEM) con tema **Aurora Obsidiana** propio, sparklines RMS en vivo, visualizador FFT de 64 bandas Bark reales, LAB de medición (THD / IMD / LUFS BS.1770-4 / SNR / True Peak), panel NEON profiler, calibración ISO 226 aplicable a EQ + DSP + daemon en un solo toque, y estados honestos: si un dataset no está desplegado, la UI lo dice en vez de simular.

---

## ✦ Calidad verificada — no declarada

- **27 tests CTest** en CI host: barrido completo del exciter (11×11×6 señales de peor caso, peak ≤ 1.0), bypass bit-exacto, stress del bus de control 15 s, estabilidad del motor adaptativo, dataset RIR real contra los 200 WAV shippeados, métricas de calidad de audio, sin denormals.
- **CI de artefactos con verificación de integridad:** el build falla si el daemon no es ARM64/PIE/RELRO/BIND_NOW, si el zip Magisk carece de `system/bin/ivanna_daemon` o `sepolicy.rule`, o si los binarios no coinciden.
- **Historial de auditoría:** 200+ commits de reparación quirúrgica — Use-After-Free del Engine (shared_ptr atómico), aislamiento DSP por sesión AudioFlinger, eliminación de alloc en realtime, lifecycle del fusion core, JNI signatures, STL estática del daemon, crossfade EQ, headroom, bypass exacto. Cada fix: un commit, un push.

---

## ✦ Instalación

**Requisitos:** Android 10+ · Magisk o KernelSU (Ruta B) · ARM64.

1. Descarga el artefacto `ivanna-magisk-module` del último CI verde → contiene `ivanna_omega_supreme.zip` (módulo) **y el APK**.
2. Flashea el zip en Magisk/KSU → reinicia.
3. Instala el APK → abre IVANNA → concede permisos de captura si quieres Ruta A sobre otras apps.

La app y el módulo van a la par: **v2.3.0 / 2300** en ambos.

---

## ✦ Lo que IVANNA no hace (honestidad de ingeniería)

- Sin root, la Ruta B no existe: la app cae a `AudioEffect` por sesión (EQ/DynamicsProcessing de Android) — el DSP profundo custom requiere el módulo.
- Los datasets SOFA/RIR ocupan espacio real en `/system/etc/ivanna_omega/` (montaje Magisk, sin tocar la partición).
- El PMU no es accesible en la mayoría de SoCs de consumo: el throughput GFLOPS se reporta como `N/M` en vez de inventarse.

---

<div align="center">

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.**

*Construido muestra a muestra. Auditado commit a commit.*

**⬡ IVANNA OMEGA SUPREME ⬡**

</div>
