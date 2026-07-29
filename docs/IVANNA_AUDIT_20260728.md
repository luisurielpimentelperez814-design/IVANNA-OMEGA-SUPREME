# IVANNA OMEGA SUPREME — Auditoría de conectividad UI → DSP

**Fecha:** 2026-07-28
**Base:** `origin/main @ 815344f` (verificado sha256 contra ZIP fuente).
**Método:** cada hallazgo cita archivo + línea real; se distingue BUG / HUÉRFANO / FALTA CREAR / REQUIERE EXTERNO.

Este documento acompaña a dos commits ya empujados:

- `792fa88` — fix(dsp): DspStateUpdater — Attack/Release/EQ/master ahora bajan a C++.
- `145c840` — fix(ui): IvannaCoreScreens — 8 controles con `/* TODO */` cableados.

## Hallazgo #1 — DspStateUpdater no propagaba Attack/Release/EQ (CERRADO)

- **Archivo:** `app/src/main/java/com/ivanna/omega/audio/DspStateUpdater.kt`
- **Tipo:** BUG real de lógica.
- **Detalle:** los sliders Attack/Release/EQ/masterGain del panel manual
  llegaban hasta `applyUpdate()` pero de ahí no bajaban a C++.
  `nativeSetCompressorParams` (`IvannaNativeLib.kt:117`) y
  `nativeSetEQParams` (`IvannaNativeLib.kt:42`) ya existían con la firma
  correcta.
- **Estado:** corregido en `792fa88`.

## Hallazgo #2 — IvannaCoreScreens fingía operar (CERRADO)

- **Archivo:** `app/src/main/java/com/ivanna/omega/ui/IvannaCoreScreens.kt`
- **Tipo:** BUG real de lógica + HUÉRFANO parcial.
- **Detalle:** 4 sliders en `OpeEngineScreen` (Low/Mid/High/Threshold),
  el slider Ángulo en `BinauralScreen`, y los 3 botones + toggle de
  `AdaptiveProfilesScreen` tenían `onValueChange` / `onClick =
  { /* TODO */ }`. Las dos primeras están montadas en el NavHost
  (`MainActivity.kt:262,268`), la tercera no.
- **Estado:** corregido en `145c840`.
  Nota: `AdaptiveProfilesScreen` sigue siendo HUÉRFANA de ruta
  (no montada en NavHost). Cableada por si se monta, pero por ahora
  el usuario no la ve.

## Hallazgo #3 — Huérfanos reclasificados

Barrido de todas las clases marcadas como "nunca instanciadas" por el
script previo:

| Clase | Verdict | Evidencia |
|---|---|---|
| `AudioParameterManager` | HUÉRFANO real | 0 referencias fuera de su archivo. Ver Hallazgo #4. |
| `AudioPipeline` | Falso positivo | `AudioForegroundService.kt:54` la instancia y llama `start()`. |
| `DspStateUpdater` | Falso positivo | `AdaptiveBackend.kt:45` la instancia y llama `requestUpdate()` desde cada `applyManualState()`. |
| `PhaseOracle` (Kotlin) | REQUIERE EXTERNO | Ver Hallazgo #5. |
| `SpatialAudioEngineV2` | Falso positivo | `PlaybackCaptureService.kt:267` la alimenta con `feedCapturedBlock()`. |
| `TokenManager` | HUÉRFANO por diseño | El propio archivo (`TokenManager.kt:14`) declara "módulo nuevo, independiente" para futuros hooks Magisk/PermissionManager. Regla 3: diseño intencional documentado — no se toca. |
| `VolterraH2Processor` | Falso positivo | `IvannaBridgePlayer.kt:16,194` la importa y la instancia `by lazy`. |

## Hallazgo #4 — AudioParameterManager sin caller natural

- **Archivo:** `app/src/main/java/com/ivanna/omega/audio/AudioParameterManager.kt`
- **Tipo:** HUÉRFANO real, punto de entrada NO obvio.
- **Detalle:** implementa `applyParametersWithTransition(fromState,
  toState, durationMs, onUpdate)` con `ValueAnimator`, interpolando
  campo a campo de `AudioState`. Nadie lo instancia.

  Candidatos evaluados como dueño:
  1. `MainActivity.kt:691` `onPresetSelected`: opera sobre `DSPState`,
     NO `AudioState`. Firmas incompatibles — cablearlo aquí exige
     inventar un puente `DSPState ↔ AudioState`, que es FALTA CREAR,
     no un cableado quirúrgico.
  2. `AdaptiveEngineScreen.kt:200-215` cambio de modo NATURAL/STUDIO/
     EXTREME: aquí sí opera sobre `AudioState`. Pero el modo actual
     NO tiene una tabla `mode → AudioState de referencia` — la
     transición se haría contra el mismo `audioState` que llega, sin
     nada distinto que interpolar. `AdaptiveEngineModulator.kt:29`
     (`modulateAdaptiveOutput`) ya hace el mapeo por modo, y
     `AdaptiveBackend.applyManualState` lo llama en cada cambio.
     `AudioParameterManager` sólo aportaría el suavizado temporal.

- **Necesita:** decisión de producto — ¿queremos un smoother de
  transiciones al cambiar modo/preset? Si sí, dónde: en `AdaptiveBackend`
  entre `state` y `dspUpdater.requestUpdate()`, o en el `onPresetSelected`
  con un puente nuevo `DSPState ↔ AudioState`.
- **Estado:** ABIERTO — pendiente de decisión.

## Hallazgo #5 — PhaseOracle Kotlin vs nativo

- **Archivos:**
  - `app/src/main/java/com/ivanna/omega/dsp/PhaseOracle.kt` (implementación
    Kotlin pura, 10 all-pass filters).
  - `app/src/main/cpp/*` — hay una versión nativa detrás de
    `IvannaNativeLib.kt:76-78` (`nativePredictSamples`,
    `nativeGetPhaseState`, `nativeSetPhaseParameters`).
- **Tipo:** REQUIERE EXTERNO (decisión de producto).
- **Detalle:** hay DOS implementaciones de Phase Oracle — Kotlin y
  nativa. Ninguna se invoca desde la UI. La Kotlin, además, si se
  metiera al pipeline audio corriendo en el hilo main de Compose,
  causaría ANR (procesa buffer completo por llamada, síncrono).
  La ruta correcta es la nativa, pero requiere:
  1. Cablear un slider/switch de Phase Oracle en `IvannaControlPanel`.
  2. Confirmar en qué punto de la cadena C++ se inserta (antes o
     después de `HarmonicExciter`).
  3. Decidir qué exponer: los 3 parámetros `alpha/beta/gamma` o una
     única "intensidad de coherencia de fase".
- **Estado:** ABIERTO — bloqueado por decisión de producto.

## Hallazgo #6 — TODOs restantes en código

Barrido `grep -rn "TODO\|FIXME" app/src/main`:

Sin resultados de "TODO real" fuera de los ya cerrados en `145c840`.
Los `TODO` en español dentro de comentarios narrativos (`"// TODO
verificar..."`) no cuentan como TODO estructural — son notas de
autor. Regla 3: no se tocan.

## Estado de la cadena UI → C++ (auditoría por control)

| Control | Cadena | Estado |
|---|---|---|
| Compresor Threshold | UI → AudioState → DspStateUpdater → `nativeSetCompressorParams` | ✅ |
| Compresor Ratio | idem | ✅ |
| Compresor Attack | idem | ✅ (corregido en `792fa88`) |
| Compresor Release | idem | ✅ (corregido en `792fa88`) |
| EQ Bass/Mid/Treble | UI → AudioState → AdaptiveBackend.applyEQ → `nativeSetEQParams` (ruta primaria); también por DspStateUpdater (ruta debounced, corregida en `792fa88`) | ✅ |
| Master Gain | idem | ✅ |
| Exciter Amount | UI → AudioState → DspStateUpdater → `nativeSetHarmonicGain` | ✅ |
| Spatial Width | UI → AudioState → DspStateUpdater → `nativeSetSpatialWidthDirect` | ✅ |
| Spatial Angle (rad) | UI → `nativeSetSpatialAngleRad` directo (Dashboard + BinauralScreen tras `145c840`) | ✅ |
| Adaptive Mode | UI → AudioState → `nativeSetAdaptiveControls` | ✅ |
| Adaptive Intensity | idem | ✅ |
| Voice Protection | UI → VoiceProtectionManager → engine (ruta directa, no por DspStateUpdater) | ✅ |
| HRTF enable | UI → `nativeSetHRTFEnabled` | ✅ |
| Anti-Dolby toggle | UI → AntiDolbyController.enable/disable → cadena internal | ✅ |
| Phase Oracle | (sin UI) | ❌ Hallazgo #5 |
| Preset transitions | Sin smoother | ❌ Hallazgo #4 |
| Evolutionary Kernel start/stop | UI → `nativeStartEvoThread`/`nativeStopEvoThread` (Dashboard toggle) | ✅ |
| NPE (PiLstm) params | UI → PiLstmBridge → engine | ✅ |
| Preset (Warm/Rock 70s/Spatial/Punch/IVANNA_OMEGA) | UI → `IvannaGlobalEffectManager.applyProfile` → sesiones AudioEffect del sistema | ✅ |

## Pendientes NO verificables sin compilar/ejecutar

Regla 7: sin `./gradlew build` local no puedo confirmar que estos
cambios compilen. Los símbolos usados existen (verificado uno a uno
por `grep`), pero:

1. Ejecutar `./gradlew :app:compileDebugKotlin` para confirmar tipos.
2. Ejecutar en dispositivo Magisk-root real para confirmar que las
   nuevas llamadas nativas (`nativeSetEQParams` desde
   `DspStateUpdater`, `nativeSetSpatialAngleRad` desde
   `BinauralScreen`) no bloquean el hilo de audio ni disparan
   `UnsatisfiedLinkError` (`isLoaded` está guardado, pero el JNI
   real puede lanzar en runtime).
3. Verificar con `logcat -s AudioStateManager,DspStateUpdater,
   AdaptiveBackend,IvannaOMEGA` que la ruta debounced (`DspStateUpdater
   .applyUpdate`) llega en 24 ms desde el `onValueChange` del slider,
   sin colisión con `AdaptiveBackend.applyEQ` (que es síncrona).

---

# Ronda 2 — 2026-07-29 (sincronización con CI)

**Base:** `origin/main @ 1799891` (HEAD avanzó 30+ commits desde la ronda 1;
los 3 commits de la ronda 1 — `792fa88`, `145c840`, `0861933` — siguen
presentes en HEAD, verificado con `git merge-base --is-ancestor`).

**Entrada:** logs del CI run del 2026-07-29.
- `DSP Native Tests (host, CTest)`: **16/16 PASSED, 0 failed** — C++ sano.
- `Build APK & Native Binaries`: **BUILD FAILED** en `:app:compileDebugKotlin`,
  5 errores, todos en `IvannaControlPanel.kt`.

## Hallazgo #7 — IvannaControlPanel rompía el build (CERRADO)

- **Archivo:** `app/src/main/java/com/ivanna/omega/ui/IvannaControlPanel.kt`
- **Tipo:** BUG real (build-breaking).
- **Causas raíz (3, produciendo 5 errores):**
  - **A)** `:87-90` — copy/paste pegado DENTRO del lambda por defecto de
    `onAntiDolbyChange`, en plena lista de parámetros. `LocalContext.current`
    y `remember` son `@Composable` — ilegales ahí. Errores `88:32`, `89:22`.
  - **B)** `:133` — `Unresolved reference: savedState`: `phaseOracleIntensity`
    se declaraba 6 líneas ANTES que `savedState`.
  - **C)** `:201-202` — `LocalLifecycleOwner.current` leído dentro del cuerpo
    de `DisposableEffect` y de `onDispose`. Errores `201:29`, `202:41`.
    Efecto lateral corregido: `addObserver`/`removeObserver` ahora operan
    garantizado sobre el MISMO lifecycle.
- **Estado:** corregido en `2694b50`.

## Hallazgo #8 — Slider PHASE ORACLE huérfano (CERRADO)

- **Archivo:** `app/src/main/java/com/ivanna/omega/MainActivity.kt`
- **Tipo:** HUÉRFANO — cadena construida entera menos el último cable.
- **Detalle:** la cadena estaba completa salvo el call site:

  | Tramo | Estado previo |
  |---|---|
  | `IvannaControlPanel.kt:437-452` slider → `onPhaseOracleChange(it)` | OK |
  | `AudioStateManager.kt:40` campo `phaseOracleIntensity` | OK |
  | `AdaptiveBackend.kt:112-125` `applyPhaseOracle` → nativo | OK |
  | `phase_oracle.cpp:204` implementación JNI real | OK |
  | `MainActivity.kt:649-781` call site | **NO pasaba el callback** |

  `applyPhaseOracle()` sólo corre desde `applyManualState`/`forceManualState`
  (`AdaptiveBackend.kt:157,180`), que en modo automático nunca se disparan.
  El valor se persistía en prefs y se pintaba en los StatBlocks α/β/γ, pero
  el motor nunca se enteraba.
- **Estado:** corregido en `427755b`, con el patrón nativo-directo que ya
  usan `onSpatialAngleChange` (:703) y `onNhoHarmonicChange` (:663).

## Hallazgos #4 y #5 de la ronda 1 — CERRADOS por commits intermedios

- **#4 `AudioParameterManager`**: ya NO es huérfano. `AdaptiveBackend.kt:48`
  lo instancia y `applyPresetWithTransition()` (`:131`) lo usa para
  interpolar presets en 400 ms. Cerrado.
- **#5 `PhaseOracle`**: la decisión de producto ya se tomó — se expone como
  "COHERENCIA DE FASE" (intensidad única 0..1 → α/β/γ) y usa la ruta
  **nativa**, no la Kotlin. `PhaseOracle.kt` (Kotlin) queda como
  implementación de referencia no usada en runtime. Cerrado.

## Barrido preventivo (mismo patrón de bug en todo el proyecto)

Tras el fix se barrió el árbol completo buscando repeticiones del patrón:

1. `LocalContext.current` / `LocalLifecycleOwner.current` /
   `LocalConfiguration.current` leídos dentro de `onDispose`,
   `DisposableEffect{}`, `LaunchedEffect{}` o `Runnable` → **0 casos**.
2. Lambdas por defecto en firmas con `val ... = LocalContext/remember`
   en su cuerpo → **0 casos**.
3. Balance de llaves y paréntesis en **todos** los `.kt` de
   `app/src/main/java` → **0 archivos con desbalance**.

## Pendiente de verificar (Regla 7)

Sigo sin poder ejecutar Gradle/NDK. Lo que falta confirmar localmente o
en el próximo run de CI:

1. `./gradlew :app:compileDebugKotlin` — que los 5 errores estén resueltos
   y no aparezcan nuevos por inferencia de tipos.
2. En dispositivo: mover el slider COHERENCIA DE FASE y confirmar por
   `logcat -s IvannaOMEGA,PhaseOracle` que `nativeSetPhaseParameters`
   recibe α/β/γ y que el efecto es audible.
3. Confirmar que `nativeSetPhaseParameters` devuelve `true` (retorna
   `Boolean`; hoy se ignora el valor de retorno en ambas rutas).
