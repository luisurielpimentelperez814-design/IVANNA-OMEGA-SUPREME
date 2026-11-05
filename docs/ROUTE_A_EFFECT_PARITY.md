# Paridad de efectos: Ruta A (captura de reproducción) frente a Ruta B (efecto del sistema)

Estado: **auditoría por lectura de código y búsquedas en el repo (2026-11-04), sin ejecutar nada en dispositivo.**
Un nombre parecido no prueba un comportamiento igual: cada fila "equivalente" es una correspondencia por lectura, no una medición.

## Premisa del propietario

La captura de reproducción (Ruta A: `PlaybackCaptureService` → motor nativo de la app → `AudioTrack` propio) intercepta ahora el audio. Se pidió que los efectos del producto se apliquen por esa ruta.

## Hallazgo estructural

`IvannaFusionCore.cpp` (clase `IvannaFusionEngine`) se compila **solo** dentro de `libomega_effect.so` (unity-build en `omega_effect.cpp`, Ruta B). La app (`libivanna_omega.so`) no lo enlaza: Ruta A arma su cadena con otras piezas. Los efectos de ese motor no corren en Ruta A salvo que tengan un equivalente propio.

## Mapa por etapa

| Efecto | Ruta B | Ruta A | Estado |
|---|---|---|---|
| Coclear activo inverso | `ctx->cochlearEngine` | `g_cochlearEngine` | mismo módulo |
| Pipeline unificado declarativo | `unifiedPipeline` | `g_unifiedPipeline` | mismo módulo |
| 5 ejes supremos | `supremeLattice/Cvnn/SnnHoa/Pinna/MsoFarrow` | `pipe.warpedLatticeInverter/transharmonicSynth/snnNmfHoaUpmixer/pinnaManifoldInterpolator/shmMsoArbitrator` | mismas clases con distinto nombre; Ruta A activa por `pipe`, Ruta B por bits `OMEGA_FLAG_SUPREME_*` |
| Reconstrucción de realidad | `AcousticRealityOrchestrator` | ídem | compartido |
| Volterra H2, anti-Dolby, RIR, punch, limitador, guardas | sí | sí | compartidos (con guardas propias en Ruta A: `s_routeAGuard`) |
| Cadena 4D (arco, diafonía, dosímetro) | `fourd::*` | `fourd::*` | **mismo módulo**; la geometría de sala del cancelador solo llegaba a Ruta B (corregido en v2.5.346) |
| WFS | `IvannaFusionEngine::m_wfs` | `RouteAWfsEngine` | equivalente propio |
| Upmix / M/S / campo AUX / campo temporal / NPE / corrección de sala | dentro de `IvannaFusionEngine` | globales y `g_upmix`, `g_widener` en el JNI | equivalentes propios, no verificados al mismo nivel |
| Adaptativo | `ctx->adaptive` (ADE) | ADE en la app (worker Kotlin) | equivalente propio |
| **EvolutionaryEQ** (FIR de 512 bandas) | `m_evoEq->processNEON` | `routeAApplyEvoEq` tras `g_eq` (v2.5.348) | **movido a Ruta A**: misma clase, calibración en hilo propio, toggle en la UI; sin medir en dispositivo |
| **Escena binaural HRTF de `HrtfManager`** | `m_hrtf->processBinauralScene` | `PDEngine` (NHO + espacial + HRTF) | motores distintos; no equivalentes |
| **GoldenEar GAN** | `applyGoldenEarGAN` | casi ausente (1 mención) | solo Ruta B |
| **`AdaptiveEngineV2`** | `ctx->adaptiveEngine` | ausente | solo Ruta B |
| **Bits `OMEGA_FLAG_ATLAS_*`** | se leen en `omega_effect.cpp` | no se leen | solo Ruta B |
| **Offload FastRPC** (`IvannaFastRpcClient`) | sí (hardware/root) | — | no trasladable: requiere el DSP del dispositivo |
| Acciones del daemon sin espejo local (`SET_INTENSITY`, `SET_HARMONIC_GAIN`, `SEND_PERCEPTUAL_STATE`, `SET_PINNA_METRICS`, `SET_ROUTE_PROFILE`, `SET_ADAPTIVE_STATE`, `SET_PF_PARAMS`, `PUSH_YAMNET_SCORES`) | sí | según `docs/ROOT_VS_NOROOT_CAPABILITIES.md` | sin cambio |

## Por qué esta versión no mueve efectos de B a A

- Llevar `IvannaFusionEngine` completo a Ruta A **duplicaría etapas** que Ruta A ya tiene con otro nombre (WFS, upmix, M/S, compresor, EQ) y cambiaría el sonido sin que nadie lo pueda comprobar aquí.
- El historial del repo (`AGENT_CLAIMS.md`) documenta que un cambio de rutas sin prueba en dispositivo hizo desaparecer el efecto y dejó a la app pidiendo el permiso de captura en bucle.
- Cada traslado necesita su correspondencia de parámetros (escala y unidad), prueba de identidad cuando está apagado y una escucha o medición real.

## Qué sí cambia en v2.5.346

La geometría de sala del cancelador de diafonía (transaural) ahora también llega a Ruta A. En v2.5.343 solo se aplicó dentro de `omega_effect.cpp`; el cancelador de Ruta A, que es el de la cadena 4D, seguía con la geometría de escritorio (0.90 m de separación, 0.85 m de distancia) en vez de la sala 3.83 × 2.83 × 7.00 m. Se deriva de FL/FR del layout del WFS de Ruta A, con la misma función que Ruta B (`RoomGeometryConfig::crosstalkPlacement`).

Verificado: prueba estructural (`tests/test_magistral_defaults.py`) y el helper compila aislado. **No** se compiló la biblioteca JNI completa (necesita NDK) ni se probó en el dispositivo.

## Siguiente trabajo (en orden de menor riesgo)

1. Decidir con el propietario cuáles de los efectos "solo Ruta B" usa de verdad.
2. Por cada uno: correspondencia de parámetros, identidad bit-exacta apagado, prueba host y escucha en el dispositivo.
3. Marcar en la UI los controles que no llegan a Ruta A.
