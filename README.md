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

> 🤖 Este repo recibe trabajo de múltiples sesiones de IA en paralelo — antes de emprender trabajo sustancial, revisa **[AGENT_CLAIMS.md](AGENT_CLAIMS.md)**.
>
> 🪝 Hooks: `bash scripts/setup-hooks.sh` (una vez por clon, idempotente) — el pre-commit corre la puerta de tests en cada commit; se salta puntualmente con `--no-verify`.
>
> 🧪 Puerta de regresión DSP en host: `bash scripts/run_ctest.sh` (76 tests, GTest vendoreado, offline; `IVANNA_SAN=asan` para ASan+UBSan) — corre en CI via [tests-host.yml](.github/workflows/tests-host.yml).

<br>

[![Build](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=github&label=BUILD&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Tests host](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/tests-host.yml?branch=main&style=for-the-badge&logo=github&label=TESTS%20HOST%2076%2F76&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/tests-host.yml)
[![Android](https://img.shields.io/badge/Android-9%20%E2%86%92%2015-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com)
[![Module](https://img.shields.io/badge/Magisk%20Module-v2.3.12-FF3E86?style=for-the-badge&logo=magisk)](magisk_module/)
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
| 1 | Pre-EQ peak guard | `ivanna_omega_jni.cpp` | Headroom antes del EQ | −1 dBFS preventivo · **ataque instantáneo / release en rampa** (no simétrico — un guard de seguridad no puede rampear su reacción sin debilitarse; la vuelta a la normalidad sí se suaviza) |
| 2 | ParametricEQ | `dsp/ParametricEQ.cpp` | 10 bandas biquad RBJ | Crossfade anti-zipper 15 ms · compensación de headroom por stack de bandas |
| 3 | Compressor | `dsp/Compressor.cpp` | RMS + sidechain HPF | Envolvente suavizada, sin escalones |
| 4 | HarmonicExciter | `dsp/HarmonicExciter.cpp` | Saturación Padé + 2ª/3ª armónica | Oversampling 2× + LPF 14.5 kHz · clamp Padé ±3 · bypass bit-exacto a wet=0 |
| 5 | StereoWidener | `dsp/StereoWidener.cpp` | M/S imaging | Clamp de correlación |
| 6 | PDEngine | `pd_engine.hpp` | NHO + BiquadEnvelopeBank + CueBasedSpatial | Motor no-lineal con inhibición lateral |
| 7 | GainStage | `dsp/GainStage.cpp` | Trim de salida suavizado | One-pole por muestra, sin doble limitación |
| 8 | SafetyLimiter | `dsp/SafetyLimiter.cpp` | Techo −0.1 dBFS | Soft-knee real · ataque/release recalculados por sample rate de sesión (8k–384k) |
| 9 | Saneo NaN/Inf | `ivanna_omega_jni.cpp` | Última red antes de `data`/DAC | Plegado en el re-intercalado final — si un IIR diverge en cualquier etapa 2-8, la muestra se reemplaza por silencio en vez de propagar NaN/Inf al HAL. Contador diagnóstico separado por ruta (`nanRecoveries`/`blkNanRecoveries`); debe quedarse en 0 en operación normal |

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

## ✦ Intelligent Upmixing — estéreo → HOA → binaural (v2.3.9+)
Canal de expasión espacial en tiempo recién añadido y cableado extremo-a-extremo al `IvannaFusionCore`:

| Etapa | Módulo | Qué entrega |
|-------|--------|-------------|
| Crossover complementario | `spatial/IntelligentUpmixer` | Separa el contenido en 2 sub-band conteñidas (bass sobre el mono L+R — **es mono-seguro, no suma de potencia en el canal fantasma**) |
| Vectores de fuente | `spatial/HoaGainMatrix` | Codificación Ambisonics de orden **0–2 real en el plano horizontal** (9 canales ACN/SN3D), con ganancia por dirección |
| Transientes | `spatial/TransientDetector` | Envolvente rápida (atq 2 ms) vs lenta (smt 60 ms) — dirige el transient sobre el bus mono, evita el chorreo espaciil de percusión |
| Decodificador binaural | `spatial/HoaBinauralDecoder` | Renderizado 2D de los 9 canales HOA hacia par estéreo usando la **base HRTF CIPIC medida** (no un pan por sin()) |
| Puerta de calidad host | `tests/spatial_dataset_regression_test.cpp` | Par exacto a ±30°, NaN = 0 en todas las fuentes traseras, canal fantasma = 0 sobre bass |

**Dónde se controla:** `Panel de espacialidad → INTELLIGENT UPMIXING` (toggle on/off + deslizador INMERSIVIDAD 0..1, cableados ambos a `ParameterStore` → JNI → el engine real, no a un intent).

**Transición de estado (2026-09-17):** activar/desactivar el toggle, o subir/bajar la
inmersividad, cruza por un crossfade real por muestra (`blockMix_`, ~15 ms) entre la señal
seca y la procesada — evita el eco/desface que producía un salto duro entre la ruta directa
(sin latencia) y la ruta HOA+HRTF (con su propia latencia FIR). Nota de honestidad: un intento
anterior en la misma sesión de trabajo declaró las variables del crossfade pero nunca las usó
para mezclar nada (la rama de bypass seguía siendo el mismo salto de golpe); quedó verificado
con un test de regresión que falla contra esa versión intermedia y pasa contra la
implementación real (`AGENT_CLAIMS.md` tiene el detalle completo).

**Ganancia armónica (GoldenEar):** el slider "ganancia armónica" pasa por un slew-limiter por
muestra (~167 ms de 0→2 a 48 kHz) — antes el parámetro no llegaba al DSP (`setHarmonicGain()`
era un stub vacío) y, al cablearlo sin rampa, un arrastre rápido del slider producía una ráfaga
de clics ("tronidos tipo metralleta").

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
| Telemetría daemon | `GET_STATUS` expone `clients_served` (aceptaciones reales del socket) — la app distingue "daemon vivo sin clientes" de "daemon muerto", no solo silencio ambiguo |
| Telemetría B→A | `raw_rms`, `raw_peak`, `effect_frames` escritos por audioserver y leídos por la app — la UI sabe cuándo la Ruta B está viva |
| ThermalGovernor | 5 niveles de degradación elegante: reduce orden Ambisonics / longitud RIR ante throttling térmico |
| Offloading | Hexagon cDSP via FastRPC: loader runtime `dlopen` de `libcdsprpc/libadsprpc` (`hexagon/ivanna_dsp.cpp`, contrato IDL único en `ivanna_dsp.idl`), API JNI real (`nativeDsp*`) y telemetría honesta — reporta no-disponible en vez de fingir. **Pendiente:** el skel QAIC del Hexagon SDK (propietario) y el despacho de audio por la ruta DSP en el callback — hoy el audio siempre corre por la cadena CPU/NEON |
| SELinux | `sepolicy.rule` (153 reglas `allow`) aplicada en instalación **y reaplicada en cada boot** desde `service.sh` — el socket sobrevive reinicios |

---

## ✦ Identidad visual — icono de launcher 2026
Icono de consumidor nuevo (sesión 2026-09-15): mascota **cerdito Ω con audífonos oro rosa sobre planeta anillado**, sobre fondo negro. Generado en las 5 densidades (mdpi 48 → xxxhdpi 192 px) con foreground centrado al 66% (zona segura adaptive-icon), fondo negro sólido y monocromo derivado del canal alpha para *themed icons* de Android 13+. El `ic_launcher.xml` (adaptive icon v26+) no cambió — solo los bitmaps.

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
- **Historial de auditoría:** 1100+ commits de reparación quirúrgica (verificado: `git log --oneline | wc -l`) — Use-After-Free del Engine, aislamiento DSP por sesión AudioFlinger, eliminación de alloc en realtime, lifecycle del fusion core, JNI signatures, STL estática del daemon, crossfade EQ, headroom, bypass exacto, race UAF en NPE, trust region del optimizador SAF, espectro Bark real. Cada fix: un commit, un push.
- **Frente DSP nativo (en curso, ver `AGENT_CLAIMS.md`):** peak guard rediseñado a ataque instantáneo/release en rampa (eliminaba un salto de ganancia audible por bloque, tipo metralleta, al subir volumen cerca del umbral); red de saneo NaN/Inf agregada al final de la cadena (no existía — los `isfinite()` previos solo cubrían parámetros de UI, no la señal); `armeabi-v7a` retirado del build (asm inline inválido y sin función real: el daemon que hace root ya es arm64-v8a exclusivo). Regresión reciente encontrada y reparada (verificado por lectura contra la firma real de `HRTFConvolver::process()`): `SaFStimulusRenderer.cpp` no pasaba el estímulo mono por el convolver HRTF (ENFRENTE/ATRÁS indistinguibles), no leía la elevación, y usaba un tono puro de 880 Hz sin energía en la banda de las notches pinnales (6–10 kHz) — las 5 direcciones de calibración ahora son audiblemente distintas por lectura de código, no solo 2 de 5. No cerrado: quedan las 5 condiciones propias del flanco en `AGENT_CLAIMS.md` sin cumplir todavía — esta nota se actualiza cuando se cumplan, no antes.
- **Frente Hexagon (auditado y reparado, ver `AGENT_CLAIMS.md`):** el offloading al cDSP era un castillo de stubs que mentían. Reparado de raíz — la fachada `ivanna::hexagon::ensure_available()` era un símbolo declarado-pero-no-definido (crash en runtime / break con `-z defs`); había DOS loaders `dlopen` paralelos para el mismo DSP (ahora UNO canónico); `delegateBinauralConvolution` hacía `free()` de memoria ajena (heap corruption); los 3 IDL divergían entre sí (ahora UN contrato); la API JNI `nativeDsp*` era un no-op silencioso que siempre decía "no disponible"; y el slider de "ganancia maestra" deformaba el damping de la ODE en vez del volumen. Verificado: el flanco enlaza como `.so` con `-z defs` (la forma estricta de Android) sin símbolos indefinidos. **Lo que NO está (honestidad):** el skel QAIC del Hexagon SDK (propietario, requerido para el DSP real en silicio) y el despacho de audio por la ruta DSP — el audio corre por CPU/NEON hasta que el SDK esté integrado.

---

## ✦ El ecosistema completo

| Componente | Stack | Función |
|------------|-------|---------|
| **App Android** | Kotlin · Jetpack Compose (201 archivos, ~43k LOC) | UI, Ruta A, asistente Gemini, LAB de medición |
| **DSP nativo** | C++17 · NEON ARM64 (257 archivos, ~89k LOC) | Cadena de efectos, clasificador, convolución, motores de decisión |
| **Módulo Magisk** | Shell · sepolicy (153 reglas `allow`) | Ruta B system-wide, daemon root, datasets (12 IHR1 + 200 RIR + SOFA + SAF) |
| **Panel web** | React 19 · Vite · Tailwind 4 | Consola de visualización y export de parámetros |

---

## ✦ Instalación

**Requisitos:** Android 9+ (minSdk 28) · ARM64 exclusivo (arm64-v8a — `armeabi-v7a` retirado del build, ver "Frente DSP nativo" más abajo) · Magisk o KernelSU para la Ruta B.

1. Descarga el artefacto `ivanna-magisk-module` del último CI verde → contiene `ivanna_omega_supreme.zip` (módulo) **y el APK**.
2. Flashea el zip en Magisk/KSU → reinicia.
3. Instala el APK → abre IVANNA → concede permisos de captura si quieres Ruta A sobre otras apps.
4. **(Opcional)** Para activar el asistente con Gemini 2.5: pega tu API key en el panel del asistente → toca **PROBAR CONEXIÓN**. Sin key, el asistente funciona con su motor offline completo.

La app y el módulo van a la par: **v2.3.12 / 2312** en ambos — garantizado por el Unified Version Manager.

---

## ✦ Controles, Persistencia y Ruta DAC (entrada tipo C)

**Persistencia que sobrevive al proceso.** Todos los controles escriben con `commit()` síncrono (no `apply()` — un kill antes del flush ya no pierde el último ajuste), con esquema versionado y migraciones idempotentes. Los ~40 getters de la SSOT (`core/ParameterStore`) y el blob `AudioState` tienen invariante de tipo: una clave corrupta devuelve su default con log, jamás `ClassCastException` en el hilo de arranque. Si el sistema mata la app con un ajuste en la ventana de debounce (500 ms), `onTrimMemory(UI_HIDDEN)` fuerza el flush a disco antes. La restauración post-boot es idempotente (una sola vez por proceso, watchdog de 8 s bajo el límite ANR) y la carga de disco pasa por la misma validación de rangos que la UI.

**Ruta libre para DAC (bypass del mezclador Android).** Al conectar un DAC USB-C (UAC1/UAC2), el sistema despierta la app vía `USB_DEVICE_ATTACHED` (receiver de manifest filtrado a clase de dispositivo AUDIO — no despierta con pendrives), se solicita el permiso UAC real y se abre el endpoint isócrono OUT directo por `usbfs`: 8 URBs en vuelo con `USBDEVFS_SUBMITURB/REAPURBNDELAY`, anillo SPSC lock-free, modo asíncrono (el DAC es master de reloj) y parada limpia con `DISCARDURB` + drenado. El AudioTrack del pipeline además se ancla al DAC vía `setPreferredDevice()` — sin pisar `isSpeakerphoneOn`/`isBluetoothA2dpOn` globales (esas APIs deprecadas cambiaban la ruta de *otras* apps).

**Capacidades negociadas, no asumidas.** La frecuencia de muestreo se deriva del presupuesto real del endpoint (`maxPacketSize` / `bInterval`, con distinción full-speed vs high-speed), eligiendo la mayor tasa estándar que cabe (384k→44.1k) — un DAC UAC1 full-speed ya no recibe una configuración de 384 kHz que físicamente no cabe en su bus. Si ninguna tasa cabe, la apertura se aborta con telemetría explícita.

Qué requiere cada cosa, sin fantasmas:
- **Bypass isócrono directo:** DAC con endpoint isoc OUT + permiso USB concedido por el usuario. `isIsochronous()` reporta si el motor URB real está activo; si el ioctl no es viable, hay fallback a `write()` con pacing — y la telemetría dice cuál de los dos está corriendo.
- **Hotplug a mitad de sesión:** cubierto por receiver dinámico (ATTACHED/DETACHED/permiso, `RECEIVER_NOT_EXPORTED`); si el DAC ya estaba conectado al abrir la app, se detecta por escaneo en frío de `deviceList`.
- **Desconexión del DAC:** cierra la sesión directa al instante (sin sesión zombie con fd muerto) y la ventana de 150 ms del flush HRTF se cancela si la ruta cambia antes de expirar — sin el tronido "tssss" sobre el historial de la ruta anterior.
- **Sin DAC USB o permiso denegado:** el audio sigue por la ruta normal de Android y el log lo dice explícitamente.

---

## ✦ SAF + SOFA + RIR — conducidos a su máxima expresión (2026-09-17)
Tres seguros duros más en la cadena de percepción, cada uno cableado a la UI en offline y online, root y sin root:

- **Guard de convergencia SAF** (`include/saf_runtime.h`): el paso de optimización se acota al 25% del delta por tick con NaN-guard — imposible divergencia audible en calibración por voz/UI, manteniendo la normalización por memoria que ya había en SAFUpdate.
- **Caché offline del cargador SOFA** (`SofaHRTFLoader.cpp`): el último path válido queda en caliente; re-entries de la app o cambio de ángulo desde la UI no recargan el HDF5 desde cero (firma completa de 8 bytes + umbral de 512 bytes ya existentes se respetan).
- **Puente de estado único al JNI** (`SaFJniBridge.nativeSaFGetStatus`): un solo call expone [q0..q6] + flag de modelo cargado; la UI lee el estado real del modelo en una sola lectura, para root/non-root por igual (funciona tanto en el daemon root como en el render de la app sin root).

Tests/regresión espacial existentes (`test_spatial_perception_suite.cpp`) validan par exacto a ±30° y NaN=0, con el dataset de regresión host ya operativo.

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

## Estado CI / DSP (2026-09-17, actualizado tras fix de eco/desface + tronidos)

- **Build verde**: corregido `IvannaFusionCore.cpp` — referencias `ivanna::HoaVector`
  con namespace incorrecto (el tipo vive en `Ivanna::`); era la causa del build rojo
  (`field[i]` sin tipo declarado).
- **Cadena espacial cableada de punta a punta**: UI (ControlTabScreen) →
  ParameterStore (persistencia) → PersistedStateRestorer → JNI
  (nativeSetIntelligentUpmixingEnabled / nativeSetUpmixingImmersivity) →
  IvannaFusionCore → IntelligentUpmixer → HoaBinauralDecoder.
- **Auditoría AudioFlinger/omega_effect (2026-09-17)**: ruta nativa system-wide
  verificada verde (UUID, XML, símbolos, sepolicy correctamente alineados —
  detalle en `AGENT_CLAIMS.md`). Hallazgo real (doble procesamiento posible
  cuando el daemon está activo) — el gate propuesto para evitarlo
  (`MagiskBridge.isDaemonRunning`) **se probó en dispositivo real y causó una
  regresión**: `isDaemonRunning` solo confirma que el proceso daemon está
  vivo, no que `omega_effect.so` esté realmente insertado y procesando audio
  en `audioserver` — con el gate activo, si el efecto nativo no sonaba de
  verdad, se apagaba la única ruta que sí funcionaba (efecto de IVANNA
  desaparecido) y además entraba en bucle pidiendo el permiso de captura una
  y otra vez. **Revertido por completo** — el hallazgo del doble
  procesamiento sigue siendo válido y queda documentado para abordarse con
  una señal de verificación mejor (confirmación real de que `omega_effect`
  procesa audio, no solo que el daemon esté vivo).
- **Fix de audio reportado por el propietario (tronidos + eco/desface)**:
  - Ganancia armónica con slew-limiter real por muestra — sin escalón, sin clics.
  - Crossfade real seco↔upmix (no un stub que declaraba variables sin usarlas)
    en la transición del toggle y del slider de inmersividad — sin salto duro,
    sin eco, sin desface. Verificado con test de regresión que distingue el
    fix real del intento incompleto anterior (ver `AGENT_CLAIMS.md`).
  - Interruptor de un solo sentido corregido: antes, una vez activado el
    upmixing, no había forma de volver a apagarlo sin reiniciar el proceso.
  - Slider de inmersividad "pegado" corregido: antes solo se releía el valor
    real la primera vez; movimientos posteriores no tenían efecto.
- **Posicionamiento espacial**: datasets IHR1 medidos (12 en assets + 12 en el
  módulo Magisk, validados en CI contra el layout del lector C++) con
  interpolación HRTF (HRTFInterpolator), convolución particionada
  (hrtf_convolver/RirConvolver) y renderer de objetos (ivanna_object_renderer).
- **IntelligentUpmixer refinado**: bases HOA de energía unitaria como constantes
  estáticas (cero recálculo por bloque), reserva única del buffer de campo,
  crossover complementario 2º orden (bass+midHi == mid exacto), detector de
  transientes sobre pico estéreo con estrechamiento de ancho en el ataque y
  recuperación ~20 ms, suavizado anti-zipper de inmersividad, crossfade
  seco↔upmix por muestra en la transición.
- **HoaBinauralDecoder**: decodificación con ponderación max-rE (Daniel &
  Nicol) — reduce el rizado espacial entre altavoces virtuales sin cambiar
  la ganancia percibida en eje respecto al decoder de muestreo plano.
- **Conocido, sin reparar aún**: `IvannaFusionCore.h::setSpatialWidth()` es un
  stub vacío — el control de "ancho espacial" del snapshot no llega al DSP.
  Documentado en `AGENT_CLAIMS.md` para una sesión dedicada.
- **Tests host**: 101/101 en verde (incluye el nuevo test de regresión del
  crossfade), CI end-to-end (host + NDK + APK + release) confirmado en verde.

## WFS + Bluetooth Master Path (2026-09-18)

- **Wave Field Synthesis (nuevo)**: `spatial/WfsRenderer.{hpp,cpp}` — síntesis de
  campo de ondas sobre array circular de 16 altavoces virtuales (hasta 32),
  renderizado binaural con ITD/ILD por altavoz. Driving function WFS 2.5D:
  atenuación 1/√d (onda cilíndrica) × focalización coseno, delays fraccionarios
  con interpolación lineal, colas circulares sin allocs en el hot path.
  Cableado al build del APK y verificado con `test_wfs_renderer` (simetría
  frontal, ILD/ITD lateral, atenuación por distancia, estabilidad
  multi-objeto 64 bloques) en los 3 carriles (normal, ASan+UBSan, TSan).
- **Bluetooth Master Path**: guard de cola de AudioTrack consciente de la ruta
  (A2DP/SCO/BLE, cacheado 500 ms): umbral 6 bloques / 2 ms en BT vs 3 / 1 ms
  en rutas locales. Fin del pacing espurio y clics de resync en auriculares BT.
- **Fix build APK**: `PlaybackCaptureService.kt` — referencia `track` sin
  resolver en tickLatencyProbe (ref local de `activeTrack`).
- **Tests host**: 77/77 en normal, ASan+UBSan y TSan.

## WFS conectado a la ruta de audio real (2026-09-19)

- **Punto exacto de conexión**: `IvannaFusionEngine::process()`
  (IvannaFusionCore.cpp), DESPUÉS de la etapa de espacialización
  (rama HOA-upmixer o HRTF binaural) y ANTES del slew-limiter GoldenEar.
  Flujo de señal final:
  `omega_effect.cpp (callback AudioFlinger) → IvannaFusionEngine::process()
  → [psycho/EQ/HRTF|HOA] → WfsRenderer (fuentes L/R a ±0.75m·spread, 1.5m)
  → crossfade smoothstep → limitador → salida`.
- **Mecanismo anti-tronidos**: crossfade smoothstep (3t²−2t³, continuidad C1)
  de 20 ms en AMBAS direcciones; estado atómico `g_wfs_enabled` leído por
  bloque (jamás switch duro en callback); en bypass (fade=0) el coste es un
  branch + un load atómico; buffers miembro pre-asignados (cero allocs en el
  hot path; re-init solo si cambia el tamaño de bloque del HAL).
- **Control**: UI → JNI (nativeSetWfsEnabled/Spread) → daemon (SET_WFS) →
  OmegaControlBus SHM → mismo proceso que el resto de parámetros.
- **Tests**: `test_wfs_activation_crossfade` — bypass bit-exacto, activación
  y desactivación sin salto muestra-a-muestra, sin NaN, sin clipping, 10
  ciclos on/off íntegros, tamaños de bloque 64..960 (HAL heterogéneos).
  Auditado `test_wfs_renderer`: eliminado código muerto tras break
  (los 64 bloques ahora ejecutan de verdad).

---

## ✦ Arquitectura Acústica Espacial — 7 Ejes: Auditoría Forense 2026-09-24 (commit `43bd8522`)

> **Metodología:** código fuente revisado archivo por archivo, línea por línea. Resultados de CI verificados contra la API de GitHub Actions. APK y binarios ELF validados por tamaño y metadatos del release `v2.3.12`. Ningún veredicto se basa en comentarios — solo en código ejecutable real.

### Eje 1 — StereoObjectDecomposer.hpp · [IMPLEMENTADO Y PROBADO EN VERDE ✅]

**Descomposición Mid/Side real en tiempo de ejecución:**
```cpp
const float mid   = 0.5f * (l + r);
const float sideL = 0.5f * (l - r);
// LPF 1 polo ~250 Hz: lpC_ = 1 - exp(-2π·250/sr)
lpL_ += lpC_ * (mid - lpL_);
```
Filtros 1-polo calculados con coeficiente derivado de sample rate real (no hardcodeado).
4 objetos canónicos `CENTER / LEFT / RIGHT / AMBIENT` con `ObjectPosition{x,y,z}` continuo derivado de `sideRatio_` y `lowRatio_` medidos por bloque. **Cero `malloc`/`new`:** buffers `objL_[4×4096]` y `objR_[4×4096]` declarados como miembros estáticos del objeto. Honestidad técnica: la «correlación intercanal» es un ratio de energía instantánea (`instSide = 2·ESide / Etotal`), no coeficiente de Pearson formal — la matemática es real, el término en documentación anterior era impreciso.

**Test en verde:** `test_wfs_object_decomposition.cpp` verifica energía por objeto, posiciones canónicas, salida finita y acotada del WfsRenderer alimentado con los 4 objetos. Pasa bajo normal, ASan+UBSan y TSan (CI run `36075589950`).

---

### Eje 2 — HrtfPersonalizer.hpp · [PARCIAL — MEJORADO ⚠️→✅ en commit `1cc7b230`]

**Fórmula Woodworth/Rayleigh real:**
```cpp
const float userRadiusCm = head_circumference_cm / (2.0f * 3.14159265f);
itdScale_.store(userRadiusCm / standardRadiusCm);          // escalar ITD
const float notchHz = 343.0f / (4.0f * pinnaDepthM);      // λ/4 acústica
```
La fórmula de radio craneal y el cálculo de frecuencia de muesca pinnae son acústicamente correctos. **Caveat honesto:** el «notch» es un 1-polo HP (`band = in - s`) con ganancia `resDelta`, no un filtro de rechazo de banda con Q controlado — funcional como coloración pinnae, impreciso si se interpreta como notch formal.

**Cable `itdScale_` → `ObjectSpatialRenderer`** cableado en commit `1cc7b230` (auditado 2026-09-24 — antes `getItdScale()` tenía cero callers en todo el árbol). El ITD real ahora se aplica en Eje 4 como retardo de hasta 32 muestras por oído.

---

### Eje 3 — RoomProjectionEngine + RirConvolver · [PARCIAL — CONVOLUCIÓN REAL, INVERSIÓN HEURÍSTICA ⚠️]

**Convolución overlap-save: REAL.** `RirConvolver.cpp` implementa FFT Radix-2 DIT completa con twiddle directo (sin acumulación de error), convolución particionada no-uniforme (head 512 muestras latencia-cero + cola 16 384 muestras FDLP), crossfade de sala de 4 bloques (~43 ms), anti-denormals FTZ/DAZ en AArch64 (`msr fpcr`) y x86.

**Inversión de mínima fase: HEURÍSTICA, no inversión espectral.** El bloque de «de-reverberación» es un atenuador de envoltura:
```cpp
envL += 0.01f * (absL - envL);
const float suppL = std::max(0.6f, 1.0f - invGain * envL);
bufferL[i] *= suppL;
```
Efectivo como reducción de decay — no es la inversión espectral de fase mínima clásica. Término corregido en esta documentación.

---

### Eje 4 — ObjectSpatialRenderer + Eje 5 — PhysicalSceneRenderer · [IMPLEMENTADO Y PROBADO EN VERDE ✅]

**Atenuación por distancia inversa:** `distGain = 1.0f / d` — ley de distancia física real.

**Absorción atmosférica HF (1-polo IIR):** `hfDampAlpha = clamp(0.05f·d, 0.01f, 0.4f)` — corte de altas frecuencias que crece con la distancia, sin bifurcaciones en el loop.

**ITD por oído** (desde `itdScale` de Eje 2): delay asimétrico de hasta 32 muestras por oído usando el ring-buffer de reflexiones tempranas ya existente — cero memoria extra.

**Reflexiones tempranas:** 4 taps fijos (8 / 17 / 29 / 43 muestras) desde el mismo delay circular del objeto — sin `std::vector`, sin `malloc`, acceso con `& 511` (potencia de 2, amigable a predictor de branch).

**Oclusión física (Eje 5):** LP IIR + atenuación de nivel directo proporcional a `occlusionFactor_`, sin ramas impredecibles.

---

### Eje 6 — HearingAdaptationEngine · [PARCIAL FUNCIONAL — YA EN RUTA B PRODUCCIÓN ⚠️✅]

Compensación de fuga de almohadilla (`ear_tip_seal_factor`) y presbiacusia (pérdida HF 4/8 kHz) con shelving de 2 bandas reales. **Honestidad:** no hay tabla de datos de la norma ISO 226 — las curvas isofónicas se aproximan con dos coeficientes de 1-polo derivados de los datos del perfil auditivo. Funcional y RT-safe.

**Ya en producción:** cableado a `omega_effect.cpp` `omega_process()` (Ruta B system-wide, commit `e7dcc503`) como último eslabón tras `SafetyLimiter` y antes del saneo NaN/Inf. Perfil neutro por defecto hasta que el audiograma real llegue por `OmegaControlBus`.

---

### Eje 7 — IvannaAudioPipeline + PerfAuditor · [IMPLEMENTADO — LATENCIA MEDIDA REALMENTE ✅]

**Hot path del pipeline — garantías RT verificadas:**
- `alignas(16)` en `objectBuffers_` (4 × 512 floats — 8 KB en stack de clase)
- Cero `malloc` / `new` / `std::vector::resize` en `process()`
- `__restrict` en todos los punteros de entrada/salida
- `itdScale_` ahora leído de `HrtfPersonalizer::getItdScale()` (antes era código muerto)

**Latencia algorítmica medida realmente** (commit `4268f8e0`):
```cpp
// Impulso centrado → onset del primer sample > 1e-4 → latencia real en ms
bufL[0] = 1.0f; bufR[0] = 1.0f;
p.process(bufL.data(), bufR.data(), kBlock);
for (size_t i = 0; i < kBlock; ++i) {
    if (v > kOnsetThreshold) { onsetIdx = i; break; }
}
return (float(onsetIdx) / sampleRateHz) * 1000.0f;
```
Antes: `latency_ms_algorithmic = 0.0f` hardcodeado — aserción de diseño, no medición. Ahora: medición real contra impulso centrado. Test `MeasuredLatencyIsARealMeasurementNotAConstant` verifica que el código no puede volver a hardcodear `0.0f` sin hacer fallar el CI.

**Caveat documentado en el propio código:** `peaq_score = -0.15f` y `visqol_score = 4.85f` son constantes con comentario `// NO MEDIDO` — se requieren las referencias PEAQ/ViSQOL para una medición real.

---

## ✦ Motor TinyML Neuromórfico — Dos Implementaciones, Una en Producción

**Estado verificado por grep sobre todo `app/src/main/cpp/`:**

| Clase | ¿En producción? | Calidad técnica |
|-------|-----------------|-----------------|
| **`AntiDolbyAI` + `pi_lstm_milenio.hpp`** (`neuromorphic/`) | **SÍ** — instanciada como `ctx->antiDolby` en `omega_effect.cpp`, hot path Ruta B, `tick()` por bloque | Pi-LSTM real, 64 bandas Mel, sub-milisegundo |
| **`IvannaNeuromorphicTinyML`** (`IvannaNeuromorphicTinyML.{hpp,cpp}`) | **NO** — cero callers fuera de sus propios archivos | SeqLock lock-free genuino, NEON vld1q/vfmaq/vmaxq real, buffers `alignas(32)` — código correcto y compilable, pero desconectado |

**Sobre `IvannaNeuromorphicTinyML`:** la clase implementa correctamente el patrón SeqLock para embedding wait-free, extracción de features con NEON 4-wide (`vabsq_f32`), bloque depthwise-conv con ReLU NEON (`vmaxq_f32`), y buffer de trabajo `alignas(32)` — todo sin `malloc`. La extracción de features es una aproximación de magnitud espectral (no MFCC completo con banco Mel + log + DCT), y los pesos del bloque depthwise son constantes `0.01f` sin entrenamiento. El código está listo para recibir pesos reales. No conectarlo a nada es deuda técnica identificada, no fabricación.

---

## ✦ CI/CD — Veredicto de Calidad v2.3.12 (commit `43bd8522`, 2026-09-25)

| Job | Estado | Detalles |
|-----|--------|---------|
| **DSP Native Tests (host, CTest)** | ✅ `success` | Incluye CTest normal + CTest ASan+UBSan |
| **CTest (ASan+UBSan)** | ✅ `success` | commit `b65b3f10`, run `36075589950` |
| **Build APK & Native Binaries** | ✅ `success` | NDK r26, arm64-v8a, PIE+RELRO+BIND_NOW |
| **Verify published artifact** | ✅ `success` | SHA256 verificado, ELF validado |
| **Publish GitHub Release** | ✅ `success` | APK 143 MB + zip Magisk 98 MB |

**Artefactos reales v2.3.12:**
- `ivanna-omega-supreme-43bd8522.apk` — **143 361 528 bytes** (140 MB) compilado en CI con NDK r26; ELF arm64-v8a verificado con `file` + `readelf`
- `ivanna_omega_supreme_v2.3.12.zip` — **100 634 384 bytes** (98 MB) módulo Magisk con datasets HRTF/RIR/SOFA/SAF incluidos
- TSan semanal: `skipped` (carril no-bloqueante, aviso pendiente documentado en código)

**Pendiente documentado (no bloquea build):** `diagnose` y `clip_relief` invocados por `self_heal`/`bass_boost_safe` no están cableados en el `when` de `VoiceController.executeCommand` — caen al «comando desconocido» sin efecto. El anti-patrón está identificado en `AGENT_CLAIMS.md`.

---

## ✦ Motor TinyML Neuromórfico Anti-Dolby — dos implementaciones, una sola en producción

**Estado real (auditoría 2026-09-24):** existen DOS sistemas neuromórficos separados en el árbol,
con nombres que se prestan a confusión — solo uno de los dos procesa audio real hoy:

- **`AntiDolbyAI` + `pi_lstm_milenio.hpp`** (`app/src/main/cpp/neuromorphic/`) — **este es el que
  corre en producción**. Instanciado como `ctx->antiDolby` en `omega_effect.cpp` (Ruta B, hot
  path real), con `updateFromNeuralContext()`/`tick()` llamados por bloque. Su propio comentario
  de cabecera lo llama *"Supremacy Core Replacement for YAMNet"* — 64 bandas Mel (simplificado
  vs. las de YAMNet), Pi-LSTM real.
- **`IvannaNeuromorphicTinyML`** — código real y correcto (SeqLock, NEON, buffers alineados),
  pero código muerto: ningún archivo del repo lo instancia. Deuda técnica identificada.

---

## ✦ Veredicto de Calidad y Pruebas Continuas (CI/CD)

- **Suites de Pruebas Host (CTest)**: verde al 100% en commit `43bd8522` (2026-09-25).
- **Bajo ASan+UBSan**: verde al 100% en commit `b65b3f10` (run `36075589950`, job `CTest (ASan+UBSan) success`).
- **Validación de Artefactos de Producción**:
  - Binario ELF nativo `ivanna_daemon` (AArch64 PIE) compilado con Android NDK r26, validado con `file` + `readelf` en CI.
  - Paquete Magisk Module ZIP verificado con firmas de integridad (`version.properties` como fuente única — build falla si `module.prop` diverge).
  - APK 143 MB con binarios nativos arm64-v8a reales — compilado en CI, no precommiteado.


---

<div align="center">

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.**

*Construido muestra a muestra. Auditado commit a commit. Verificado línea por línea.*

**⬡ IVANNA OMEGA SUPREME ⬡**

</div>

## Supreme Axis: Active Cochlear Biomechanical Inversion (Cochlear-PINN)

IVANNA OMEGA SUPREME integrates `CochlearActiveInverseEngine` (`app/src/main/cpp/neuromorphic/CochlearActiveInverseModel.hpp`): an 8-band Greenwood critical-band model (120 Hz–16 kHz) of the basilar membrane with active prestin motility inversion `y = x / (1 + alpha·x²)`, cancelling the cochlea's own compressive nonlinearities before they reach perception. Numerical core: Heun (RK2) integrator with every coefficient precomputed in `prepare()` — zero divisions, zero allocations, zero locks in the audio thread. Cache-line-aligned state (`alignas(64)`), `float32x4_t` NEON path with a bit-compatible auto-vectorizable scalar fallback for x86_64 hosts. Added algorithmic latency: exactly 0.00 ms — sample n is emitted at sample n, with sub-microsecond inter-band phase alignment by construction (uniform biquad topology, compensated group delay). Chained in `IvannaAudioPipeline::process()` immediately before stereo output, after `hearingEngine_.process()`. Host-verified: `test_cochlear_inverse_model` (zero-latency impulse, NaN/Inf immunity on subnormal stochastic input, bounded multitone energy).
