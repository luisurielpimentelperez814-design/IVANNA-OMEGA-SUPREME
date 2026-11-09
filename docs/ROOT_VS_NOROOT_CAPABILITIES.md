# Capacidades con y sin root (auditoría por lectura de código, 2026-10-30)

Estado: **verificado por lectura, no en dispositivo.** No hay Android SDK ni dispositivo en el entorno donde se escribió esto.

## Los dos caminos
| Camino | Requiere | Qué procesa | Dónde |
|---|---|---|---|
| Ruta B | root + módulo Magisk + daemon | audio de **todo el sistema** | `libomega_effect.so` en audioserver |
| Ruta A (sin root) | permiso de captura (MediaProjection) | audio de apps que **permiten captura**, más el reproductor propio | `PlaybackCaptureService` → `DSPBridge.process` |
| Respaldo mínimo | nada | 4 bandas fijas de `DynamicsProcessing` sobre una sesión propia | `NoRootAudioProcessor` |

`AudioBackendSelector` elige el modo (`ROOT_DAEMON`, `ROOT_NO_DAEMON`, `NO_ROOT`) y reevalúa cada 10 s.

## Límites de Ruta A que ningún cambio de código puede quitar
- Una app puede excluirse de la captura (`allowAudioPlaybackCapture=false`) y el audio protegido por DRM no se captura.
- El audio se reinyecta por un `AudioTrack` propio: añade latencia y el usuario debe mantener la captura autorizada.
- No hay procesado a nivel de dispositivo de salida (USB, BT) ni de llamadas.

## Acciones que `OmegaEngineBridge` envía al daemon
Handlers en `daemon/control/command_server.cpp`. "Local" = también llega al motor de la app.

- **Local confirmado en el puente:** `SET_ROOM_RT60` (`nativeSetLocalRoom`), `PUSH_SAF_STATE` (`nativePushLocalSafLatent`), `SET_SCENE_POSE` (vía `IvannaSpatialManager.setScenePose`).
- **Los setters JNI de la app ya escriben también en el bus de Ruta B** (p. ej. `nativeSetAntiDolbyIntensity`); la UI que los usa funciona en ambos caminos.
- **Solo daemon desde el puente (sin espejo local verificado):** `SET_INTENSITY`, `SET_HARMONIC_GAIN`, `SEND_PERCEPTUAL_STATE`, `SET_PINNA_METRICS`, `SET_ROUTE_PROFILE`, `SET_ADAPTIVE_STATE`, `SET_PF_PARAMS`, `PUSH_YAMNET_SCORES`, y las del daemon sin llamador en el puente (`SET_4D`, `SET_WFS`, `SET_CROSSTALK`, `SET_UPMIXING`, `SET_HIRES`, `SET_NPE`, `SET_SUPREME_AXES`, `SET_USER_FLAGS`, `SET_HEAD_POSE`, …).

## Por qué no se espejaron en bloque
Cada setter local tiene escala y significado propios. Ejemplo: `nativeSetHarmonicGain` limita a 0–0.65 y escribe `g_pd.nho_harmonic`, mientras el daemon acepta 0–2 sobre `IvannaFusionCore::setHarmonicGain`. Son parámetros distintos con el mismo nombre; enlazarlos por nombre cambiaría el sonido sin que nadie lo notara. Cada acción exige comprobar equivalencia (rango, unidad, motor destino) y una prueba antes de espejarla.

## Auditoría v2.5.369 — paridad Ruta A (2026-11-08)

Directiva de Esse: ORBE y HOLOS ACTIVOS y las funciones del daemon deben
funcionar EN LA RUTA A. Esta auditoría revisó cada ítem de `RouteCapabilities`
contra el código (JNI `nativeProcessBlock` / `hyperProcessRouteA` / FusionCore,
`PersistedStateRestorer`, `OmegaEngineBridge`, `AudioEngine`, `AudioPipeline`,
`PerceptualCortex`, `AdaptiveBackend`, `audio_control_plane`).

**Conclusión: la cadena DSP completa YA corre en Ruta A y la mayoría de los
parámetros YA tienen espejo local.** La lista `daemonOnlyFeatures` se redujo
a 2 ítems. Lo que sigue es la evidencia por función.

### Cadena DSP en Ruta A (verificado en `nativeProcessBlock`, ivanna_omega_jni.cpp)
Orden: 4D feed → decisiones adaptativas → gain → peak guard → EQ → EvoEq →
comp → exciter → widener → PDEngine (NHO+Spatial) → WFS → neuro loop →
ORBE → HOLOS → Φ (`hyperProcessRouteA`) → RIR → Volterra → Cochlear-PINN →
ejes supremos (live spatial, HOA, pinna, transharmonic, MSO, warped lattice)
→ Punch → NPE → 4D output planar → SafetyLimiter → NaN/Inf safety net.
ORBE y HOLOS ya NO son excluyentes: corren en serie (ORBE primero, HOLOS
después), cada uno guardado por su propio `if (amount > 0)`.

### Por función (lo del screenshot "Root sin daemon")
- **WFS, crosstalk, upmixing, 4D y ejes supremos — YA EN RUTA A.**
  WFS: `processRouteAWfs` gobernado por `g_wfs_enabled` (lo fija
  `nativeSetWfsEnabled`, que el restorer llama al arrancar).
  Crosstalk/4D: `fourd::feedPlanar` + `applyOutputPlanar` + `routeAApplyXtcGeometry`.
  Upmixing HOA: `pipe.snnNmfHoaUpmixer().process(...)` en `nativeProcessBlock`;
  lo gobierna `g_upmixing_enabled` (`nativeSetIntelligentUpmixingEnabled`,
  restorer). Ejes supremos y pinna: bloque "Ejes 1–6 + 5 Ejes de Supremacía"
  en `nativeProcessBlock` con `SupremeAcousticStabilityGuard`.
- **Estado perceptual — YA EN RUTA A.** El bundle `SEND_PERCEPTUAL_STATE`
  va al daemon, pero cada parámetro tiene setter JNI local que el restorer
  y la UI ya llaman: `nativeSetHarmonicGain`, `nativeSetAntiDolbyIntensity`,
  `nativeSetCompressorParams`, `nativeSetSpatialWidthDirect`,
  `nativeSetLoudnessTarget`, `nativeSetSpatialAngleRad`. El audio capturado
  recibe el mismo estado perceptual; solo cambia el destino (motor local,
  no el efecto del sistema).
- **Métricas de pinna — YA EN RUTA A (vía matching de sujeto HRTF).**
  El efecto real es el matching de sujeto HRTF en proceso:
  `PinnaMetricsSection` → `IvannaSpatialManager.setHrtfSubject(id)`
  (ver comentario "HUECO CONOCIDO" en `PinnaMetricsSection.kt`).
  Caveat honesto: las métricas crudas (concha/hélix/fosa en mm) NO
  recalibran el manifold de pinna (Eje 4) en Ruta A — ese usa latentes
  dorados fijos (`kMasterSafGoldenQNorm`). El handler `SET_PINNA_METRICS`
  del daemon sí publica al bus, pero el comentario del código lo marca
  como eco sin efecto verificado en el DSP. Espejar la calibración cruda
  exigiría un JNI nuevo + persistencia y validación de sonido; no se hizo
  en la 369 porque el efecto real (sujeto HRTF) ya funciona en proceso.
- **Perfil por ruta de salida — YA EN RUTA A.**
  `AudioEngine.setRouteProfile` → `nativeSetRouteProfile` →
  `ivanna_set_route_profile` → `control_set_route_profile`, que escribe
  `route_bass_boost_db` / `route_dialog_boost_db` / `route_widener_mult`
  en el control frame local (los consume `control_apply_frame`).
  `AudioRouteManager` además lo envía al daemon; ambas patas existen.
- **Estado adaptativo y puntuaciones YAMNet — YA EN RUTA A.**
  Adaptativo: `AdaptiveBackend.applyManualState` aplica localmente
  (`dspUpdater.requestUpdate`, `applyEQ`, `applyPhaseOracle`) Y empuja al
  daemon; el motor `AdaptiveDecisionEngine` local calcula
  `g_lastAdaptive*` y `nativeProcessBlock` los aplica.
  YAMNet: la cadena local está completa y viva —
  `AudioPipeline.feedYamnet` → `classifyAndRoute` →
  `PerceptualCortex.updateContentClassification` →
  `AudioEngine.nativeSetAntiDolbyScoresStatic` → JNI →
  `ivanna_set_anti_dolby_scores` → `update_anti_dolby` →
  `control_set_yamnet_scores` (escribe `g_control_frame.yamnet_*`) →
  `IvannaControlLoop.nativeApplyControlFrame` → `control_apply_frame`
  (boost EQ +0..1.2 dB a 2 kHz si voz > 0.6; widener ×0.7 si bajos > 0.7).
  El `pushYamnetScores` al daemon es una pata adicional, no la única.
- **Intensidad global del motor — NO EN RUTA A (queda en la lista).**
  El daemon guarda `m_state.intensity` (0..1) y el motor local no tiene
  un único parámetro maestro de intensidad. Espejarlo exige decidir qué
  significa "intensidad" en Ruta A (wet/dry maestro o escala de todos
  los módulos), no solo cablear. Decisión de diseño pendiente, no un
  cableado.
- **Procesado del audio de todas las apps — NO POSIBLE sin daemon (queda).**
  Límite inherente a la captura: Ruta A solo procesa lo que la captura
  entrega. Lo que SÍ se garantiza es que TODO el DSP aplica al audio
  capturado (ver cadena arriba).

### ORBE / HOLOS activos por defecto (v2.5.369)
- ORBE amount 1.0 (limpio: THD+N −139.33 dB @1 kHz, 23× RT).
- HOLOS amount 0.5, harmonics 0.0 (retuneo medido: K=0 es lo único que
  cumple THD+N ≤ −70 dB; a 0.5/0.0: THD+N −94.0 dB @1 kHz, −79.6 dB
  @100 Hz, IMD −159.5 dB; throughput 3.5× RT — riesgo a medir CPU/XRuns
  en el G85). Ver README §5 (HOLOS) y el test
  `test_holos_retune_sweep.cpp`.

## Ya implementado
- `RouteModeBanner` / `RouteCapabilities` (v2.5.202): aviso en la UI con la lista de funciones que no llegan al audio sin daemon.
- Auditoría v2.5.369 (2026-11-08): `RouteCapabilities.daemonOnlyFeatures`
  reducida a intensidad global + procesado de todas las apps; el resto
  verificado en Ruta A (ver sección anterior).

## Siguiente trabajo (en orden)
1. Decisión de diseño: qué es "intensidad global" en Ruta A y espejarla
   (wet/dry maestro o escala de módulos), con prueba de sonido.
2. (Opcional) JNI `nativeSetPinnaMetrics` para recalibrar el manifold de
   pinna (Eje 4) con las métricas crudas en Ruta A, con persistencia y
   validación de sonido. El efecto real (sujeto HRTF) ya funciona.
3. Por acción nueva: identificar el parámetro local equivalente, documentar
   rango/unidad y espejar solo si coincide, con prueba JVM.
4. Marcar junto a cada control concreto (no solo en el aviso general) los que no tengan equivalente local.
5. Variantes Gradle `free` / `premium` (Play: sin módulo Magisk).
