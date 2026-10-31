# FotoHolo Nanotomix — contrato mínimo de telemetría

## Estado real del cableado

El dashboard FotoHolo se construye como bundle local de Vite/WebGL y se carga dentro de `SpatialAudioPanel` mediante `WebViewAssetLoader`. El host instala `FotoHoloTelemetryBridge` antes de navegar a `fotoholo-embed.html`, limita los recursos a assets empaquetados y no expone la interfaz a contenido remoto. El bundle consulta snapshots nativos; no comparte memoria directamente con el motor DSP.

Las 64 bandas Bark se alimentan desde bloques PCM mono reales de `PlaybackCaptureService`. El timestamp de muestra representa el momento en que JNI termina de consumir ese bloque, no el momento posterior en que el dashboard lee el buffer. La captura y el motor de control corren en hilos distintos; el puente rechaza datos ausentes, futuros o vencidos antes de publicar características como actuales.

FotoHolo tiene dos caminos de control excluyentes: Ruta A escribe en el motor nativo de la app cuando SAFMUS confirma captura válida y no detecta el efecto global; Ruta B publica únicamente `spatialWidth` al daemon cuando el AudioEffect global posee la salida. La publicación del comando no prueba que el bloque DSP lo haya consumido: la confirmación final debe correlacionar la generación enviada con `dsp_applied_generation` del beacon.

Los activos SOFA/HRTF, RIR y SAF disponibles en la app no son por sí solos prueba de que estén cargados o aplicados a la ruta activa. El snapshot debe seguir separando disponibilidad de archivo, carga del dataset, comando, read-back y evidencia acústica.

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

## Alcance y validación

1. **Ya conectado:** WebView local y aislado en `SpatialAudioPanel`, puente Android instalado antes de cargar el bundle, snapshot de solo lectura y escena WebGL que consume la telemetría.
2. **Cableado de audio endurecido en este candidato:** timestamp monotónico desde el productor PCM, TTL de 1 s, descarte de muestras vencidas y exclusión de Ruta A/Ruta B cuando la propiedad global no puede determinarse.
3. **Entrega de Ruta B:** el ACK conserva la generación publicada y el hilo de control consulta `GET_STATUS` hasta observar `dsp_applied_generation` suficiente. No se reenvía el mismo SET para simular progreso.
4. **Pendiente de dispositivo:** demostrar en Moto G85 que las bandas avanzan con reproducción real y caducan al detener la captura; correlacionar generación publicada/aplicada y frames; comprobar por separado altavoz, USB-C, cable y Bluetooth.
5. **Pendiente de acústica:** A/B nivelado, clipping, polaridad, latencia y underruns. El read-back de generación confirma consumo de un bloque, no mejora perceptual ni respuesta medida en DAC/recinto.

## Implementación añadida en la rama de integración

- `FotoHoloEngine` publica un timestamp monotónico del último cuadro Bark válido, además de las 64 bandas, energía, reparto graves/agudos, centroide y flux. El timestamp se conserva cuando no hay una muestra nueva, para poder detectar datos obsoletos.
- `FotoHoloTelemetryBridge.kt` expone un snapshot JSON de solo lectura a través de `@JavascriptInterface`. Incluye antigüedad de la muestra, ruta/frecuencia/latencia/cortes disponibles en `OmegaMetrics`, lectura espacial nativa, estado del perfil HRTF, carga/conteo del dataset RIR y último estado publicado por `SaFEngine`.
- `FotoHolo3DGenerator.tsx` consulta `window.IvannaFotoHoloBridge.getSnapshotJson()` cada 250 ms si el host la expone. Ajusta las superficies de onda a energía y transitorios Bark reales, y deja de mostrar esas lecturas como vivas cuando la muestra tiene más de 1 segundo.
- El panel distingue disponibilidad de archivos, dataset cargado y aplicación efectiva. HRTF separa perfil cargado de la bandera activa nativa; RIR expone carga y número de salas, pero no confirma qué IR está convolucionándose ni el wet aplicado; SAF expone el último estado real publicado por una instancia de `SaFEngine`, y se mantiene como no inicializado si no hay instancia.

## Límite de integración y pruebas de aceptación

El puente Android→WebGL ya está integrado en `SpatialAudioPanel.kt`; no debe describirse como una tarea pendiente. El límite restante es demostrar que cada dato representa el estado acústico de la ruta que realmente está reproduciendo. En particular, el WebView local y el snapshot de Kotlin no prueban por sí solos que `libomega_effect.so` esté insertada en AudioFlinger ni que el DAC haya recibido el procesamiento.

### Pruebas de aceptación pendientes
1. Build Android, bundle WebGL y pruebas JVM en CI.
2. En Moto G85, comprobar que `sampleTimestampMonotonicNs` avanza al ingerir bloques PCM reales y envejece al detener la captura.
3. Confirmar que `sampleAgeMs` supera el TTL y que bandas/características se ponen a cero cuando el dato caduca.
4. Con Ruta B, correlacionar `generation`, `dsp_applied_generation`, `dsp_frames` y estado del beacon durante reproducción real.
5. Verificar audible A/B, clipping, polaridad, latencia y underruns por altavoz, USB, cable y Bluetooth.
6. HWS-01..04 y su evidencia física siguen siendo puertas independientes para cualquier release de producción.

## Refinación visual asistida por IA y render HD/4K

La escena FotoHolo incluye ahora un flujo explícito de refinación visual bajo demanda:

- **Android/WebView local:** `FotoHoloTelemetryBridge.requestVisualRefinement` envía a `GeminiOrchestrator` únicamente parámetros de escena y métricas acústicas agregadas recientes. La respuesta es asíncrona y vuelve al WebView como evento `ivanna-fotoholo-ai-result`.
- **Dashboard servido por Node:** `POST /api/fotoholo/refine` usa el proxy Gemini del servidor, validación estricta, límites de valores y rate limit. Requiere `GEMINI_API_KEY`; si se configura `DASHBOARD_AUTH_TOKEN`, el cliente debe tener un mecanismo de autenticación de sesión antes de usar la ruta.
- **Aplicación en caliente:** la propuesta se valida y limita antes de cambiar la escena. Anchura y ángulo solo se mandan al DSP nativo por los métodos ya existentes; la IA no recibe permiso para emitir comandos de audio. Si Gemini no está disponible, la interfaz solo usa un ajuste adaptativo local cuando hay telemetría reciente y lo etiqueta como no generativo.
- **HD/4K:** selector Auto, 1920 px y objetivo 3840×2160. El render limita dimensiones al máximo de textura de la GPU y al presupuesto aproximado de 8.3 megapíxeles; si el hardware no soporta 4K, muestra la resolución efectiva en vez de afirmar que lo logró. Se reutilizan buffers GPU entre fotogramas para evitar recrearlos en cada render.
- **En vivo:** los frentes de onda continúan reaccionando a energía y transitorios Bark recientes. Las métricas obsoletas no se presentan como actuales.

La IA refina la **presentación visual**, no certifica una simulación física ni sustituye HRTF/RIR/SAF. La disponibilidad de Gemini depende de la configuración de claves del entorno. El objetivo 4K depende de la GPU/dispositivo; no es garantía de 4K en todos los teléfonos.
