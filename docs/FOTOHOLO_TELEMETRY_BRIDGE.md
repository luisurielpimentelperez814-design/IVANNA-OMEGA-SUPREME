# FotoHolo Nanotomix — contrato mínimo de telemetría

## Estado real del cableado

El dashboard web ahora comparte `DspParameters` con el generador WebGL: anchura espacial y ángulo visual actualizan los controles DSP de la interfaz, y los cambios externos de esos dos parámetros se reflejan en la escena. El panel muestra explícitamente que estos valores son **solicitados por la UI**, no confirmaciones del motor nativo.

Los activos SOFA/HRTF, el índice IHR1, los RIR y el motor SAF existen en la aplicación Android. Eso no implica que el dashboard web reciba telemetría de esa ejecución: la escena React y el motor Android están en procesos/plataformas separados y no se ha encontrado un canal de runtime entre ellos. El esquema `research/deh6_optics/interfaces/acoustic_bridge.schema.json` es para registros offline, no para control en vivo.

## Contrato aproximado necesario

El puente Android → dashboard/escena debe publicar un snapshot versionado y con timestamp, no asumir que una opción seleccionada está activa:

| Grupo | Campos mínimos | Origen autoritativo |
|---|---|---|
| Sesión | `schemaVersion`, `sessionId`, `timestampMonotonicNs`, `sequence` | Servicio nativo |
| Ruta | `outputRoute` (headphones/AUX/speaker/USB/BT), `sampleRateHz`, `channels`, `bufferFrames`, `latencyMs` | AudioTrack/AudioFlinger/DSP, cuando esté disponible |
| HRTF/SOFA | `datasetId`, `format`, `loaded`, `measurementCount`, `sampleRateHz`, `taps`, `activeProfile` | Cargador HRTF real |
| RIR | `rirId`, `loaded`, `sampleRateHz`, `taps`, `rt60Seconds`, `wetApplied` | Cargador/convolucionador RIR; RT60 solo si procede de metadata validada |
| SAF/calibración | `modelLoaded`, `jniLoaded`, `phase`, `iteration`, `converged`, `errorEnergy` | `SaFEngine.state` y JNI |
| DSP efectivo | `hrtfEnabledEffective`, `spatialWidthCommanded`, `spatialWidthEffective`, `spatialAngleDeg`, `bypassEffective`, `clippingCount`, `underruns` | Readback del DSP nativo, no solo valores escritos por UI |
| Audio vivo (agregado) | `rmsDbfs`, `truePeakDbfs`, `transientFlux`, `stereoCorrelation`, `droppedFrames` | Ventanas agregadas fuera del callback crítico |
| Calidad del dato | `source`, `valid`, `staleAfterMs`, `reasonUnavailable` | Puente; nunca inventar valores faltantes |

### Reglas de seguridad y consistencia

- El callback de audio no debe serializar JSON, asignar memoria ni publicar a la UI directamente. Copiar métricas atómicas/agregadas a un snapshot desde un hilo de baja prioridad.
- Publicar con secuencia monotónica, timestamp monotónico y TTL. Si el snapshot expira, mostrar **telemetría obsoleta/no disponible**, no congelar la última lectura como si fuera actual.
- Separar `commanded` de `effective/readback`. Un parámetro enviado no demuestra que el DSP lo haya aplicado.
- Incluir ruta de salida para distinguir auriculares de AUX/altavoces. El software puede informar ruta y DSP, pero no medir el recinto físico sin micrófono/loopback de calibración.
- No enviar audio crudo, nombres de archivos personales ni identificadores del dispositivo en el snapshot por defecto.
- La escena visual debe mapear únicamente campos válidos: geometría espacial desde parámetros efectivos; envolventes/energía desde métricas medidas; reverberación desde RIR activa y validada. Datos ausentes se presentan como no disponibles, no se sustituyen por números simulados.

## Alcance estimado

1. **Ya conectado en el dashboard:** controles espaciales de UI ↔ anchura/ángulo mostrados en FotoHolo.
2. **Siguiente tramo:** exponer un `AcousticTelemetrySnapshot` desde el servicio nativo y poblarlo desde HRTF/RIR/SAF/DSP reales; añadir pruebas de unidad para campos válidos, datos ausentes y snapshots obsoletos.
3. **Puente de transporte:** preferir un flujo local tipado entre la app Android y su propia pantalla Compose. Si la visualización web se sirve dentro de Android, usar un puente WebView explícito con allowlist y validación de esquema; si es un dashboard remoto, requiere un canal autenticado aparte. No se debe asumir que el dashboard web actual comparte memoria con Android.
4. **Validación final:** instrumentar y probar en dispositivo con auriculares y AUX por separado; comparar comandos y readback; verificar que la escena cambia solo cuando cambia la telemetría efectiva. Sin ese dispositivo y el canal nativo, no es honesto afirmar conexión acústica extremo a extremo.

Este contrato es una propuesta de interfaz y alcance; no afirma que todos estos campos estén implementados actualmente.
