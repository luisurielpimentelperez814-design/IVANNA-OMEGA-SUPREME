# Notificación al flanco UI — Sella `SaFEngine` (sesión 2026-09-10)

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


## Mensaje al dueño del flanco UI

Hubo que intervenir desde fuera del flanco (Ui sigue siendo tuyo por
`OWNERSHIP_PHASE_ORACLE.md` y la entrada `flanco UI` en `AGENT_CLAIMS.md`,
commit `ed83718c`). La auditoría que dejó el propietario seguía abierta y
la pantalla `SaFCalibrationScreen` no emitía ningún estímulo aunque los
módulos `SaFStimulusPlayer.kt` y `SaFCalibrationPrefs.kt` ya existían.
Sólo faltaba cablearlos a `SaFEngine.kt`.

**No dupliques trabajo:** el cableado vive ahora en
`app/src/main/java/com/ivanna/omega/saf/SaFEngine.kt`. La línea de
separación respetada fue:

- Flanco UI (TUYO, intacto): `ui/SaFCalibrationScreen.kt`.
- Flanco Motor SAF (intervenido): `saf/SaFEngine.kt` ← único archivo modificado.

## Qué cambió exactamente

| Punto | Antes | Ahora |
|---|---|---|
| `startCalibration()` | cambiaba estado a `CALIBRATING`, no emitía audio | emite el primer estímulo (`FRENTE`) por `playStimulus()` |
| `feedFeedback()` | avanzaba dirección, no reproducía tono siguiente | emite el estímulo de la nueva dirección tras avanzar |
| Persistencia de `q[7]` | sólo TXT nativo JNI (`saf_calibration_state.txt`) | espejo binario sellado `SaFCalibrationPrefs` (IVSF v2) en cada paso |
| Carga en `initialize()` | `nativeSaFLoadState` único | resolución de divergencia nativa vs prefs → iteración mayor gana; binario sellado gana al empate |
| Recursos | `scope` único | `playerScope` dedicado para `SaFStimulusPlayer.play()`; nuevo `release()` idempotente |

Decisiones de diseño relevantes:

- **Idempotencia del player**: `SaFStimulusPlayer.play()` ya incrementa
  `AtomicInteger` por generación y cancela el estímulo en vuelo. Por eso
  es seguro llamar `playStimulus()` consecutivamente en `startCalibration()`
  y en cada `feedFeedback()` sin coordinación extra.
- **No-efeción colateral**: si `play()` falla (AudioTrack float no
  disponible en el hardware, ruta de salida ocupada), el motor sigue
  permitiendo feedback — la calibración es diagnóstica aunque el estímulo
  no suene. Se loguea como `warn`.
- **Resolución nativa vs prefs**: `usePrefs = prefsSnap.iteration > nativeIter`.
  Razón: si el usuario cierra la app a mitad de calibración, el TXT nativo
  puede tener `iter=K` y el binario sellado `iter=K+1` (porque SHA-256 +
  rename atómico commitea incluso si los últimos samples no llegaron al
  flush del JNI). Gana la versión más reciente. En empate gana el binario
  sellado porque detecta truncado/manipulación que el TXT no detecta.
- **Reset consistente**: `startCalibration()` llama tanto
  `nativeSaFSaveState()` (escribe vacío) como `SaFCalibrationPrefs.clear()`
  para que el siguiente `initialize()` arranque limpio en ambas
  representaciones.

## Invariantes que la UI debe respetar

1. **`rendererHandle: Long` se sigue aceptando pero se ignora.** El motor
   ya no necesita un renderer para producir los tonos: genera AudioTrack
   propio. Pasar `0L` desde `SaFCalibrationScreen` (como ahora) sigue
   funcionando.
2. **El estado se sigue leyendo vía `engine.state.collectAsState()`.** No
   se añadió ningún StateFlow nuevo: sólo cambia la forma en que se
   emiten estímulos dos puntos del flow.
3. **`release()` debe llamarse desde DisposableEffect** en el componente
   Compose que posea el engine. Es idempotente y tolerante a fallo
   parcial, pero si no se llama el playerScope queda vivo.

## Archivos tocados en este commit

- `app/src/main/java/com/ivanna/omega/saf/SaFEngine.kt` — 92 inserciones,
  3 eliminaciones (ver `git diff --stat`).

## Archivos NO tocados (por contrato)

- `app/src/main/java/com/ivanna/omega/ui/SaFCalibrationScreen.kt` (TUYO)
- `app/src/main/java/com/ivanna/omega/saf/SaFStimulusPlayer.kt` (ya estaba,
  commit `79d352c3`, intacto)
- `app/src/main/java/com/ivanna/omega/saf/SaFCalibrationPrefs.kt` (ya estaba,
  commit `0fe03ee5`, intacto)
- `app/src/main/java/com/ivanna/omega/saf/SaFBridge.kt` (interfaz JNI, intacto)
- `app/src/main/cpp/` (toda la base nativa intacta)

Si encuentras algo que falte cablear, abre otro commit referenciando este.
— Mensaje dejado por la sesión de intervención externa (`flanco Motor SAF`),
  no por el dueño del flanco UI. Sello en `git log -1` una vez pusheado.
