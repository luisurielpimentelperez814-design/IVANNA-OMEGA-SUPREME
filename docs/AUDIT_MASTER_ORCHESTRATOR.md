# Auditoría — MasterAudioOrchestrator (Fase 0)

Estado: **auditoría real, hecha leyendo el árbol actual (`main`, commit `be214e85`)**,
no por comentarios ni documentación previa. Cubre los módulos que el prompt maestro
pide auditar. Cada fila indica cómo se verificó y con qué nivel de confianza.

**Hallazgo estructural previo, antes de la tabla:** el repo tiene **dos cadenas DSP
paralelas e independientes**, no una sola:

- **Ruta A** — `jni/ivanna_omega_jni.cpp`. Captura por software (sin Magisk), corre
  en el **proceso app**. Tiene su propia instancia de EQ/Compressor/Exciter/Widener/
  RirConvolver/SafetyLimiter (`g_eq`, `g_comp`, `g_exciter`, `g_widener`,
  `g_rirConvolver`, `g_safety_limiter`), todas variables globales `static`.
- **Ruta B** — `omega_effect.cpp` → `IvannaFusionCore::processStereo()`. Efecto de
  AudioFlinger (vía módulo Magisk), corre en el **proceso audioserver**. Tiene su
  propia instancia de cada motor (miembros de `IvannaFusionEngine` / del
  `effect_context_t` en `omega_effect.cpp`).

No pueden compartir instancias — son procesos distintos — pero **sí duplican
lógica de cableado** (cada ruta decide por separado cómo conectar Exciter→Limiter,
por ejemplo). Un futuro `MasterAudioOrchestrator` que solo hable con Ruta B **no
cubre Ruta A**. Esto no está resuelto en esta auditoría; queda documentado como
riesgo de alcance para la Fase 4.

## Tabla de auditoría

| Módulo | Produce | Consume | Estado | Fuente real |
|---|---|---|---|---|
| **AdaptiveEngineV2** | `g_wfs_adaptive_spread_scale` y otros parámetros derivados de `spatialIntensity`/fatiga/saturación (según `be214e85`) | Señal real del pipeline (a través de `ctx->adaptiveEngine`) | **VIVO** — instanciado y destruido en `omega_effect.cpp:803,1124,1183`; es el único "cerebro adaptativo" realmente conectado al audio hoy | `app/src/main/cpp/adaptive_engine_v2.hpp`, uso en `omega_effect.cpp` |
| **AdaptiveDecisionEngine** | Decisiones vía `AdaptiveStateBus` (seqlock); expone JNI propio | Debería consumir `RawMetricsBus`, publicada por el hot path | **CÓDIGO MUERTO EN LA PRÁCTICA** — se compila (`CMakeLists.txt:228`) y tiene tests propios, pero el propio `CMakeLists.txt` documenta (comentario junto a la línea 228, verificado 2026-09-10): *"ningún archivo llama a `RawMetricsBus::publish()` ni a `AdaptiveStateBus::consumeIfNewer()` fuera de `experimental/`"*. Los 4 archivos que lo mencionan (`wfs_globals_effect.cpp`, `wfs_controls_bridge.cpp`, `dsp/SafetyLimiter.cpp`, `jni/ivanna_adaptive_jni.cpp`) **solo lo nombran en comentarios**, no lo invocan. No confundir con AdaptiveEngineV2 (nombre parecido, clase distinta) | `app/src/main/cpp/experimental/adaptive_engine/adaptive_decision_engine.{hpp,cpp}` |
| **PerceptualBrainDashboard** | Estado perceptual (escena/fatiga/separación/presencia vocal) vía `PerceptualBrainEngine`/`PerceptualBrainCortex` | UI (`BrainScreen.kt`, `PerceptualViewModel.kt`), enrutado desde `MainActivity.kt`/`IvannaRoute.kt` | **CORRECCIÓN (2026-09-23, sesión posterior a esta auditoría): SÍ EXISTE y SÍ ESTÁ CONECTADO** — el hallazgo original de esta fila era incorrecto. Los archivos (`ai/PerceptualBrainCortex.kt`, `ai/PerceptualBrainEngine.kt`, `ui/PerceptualBrainDashboard.kt`) existen desde el commit `ef98f234` (2026-09-03, **20 días antes** de esta auditoría) — no una adición posterior. Confirmado con `git log --diff-filter=A` (fecha real de creación) y con `grep` de importadores reales (`MainActivity.kt`, `BrainScreen.kt`, `PerceptualViewModel.kt`, `IvannaRoute.kt`). Nota: existen también `PerceptualDecisionEngine.kt`/`PerceptualCortex.kt` con nombres muy similares — no confundir las cuatro clases entre sí sin verificar cuál es cuál antes de tocar código | `ai/PerceptualBrainEngine.kt`, `ai/PerceptualBrainCortex.kt`, `ui/PerceptualBrainDashboard.kt`, `ai/PerceptualDecisionEngine.kt`, `ai/PerceptualCortex.kt` |
| **WFS (WfsRenderer)** | Salida de objetos renderizados (VBAP+delay lines) | `IvannaFusionCore::process()` (Ruta B); geometría/spread vía `OmegaControlBus` (`SET_WFS`) | **VIVO**, auditado a fondo esta sesión (sin malloc/locks en `process()`, transiciones suaves). `WfsProtectionChain` ya integrada (`be214e85`) | `spatial/WfsRenderer.{hpp,cpp}` |
| **HRTF** | `HRIRPair` binaural vía `HRTFConvolver::process()` | Ruta B (`IvannaFusionCore`) y Ruta A (uso independiente, no verificado en esta pasada) | **VIVO** — se acaba de corregir un bug real de asignación de memoria en el hot path (`SyntheticHRTF::generate()`, ver `CHANGELOG.md` v2.3.11) | `spatial/hrtf_convolver.{hpp,cpp}`, `spatial/synthetic_hrtf.hpp` |
| **SAF (SafSpatialModifier)** | `HRIRPair` modulado por `q_t` (energía PCA) | `HRTFConvolver::updateSafField()`, hilo de control (no el de audio) | **VIVO pero de baja frecuencia** — confirmado no estar en el hot path por bloque; se dispara desde `ivanna_object_renderer.cpp` en cambios de estado, no cada bloque | `SafSpatialModifier.hpp` |
| **RIR (RirConvolver)** | Reverberación convolucionada por sala | Ambas rutas (A y B), cada una con su propia instancia | **VIVO en ambas rutas** — confirmado por instanciación real (`omega_effect.cpp:839`, `ivanna_omega_jni.cpp:687,1474,2659`) | `spatial/RirConvolver.{hpp,cpp}` |
| **Upmix (IntelligentUpmixer / HoaBinauralDecoder)** | Campo HOA a partir de estéreo | `IvannaFusionCore` (Ruta B) | **VIVO**, auditado esta sesión: buffers preasignados, `.resize()` solo actúa si cambia el tamaño (no-op si es igual) | `spatial/IntelligentUpmixer.cpp`, `spatial/HoaBinauralDecoder.cpp` |
| **Compressor** | Señal comprimida | Ambas rutas, instancia propia cada una (`g_comp` en A; miembro en B) | **VIVO**, sin malloc/locks confirmado esta sesión | `include/Compressor.h`, `dsp/Compressor.cpp` |
| **HarmonicExciter** | Armónicos añadidos | Ambas rutas — confirmado `.process()` real en Ruta A (`ivanna_omega_jni.cpp:1015,1593`); en Ruta B solo verificado por comentario, **no confirmado con la misma certeza** | **VIVO en Ruta A (confirmado); Ruta B pendiente de verificar con la misma profundidad** | `include/HarmonicExciter.h`, `dsp/HarmonicExciter.cpp` |
| **Loudness** (`loudness_meter.hpp`, `perceptual_loudness.hpp`, `equal_loudness.hpp`) | Métricas LUFS / compensación Fletcher-Munson | No confirmado como consumidor directo del hot path en esta pasada | **NO VERIFICADO A FONDO** — existen los 3 archivos, no se trazó su cableado real en este turno por límite de tiempo. Pendiente | `dsp/loudness_meter.hpp`, `perceptual_loudness.hpp`, `equal_loudness.hpp` |
| **SafetyLimiter** | Señal limitada (true-peak) | Ambas rutas, instancia propia cada una | **VIVO**, confirmado por instanciación real y comentario explícito de reutilización de la clase entre rutas (`omega_effect.cpp:21`) | `include/SafetyLimiter.h`, `dsp/SafetyLimiter.cpp` |
| **ParameterStore** | Persistencia de parámetros (SharedPreferences) | UI, `VoiceController`, `ProfileManager`, etc. | **DUPLICADO REAL, confirmado por conteo de importadores**: `com.ivanna.omega.core.ParameterStore` es la SSOT dominante (usada en `ControlTabScreen`, `MainActivity`, `ProfileManager`, `VoiceController`, `PersistedStateRestorer`, `OemViewModel`...). `com.ivanna.omega.audio.ParameterStore` es un store **separado, deliberado** (Gson, para `AdaptiveBackend`) — su propio comentario dice explícitamente que es independiente del de `core`. No es un bug, pero si el Orquestador va a escribir preferencias, debe apuntar a `core.ParameterStore` (la SSOT) | `app/src/main/java/.../core/ParameterStore.kt`, `.../audio/ParameterStore.kt` |
| **OmegaControlBus** (daemon) | Snapshot de estado (WFS, RIR, EQ, etc.) publicado por el daemon root | `omega_effect.cpp` (`omega_apply_snapshot`), ambas rutas vía socket de comandos | **VIVO** — es el canal real app↔daemon↔audioserver ya usado hoy por `SET_WFS`, `SET_HIRES`, etc. (confirmado y extendido esta sesión con `nativeSetWfsSpeakerLayout`) | `daemon/core/omega_control_bus.cpp`, `include/omega_control_bus.h`, `daemon/control/command_server.cpp` |
| **AdaptiveStateBus** | Snapshot de decisión adaptativa (seqlock) | Nadie fuera de `experimental/` — ver fila AdaptiveDecisionEngine | **BUS SIN CONSUMIDOR REAL** — exactamente el patrón que el prompt pide detectar. Existe, compila, tiene su propio bug de sincronización ya corregido (ver comentario en `include/audio_bus.h`), pero no está conectado al camino vivo | `experimental/adaptive_engine/adaptive_decision_engine.hpp` |
| **JNI** | Puentes Kotlin↔C++ | App (Ruta A) y wrapper de IME | **VIVO, mapeado en profundidad esta sesión** (se repararon 2 símbolos indefinidos y una función JNI faltante esta misma sesión) | `jni/ivanna_omega_jni.cpp` (~2600 líneas, Ruta A completa), `jni/ivanna_ime_jni.cpp`, `jni/ivanna_adaptive_jni.cpp` (puente hacia el motor desconectado) |
| **IvannaFusionCore** | Orquesta HOA/HRTF/WFS/RIR/Compressor/Widener (Ruta B) | `omega_process()` | **VIVO, es el "cerebro" actual de Ruta B** — pero decide con lógica si/else embebida en `process()`, no con un motor de decisión centralizado. Es, en la práctica, el candidato natural a *consumir* las salidas del futuro `MasterAudioOrchestrator`, no a ser reemplazado por él (regla explícita del prompt: "no crear otro FusionCore") | `IvannaFusionCore.{h,cpp}` |

## Conclusiones de la Fase 0 (antes de tocar código de Fase 1+)

1. **Ya existe un motor de decisión centralizado a medio construir**:
   `AdaptiveDecisionEngine` + `AdaptiveStateBus` + `RawMetricsBus`, con su propio
   JNI (`ivanna_adaptive_jni.cpp`) y tests. Está **compilado pero desconectado**
   (0 publishers reales, 0 consumers reales fuera de su propia carpeta). Antes de
   escribir un `MasterAudioOrchestrator` nuevo, hay que decidir explícitamente:
   **¿se resucita/cablea este motor, o se documenta como abandonado y se construye
   uno nuevo?** El prompt maestro prohíbe "duplicar AdaptiveDecisionEngine" — pero
   ya existe un `AdaptiveDecisionEngine` real en el árbol. Construir el
   `MasterAudioOrchestrator` sin tocar ese archivo, o sin dejar constancia
   explícita de por qué no se usa, sería violar esa regla por omisión.
2. **Dos rutas de audio independientes (A y B)** — cualquier orquestador que solo
   hable con Ruta B deja Ruta A (captura por software, sin Magisk) sin coordinar.
   El prompt asume un solo camino (`AudioFlinger → libomega_effect → IvannaFusionCore`);
   el árbol real tiene dos. Documentado aquí porque, por regla del propio prompt,
   "si el árbol contradice este documento, gana el árbol".
3. **`ParameterStore` duplicado es intencional, no un bug** — pero cualquier
   parámetro nuevo del Orquestador debe usar `core.ParameterStore` (la SSOT), no
   crear un tercero.
4. **Loudness (3 archivos) no se trazó con la misma profundidad** que el resto —
   pendiente antes de que el Orquestador dependa de sus métricas.
5. **CORRECCIÓN (sesión 2026-09-23, posterior)**: `PerceptualBrainDashboard` **sí existe** y **sí está conectado** (`MainActivity.kt`, `BrainScreen.kt`, `PerceptualViewModel.kt`, `IvannaRoute.kt`) — existía ya 20 días antes de esta auditoría (commit `ef98f234`, 2026-09-03). El hallazgo original de este punto era incorrecto; ver la fila corregida arriba.

## Alcance de esta auditoría (honestidad sobre límites)

Esta Fase 0 cubre los 16 módulos pedidos con evidencia real (grep + lectura de
código + `git blame`/comentarios propios del repo), pero **no** incluye todavía:
- Medición en dispositivo de ningún camino (todo es análisis estático + CI host).
- Trazado completo de Loudness ni de la Ruta A completa símbolo por símbolo.
- Diseño de los contratos de datos de Fase 1 (`OrchestratorInputs`/`Outputs`) —
  eso es la Fase 1, no esta.

**No se ha escrito ninguna línea de `MasterAudioOrchestrator` todavía.** Por regla
explícita del prompt ("NO escribir MasterAudioOrchestrator hasta terminar esta
auditoría"), y dado el hallazgo del punto 1 (un motor de decisión ya existe, a
medio cablear), la Fase 1 no debería empezar sin que el dueño del repo decida
explícitamente qué hacer con `AdaptiveDecisionEngine` — construir sobre él o
documentar por qué se abandona.
