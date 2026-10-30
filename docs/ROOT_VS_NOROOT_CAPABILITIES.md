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

## Ya implementado
- `RouteModeBanner` / `RouteCapabilities` (v2.5.202): aviso en la UI con la lista de funciones que no llegan al audio sin daemon.

## Siguiente trabajo (en orden)
1. Por acción: identificar el parámetro local equivalente, documentar rango/unidad y espejar solo si coincide, con prueba JVM.
2. Marcar junto a cada control concreto (no solo en el aviso general) los que no tengan equivalente local.
3. Variantes Gradle `free` / `premium` (Play: sin módulo Magisk).
