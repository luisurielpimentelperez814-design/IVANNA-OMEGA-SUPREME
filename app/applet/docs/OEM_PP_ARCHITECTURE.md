# IVANNA OMEGA SUPREME — Arquitectura OEM++
### Auditoría v2.2.2 + arquitectura objetivo · 2026-08-30

> Este documento fue escrito contra el árbol real del repositorio (HEAD
> `5c9e24ce`). Cada afirmación cita el archivo que la respalda. No hay
> claims aspiracionales presentados como hechos; lo que no existe está
> marcado explícitamente como **[PENDIENTE]**.

---

## 1. Auditoría completa de la cadena de audio

### 1.1 La cadena real (verificada en código)

```
┌────────────────────────── RUTA A (app, proceso usuario) ──────────────────────┐
│ AudioPipeline.kt / IvannaBridgePlayer.kt                                    │
│   → DSPBridge (JNI, g_dspProcessMutex)      ivanna_omega_jni.cpp:549        │
│   → GainStage.in → ParametricEQ → Compressor → HarmonicExciter              │
│     → StereoWidener → GainStage.out         ivanna_omega_jni.cpp:1107-1115  │
│   → PDEngine.process_block (NHO + Spatial)  pd_engine.hpp:152               │
│   → SafetyLimiter (ÚLTIMO)                  ivanna_omega_jni.cpp:1144-1145  │
│   → DC blocker (5 Hz, fase mínima)          dc_blocker.hpp                  │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────── RUTA B (audioserver, sistema completo, root) ────────────────┐
│ libomega_effect.so (AudioFlinger effect, procesa TODO el audio del sistema) │
│   omega_effect.cpp                                                          │
│   → readLatest() OmegaControlBus (seqlock SHM, lock-free)  :409             │
│   → omega_apply_snapshot (una vez por generation)          :224             │
│   → IvannaFusionEngine por sesión (no global)              :547             │
│   → RirConvolver (overlap-save, MAX_IR truncado con log)   :310-361         │
│   → SafetyLimiter por contexto (aislado entre sesiones)    :567             │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────── PLANO DE CONTROL (cross-process) ────────────────────────────┐
│ UI → DSPBridge/MagiskBridge → daemon (ivanna_daemon)                         │
│   → OmegaControlBus SHM (seqlock) → omega_effect lee en cada callback        │
│   Sockets abstractos: @omega_daemon_socket (datos) + @omega_command_socket   │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Hallazgos de la auditoría (con estado)

| # | Hallazgo | Archivo | Estado |
|---|----------|---------|--------|
| A1 | SafetyLimiter corre **después** de PDEngine en ambas rutas (antes limitaba y PDEngine re-amplificaba → salida sin protección) | `ivanna_omega_jni.cpp:1144`, `omega_effect.cpp:567` | ✅ CORREGIDO |
| A2 | Desfase 0.25 muestras en HarmonicExciter (decimado escribía puntos medios sobre originales) | `HarmonicExciter.cpp` | ✅ CORREGIDO (fa5ab52a) |
| A3 | Clamp duro ±1.0 por muestra en exciter → clipping de onda cuadrada | `HarmonicExciter.cpp` | ✅ CORREGIDO (f391ef75, excScale por headroom) |
| A4 | tanh `x/(1+|x|)` = ~4.8% THD en FusionCore | `IvannaFusionCore.cpp` | ✅ CORREGIDO (Padé [3/2], <1e-4) |
| A5 | ITD asimétrico: `itd_samples()<0` para fuentes a la izquierda quedaba clameado a 0 | `cue_based_spatial.hpp` | ✅ CORREGIDO (9a97cb71) |
| A6 | Zipper noise en widener/exciter (saltos por bloque al mover sliders) | `StereoWidener`, `HarmonicExciter` | ✅ CORREGIDO (one-pole ~15 ms) |
| A7 | Buffers RT preasignados en SET_CONFIG; cero malloc/resize en `omega_process` | `omega_effect.cpp:552-575` | ✅ VERIFICADO |
| A8 | FTZ+DAZ en todos los hilos de audio (7 call-sites) | `audio_thread_priority.h` | ✅ VERIFICADO |
| A9 | `omega_process` no hace malloc/locks/logs por bloque (seqlock readLatest) | `omega_effect.cpp:409` | ✅ VERIFICADO |
| A10 | Control térmico: **cero** referencias a `getThermalHeadroom` en todo el árbol | — | ✅ CORREGIDO (ThermalGovernor, 5c9e24ce) |
| A11 | Cliente FastRPC/Hexagon: stubs documentados, fallback limpio (devuelven false → CPU/NEON sin cortar audio) | `hexagon/ivanna_fastrpc_client.cpp` | ⚠️ PARCIAL (ver §6) |
| A12 | Assets `.sofa` en `assets/saf/sofa_elite/` tienen magic HDF5 corrupto (falta byte 0x89) — no usables para regenerar IHR1 | `assets/saf/sofa_elite/*.sofa` | ⚠️ PENDIENTE (ver §2.4) |

### 1.3 Conversiones, unidades y tasas de muestreo

- **Sample rate**: ya no hardcodeado a 96 kHz — `AudioPipeline`/`AudioEngine`
  leen `AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE` con fallback 48 k
  (commits 9c17f88b, 6640442c). Rango admitido 8 k–384 k, clamp
  (`nativeInitDSP`, `ivanna_omega_jni.cpp:485`).
- **EQ**: toma el SR real del pipeline en cada `setParams` (antes fijo a
  96 kHz → todas las bandas caían una octava arriba). `ParametricEQ.cpp`.
- **Limiter**: constantes de ataque/release recalculadas al SR real
  (barrera de regresión en `tests/test_limiter_hires_timing.cpp`).
- **Unidades**: parámetros EQ en dB directos (antes se multiplicaban por
  8/12 produciendo +72 dB). `ParametricEQ.cpp:setParams`.
- **Latencia añadida**: cero en el limiter (lookahead por bloque, sin delay
  line); head tracker con predicción de 10 ms para esconder la latencia
  del buffer (`OrientationPredictor`).

### 1.4 Tiempo real

- Callback RT (`omega_process`, `nativeProcessBlock`): sin malloc, sin
  locks (salvo `g_dspProcessMutex` en la entrada JNI de control, nunca en
  el hilo de audio), sin logs por bloque, FTZ/DAZ activo.
- Telemetría `ivanna_diag.sh` con rotación de 256 KB, snapshot por ciclo
  del daemon (no por bloque de audio).

---

## 2. SOFA/HRTF nivel profesional

### 2.1 Estado real

- **12 sujetos IHR1** (512 taps @ 48 kHz) en
  `magisk_module/system/etc/ivanna_omega/hrtf/` con `hrtf_index.json` +
  SHA256 por sujeto; `customize.sh` valida hash y hace rollback completo
  si alguno no coincide (fallback sintético seguro).
- **Formato IHR1**: magic `IHR1`, numPos, irLen, srHz, tabla [az,el] y
  HRIR L/R por posición — leído por `HRTFBinLoader.cpp` (autodetección
  IVHRTF01/IHR1 por magic bytes).
- **Conversor offline** `tools/sofa_to_ihr1.py`: resample con
  `resample_poly` (fase preservada), truncado a 512 taps, normalización
  global 0.89 de headroom.
- **Selección de sujeto**: `setHrtfSubject` normaliza etiquetas legibles
  vía `resolveSubjectId` (282fceec); 12º sujeto `freefield_demo`
  (8abe28e6).
- **Carga fuera del hilo de audio**: `SET_CONFIG` (audioserver) y
  `nativeObjectRendererCreate` (app) — nunca en callback.

### 2.2 Cable SAF → Renderer (cerrado en esta serie)

- `SafHRTFBridge::initialize()/update()` publican q[7] al snapshot global
  (`ivanna_saf_apply_latent`, seqlock-lite); `ivanna_spatial_jni.cpp`
  lo aplica al `ObjectRenderer` real por handle.
- `nativeSaFInit` publica q₀ en el arranque (ya no hace falta el primer
  feedback del usuario).
- SAF actúa como capa perceptual (modifica agresividad/campo vía
  `SafSpatialModifier` → HRIR), **no toca taps HRTF ni fase**.

### 2.3 Cache de filtros

- `HRTFConvolver`: crossfade por seqlock (`newTargetPending_`), dataset
  compartido `SharedDataset` (lock-free para el hilo de audio), HRIR
  personalizado inyectable (`loadCustomHrir`).

### 2.4 [PENDIENTE] Fuentes SOFA

Los `.sofa` del árbol están corruptos (les falta el byte 0x89 del magic
HDF5). Los IHR1 desplegados son válidos (verificados por SHA256), pero
regenerar o añadir sujetos requiere re-descargar los SOFA fuente limpios
(MIT KEMAR, CIPIC, TU-Berlin, LISTEN) y re-correr `tools/sofa_to_ihr1.py`.
Mitigación: pipeline CI que descargue, valide (h5py + checksum) y genere
IHR1 + índice firmado.

---

## 3. SAF (Spatial Audio Framework)

Verificado como parte real del pipeline, no interfaz visual:

- `SaFOptimizer` (Φ_SAF^∞) — optimizador Riemanniano con estado persistido
  (`nativeSaFSaveState/LoadState`, `IVANNA_SAF_STATE_V1`).
- `SaFEngine.kt` orquesta calibración; `SaFCalibrationScreen` muestra
  iteración/energía/convergencia reales del optimizador.
- Latente q[7] llega al renderer (§2.2). **Lo que falta**: mapeo de q a
  parámetros de sala (RT60/absorción) — hoy SAF modula el campo HRTF pero
  no el motor RIR. **[PENDIENTE]** Puente SAF→RIR (selección de sala por
  q) — diseño en §7.

---

## 4. RIR (Room Impulse Response)

- **200 salas reales** (`rir_0000.wav`…`rir_0199.wav`, estéreo 16 kHz PCM)
  + `metadata.csv` (dimensiones, posiciones, distancia, RT60).
- `RirDataset`: índice perezoso (solo CSV en RAM), carga bajo demanda.
- `RirConvolver`: convolución **overlap-save con FFT radix-2 propia**,
  bloques de 512, lock-free, truncado defensivo a `MAX_IR` (conserva
  reflejos tempranos — lo perceptualmente relevante).
- **Dos consumidores reales**: `omega_effect.cpp` (Ruta B, `omega_apply_room`)
  y `ObjectRenderer::selectRoomByRT60` (Ruta A, commit 120d29c0).
- Selección inteligente por RT60 (`findNearestByRT60`, mediana del dataset
  como default) — falta selección por volumen/geometría compuesta
  **[PENDIENTE: score multi-criterio]**.
- Fallback algorítmico: FDN de 4 líneas en `ObjectRenderer` (feedback
  0.72–0.80, estable) cuando no hay dataset.
- Carga fuera del callback RT: `omega_rir_dataset_init()` en SET_CONFIG
  (commit 2ecbbbb8) — antes el primer callback pagaba 200 WAV de disco.

---

## 5. IA de audio espacial adaptativa

Capas existentes (reales, no decorativas):

| Capa | Implementación | Rol |
|------|----------------|-----|
| Clasificador de contenido | `IvannaAudioClassifier.cpp` (TinyML INT8) | tipo de contenido |
| AdaptiveDecisionEngine | `adaptive_engine_core.hpp` | target_gain/comp/exciter/width por bloque |
| YAMNet bridge | `audio_control_plane.cpp` | escena acústica → EQ/width |
| EvolutionaryKernel | `evolutionary_kernel.cpp` (LM-CMA-ES) | optimización de genoma NHO/Spatial |
| PerceptualGuard | `omega_perceptual_guard.h` | límites seguros (safety_margin, voice_protect) |
| Aprendizaje de sesgo | `LearningBias` (JVM↔C++ `learning_bias_get`) | preferencias por contexto |
| Fatiga auditiva | `nativeSetFatigueProtection` | protección de audición |

**Límites seguros**: `blend_adaptive_from_neutral` (strength-blend),
PerceptualGuard con márgenes duros, fallback a parámetros neutros si la
IA falla (`runCatching` + defaults en cada capa).

**[PENDIENTE]** Fusión térmica con la IA: el ThermalGovernor (nuevo) actúa
en paralelo al ADE; la arquitectura objetivo lo integra como entrada más
del decisor (§7).

---

## 6. Motor DSP híbrido (Hexagon / NEON / CPU)

| Nivel | Estado |
|-------|--------|
| Hexagon cDSP (FastRPC) | Cliente con dlopen perezoso de `libadsprpc/libcdsprpc`, handles con refcount, DMA buffers alineados — **símbolos DSP externos son stubs documentados**; initialize() devuelve false y el caller degrada limpio |
| NEON | Activo: limiter (peak vectorizado), FusionCore (fast_tanh NEON Padé), room_model |
| CPU escalar | Fallback universal verificado por tests de host (CTest, 23/23) |

**Detección de SoC**: parcial (diag lee presencia de cDSP/FastRPC en
`ivanna_diag.sh`). **[PENDIENTE]** tabla SoC→capacidades con selección de
backend por subsistema (convolver RIR es el primer candidato a cDSP).

---

## 7. Arquitectura objetivo OEM++

```
                    ┌─────────────── CAPA DE DECISIÓN ───────────────┐
                    │  AdaptiveDecisionEngine (Motor A)              │
                    │   entradas: contenido(TinyML) · escena(YAMNet) │
                    │   · térmico(ThermalGovernor) · ruta(RouteDsp)  │
                    │   · aprendizaje(LearningBias) · fatiga(HF)     │
                    └───────┬───────────────────────────┬────────────┘
                            │ control frame (SHM seqlock)│
        ┌───────────────────┴───┐                 ┌──────┴───────────┐
        │  RUTA A (app)         │                 │ RUTA B (audiosrv) │
        │  EQ→Comp→Exc→Wide→PD  │                 │ omega_effect      │
        │  →Limiter→DC          │                 │ FusionEngine×sesión│
        │  ObjectRenderer       │                 │ RirConvolver       │
        │  (HRTF+SAF+RIR)       │                 │ Limiter×sesión     │
        └───────┬───────────────┘                 └──────┬────────────┘
                │           BACKEND DISPATCHER            │
                │   cDSP(FastRPC) → NEON → CPU escalar    │
                └───────────────────┬─────────────────────┘
                          ┌─────────┴─────────┐
                          │  WATCHDOG + DIAG  │
                          │  ivanna_diag.sh   │
                          │  rotación 256KB   │
                          └───────────────────┘
```

### Cambios críticos propuestos (archivo → qué)

1. **`audio/ThermalGovernor.kt`** ✅ HECHO (5c9e24ce) — degradación
   proactiva por headroom térmico.
2. **`audio/BackendDispatcher.kt`** [PENDIENTE] — tabla SoC→backend,
   selección por subsistema (RIR→cDSP si disponible, exciter→NEON,
   métricas→CPU).
3. **`spatial/RirDataset.hpp`** [PENDIENTE] — `findBestRoom(rt60, volume,
   distance)` score multi-criterio (hoy solo RT60).
4. **`SaFRoomBridge.kt`** [PENDIENTE] — q[7] → parámetros de sala
   (RT60 objetivo, wet, absorción) cerrando SAF→RIR.
5. **`tools/sofa/`** [PENDIENTE] — pipeline CI de descarga+validación de
   SOFA limpios (los del árbol tienen magic HDF5 corrupto).
6. **`tests/`** — ya cubren: limiter hi-res timing, overshoot del exciter,
   RIR dataset contra datos reales, denormales, estabilidad DSP. Faltan:
   test de THD end-to-end de la cadena completa y test térmico simulado
   [PENDIENTE].

---

## 8. Riesgos y mitigaciones

| Riesgo | Mitigación actual |
|--------|-------------------|
| Bootloop por módulo Magisk | watchdog adaptativo + boot-streak con ventana temporal 10 min (1af7918c) |
| SELinux en ROMs OEM | `sepolicy.rule` v2.0 con matriz de tcontexts; magiskpolicy --live |
| Crash del daemon | watchdog con backoff exponencial; PID file atómico |
| Fallo de IA/optimizador | fallback a neutro en cada capa; runCatching + defaults |
| FastRPC ausente | stubs devuelven false → NEON/CPU sin cortar audio |
| SOFA corruptos | IHR1 firmados por SHA256 con rollback en customize.sh |
| Throttling térmico | ThermalGovernor (nuevo) degrada antes del kernel |
| Denormales en ARM | FTZ/DAZ en los 7 hilos de audio |

Compatibilidad: Magisk / KernelSU / APatch comparten el mismo módulo;
no se toca Audio Policy HAL ni servicios OEM; desinstalación atómica
(`uninstall.sh` purga logs/PID/SHM).

---

## 9. Plan de migración (sin romper instalaciones existentes)

1. **v2.2.2 → v2.3.0**: ThermalGovernor + dispatcher de backend. Sin
   cambio de formato IHR1 ni de sockets — upgrade in-place.
2. **v2.3.x**: score multi-criterio RIR + puente SAF→RIR. Compatibilidad:
   `findNearestByRT60` queda como caso degenerado del score.
3. **v2.4.0**: pipeline SOFA CI + sujetos nuevos. `hrtf_index.json`
   versionado (campo `version` ya existe); loaders rechazan versiones
   desconocidas sin crashear.
4. Cada fase mantiene: mismo path `/data/adb/ivanna_omega/`, mismos
   sockets abstractos, mismo formato de snapshot SHM (versionado por
   `OMEGA_CTRL_MAGIC`).

---

## 10. Comparación antes/después (esta serie de trabajo)

| Métrica | Antes | Después |
|---------|-------|---------|
| THD saturación (FusionCore) | ~4.8% (tanh x/(1+\|x\|)) | <0.01% (Padé [3/2]) |
| Alineación temporal exciter | -0.25 muestras (desfase+peine) | exacta |
| Clipping exciter | onda cuadrada (clamp duro) | limitación por headroom, release 20 ms |
| ITD | solo lado derecho | simétrico L/R |
| Zipper noise sliders | click por bloque | rampa 15 ms |
| Control térmico | inexistente | gobernador activo (3 umbrales) |
| Métricas falsas en UI | escáner oreja hardcodeado, curva sin() | eliminadas; Bark64 real |
| Latencia limiter | medida fake 2.8 ms en standby | 0 = sin medir (verdad) |
