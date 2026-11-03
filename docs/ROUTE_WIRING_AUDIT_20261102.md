# IVANNA — auditoría de cableado de rutas de audio

**Fecha:** 2026-11-02  
**Repositorio:** `luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME`  
**Base auditada:** `main` en `4ba6694879a7151b3023bfe4b983c062e396ba49`  
**Rama de reparación Kotlin:** `fix/build-v2.5.233-kotlin-compile`, HEAD `4dc9ea365b53c36940c3925344f1ff847f0abb02` (PR #15 abierto; no afirmar merge hasta que GitHub lo confirme).

## Dictamen

IVANNA dispone de dos rutas de salida (efecto de sistema y ruta propiedad de la app) y de varios puntos de entrada JNI in-process que procesan audio dentro de la ruta de la app. La integración debe preservar su propiedad de reloj y evitar que dos rutas procesen la misma señal. La presencia de una clase, un setter JNI, un socket o un daemon no demuestra por sí sola que el bloque PCM de la salida elegida lo haya consumido.

## Topología verificada

### Ruta A — efecto de sistema / AudioFlinger

`Aplicaciones Android → AudioFlinger → libomega_effect.so → omega_process()
→ OmegaControlBus / omega_apply_snapshot() → IvannaFusionCore + IvannaAudioPipeline
→ HAL OEM → salida activa`

En el snapshot de Ruta A se verifican consumidores de perfil de ruta, EQ por bandas, HRTF enable/pose, vector latente SAF q[7], WFS, NPE y sala RIR. El callback también invoca `processLiveSpatialAxes()` cuando el arbitraje permite ese renderer. La confirmación de funcionamiento global exige beacon/contador de frames del efecto en el dispositivo; daemon activo o módulo instalado no bastan.

### Ruta B — reproductor/captura propiedad de IVANNA

Reproductor propio (`IvannaBridgePlayer`):

`MediaCodec → PCM estéreo → StereoAudioResampler → DSPBridge / etapas locales
→ AudioTrack propio → AudioFlinger → HAL → salida`

Captura de reproducción permitida (`PlaybackCaptureService`, ruta independiente que no usa `StereoAudioResampler`):

`AudioRecord (AudioPlaybackCapture) → PCM estéreo a SAMPLE_RATE (48 kHz) → DSPBridge / etapas locales
→ AudioTrack propio → AudioFlinger → HAL → salida`

`IvannaBridgePlayer` toma timestamps del mismo `AudioTrack`, empareja `framePosition` y `nanoTime`, obtiene una corrección acotada del único `AudioTrackBufferPll` y la entrega a `StereoAudioResampler.setClockCorrectionPpm()`. Este es el ASRC software vivo para el reproductor propio. No controla el reloj físico del DAC, el oscilador Bluetooth ni las demás aplicaciones.

Playback Capture está sujeto al permiso MediaProjection y a la política de captura de la aplicación origen; no captura universalmente cualquier aplicación.

### Entrada JNI in-process — subruta de procesamiento de la app

`DSPBridge_nativeProcess / IvannaNativeLib_nativeProcessBlock
→ IvannaAudioPipeline::processLiveSpatialAxes()
→ etapas nativas permitidas → salida propia de la app`

No es una tercera salida independiente: es un punto de entrada de procesamiento dentro de la ruta propiedad de IVANNA. Comparte motores nativos con Ruta A, pero no debe recibir el mismo PCM que ya procesó el efecto del sistema.

## Matriz de funciones y límites

| Función | Consumidor verificado | Alcance real |
|---|---|---|
| EQ / perfil de salida | AudioRouteManager → JNI local y OmegaEngineBridge; Ruta A aplica snapshot al FusionCore | Perfiles por Bluetooth, AUX, USB y altavoz; read-back/runtime sigue siendo específico del dispositivo |
| HRTF / SOFA | Ruta A FusionCore; Ruta C JNI; RouteDspCalibrator aplica política por salida | HRTF binaural estéreo; el dataset cargado no equivale a bloques procesados |
| SAF / SAFMUS | q[7] validado en snapshot y entregado al motor espacial de Ruta A; acoplamiento de RIR/HRTF donde está implementado | q[0]/q[1] tienen actuadores documentados; q[2..6] están reservados, no declarar morph completo |
| RIR / convolución | RirConvolver en Ruta A y Ruta C; Ruta A de sistema tiene worker de carga fuera del callback | Solo activo cuando IR/dataset está cargado y wet efectivo; no afirmar una sala activa por presencia de assets |
| WFS / HOA / upmix | FusionCore y renderers con arbitraje de prioridad | Buses/objetos virtuales renderizados a la salida que realmente exponga el sistema |
| NHO / dinámica / limitador | DSPBridge/JNI local y consumidores del snapshot del efecto | Deben conservar headroom, bypass transparente y límites por bloque |
| Adaptativo / NPE | setters/estado nativo y consumer de bloque; Ruta A evidencia por contador de bloques | Habilitado no significa procesando; exigir contador/delta real |
| ASRC software del reproductor | AudioTrackBufferPll → StereoAudioResampler | Solo el AudioTrack de `IvannaBridgePlayer`; `PlaybackCaptureService` no usa este ASRC |
| VariableAsrc C++ + ASRC HAL global | componente nativo y tests existen; no hay call-site de producción para `VariableAsrc::setTargetRatio()` | **NOT_APPROVED / BLOCKED** hasta recibir pares frame/timestamp del PAL propietario y conectar el actuador al mismo stream |
| 16 fuentes → 5 buses | PhysicalTargetDispatcher experimental | **NOT_IMPLEMENTED como salida física 5.0**; cinco buses lógicos no prueban cinco canales físicos |

## Bloqueos que no se deben ocultar

1. **ASRC system-wide Moto G85:** `HalClockProducer::ensureSource()` devuelve `kNoSource` si no hay un publicador ligado al stream HAL/PAL propietario. Es la conducta correcta: abrir un PCM ALSA independiente no identifica el reloj del stream audible. Falta integrar el callback en la fuente PAL propietaria, publicar frames + timestamp monotónico + streamId + routeGeneration, correlacionarlos y después enlazar un actuador software en el mismo flujo. Hasta entonces ratio unity/bypass.
2. **Salida 5.0 física:** `PhysicalTargetDispatcher` produce cinco buses lógicos y no abre un dispositivo Android. Antes de activarlo en producción hay que negociar canales reales, confirmar el dispositivo/ruta y construir una matriz calibrada con headroom. En el Moto G85 con salida estéreo, la presentación debe permanecer binaural/downmix estéreo; no inventar satélites o LFE.
3. **Telemetría:** separar `requested`, `accepted`, `applied` y `processed`. Para afirmar procesamiento, exigir contador de bloques y evidencia de salida/energía en la ruta activa; no usar daemon/socket conectado como sustituto.
4. **Bypass y recuperación:** todo módulo opcional debe volver a identidad/bypass si faltan dataset, clock pair, permiso, canal o presupuesto. Los cambios de ruta reinician estimadores de reloj; no interpretar un salto de latencia por Bluetooth/USB como deriva de reloj.

## Validación requerida

- CI estático: `python3 scripts/check_audio_route_wiring.py` (contrato de call-sites, no prueba de runtime).
- CI nativo: SOFA HDF5, SAFMUS, ASRC numérico, CTest y sanitizers según workflow.
- CI Android: `build-apk` debe compilar release APK, daemon ARM64 y módulo Magisk desde fuente; comprobar manifiestos y SHA-256.
- Moto G85: validar por separado Ruta A con beacon/frames de AudioFlinger y Ruta B con timestamps de AudioTrack; repetir por altavoz, AUX, USB y Bluetooth. PAL global permanece bloqueado hasta evidencia del callback propietario.

## Reversión y seguridad

Esta auditoría no habilita actuadores nuevos ni cambia permisos, SELinux, mixer controls o configuración del sistema. Las etapas opcionales conservan sus feature flags y bypass. Si una nueva integración de reloj o multicanal no supera sus gates, se mantiene desactivada sin afectar la ruta estéreo funcional.
