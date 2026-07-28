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
