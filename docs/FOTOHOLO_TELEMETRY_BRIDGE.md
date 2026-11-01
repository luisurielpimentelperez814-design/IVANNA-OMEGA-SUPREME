# FotoHolo Nanotomix — contrato mínimo de telemetría

## Estado real del cableado

El dashboard FotoHolo se construye como bundle local de Vite/WebGL y se carga dentro de `SpatialAudioPanel` mediante `WebViewAssetLoader`. El host instala `FotoHoloTelemetryBridge` antes de navegar a `fotoholo-embed.html`, limita los recursos a assets empaquetados y no expone la interfaz a contenido remoto. El bundle consulta snapshots nativos; no comparte memoria directamente con el motor DSP.

Las 64 bandas Bark se alimentan desde bloques PCM mono reales de `PlaybackCaptureService`. El timestamp de muestra representa el momento en que JNI termina de consumir ese bloque, no el momento posterior en que el dashboard lee el buffer. La captura y el motor de control corren en hilos distintos; el puente rechaza datos ausentes, futuros o vencidos antes de publicar características como actuales.

FotoHolo tiene dos caminos de control excluyentes: Ruta A escribe en el motor nativo de la app cuando SAFMUS confirma captura válida y no detecta el efecto global; Ruta B lee el propietario espacial vigente desde `GET_STATUS` y respeta la jerarquía real de `omega_apply_snapshot`: WFS, después HOA y finalmente Mid/Side. Modula únicamente el actuador que posee la salida mediante `SET_WFS.wfsSpread`, `SET_UPMIXING.upmixingImmersivity` o `SET_PERCEPTUAL_STATE.spatialWidth`.

Los valores base WFS/HOA se conservan en preferencias. Cada consulta compara el estado actual del daemon con el último valor publicado para detectar cambios externos del usuario; al cambiar el propietario, FotoHolo restaura el valor base del propietario anterior antes de aplicar el nuevo. La desviación de ancho se limita a ±0.25 y se convierte en ajustes WFS/HOA más pequeños y acotados. Las escrituras Ruta B se limitan a 5 Hz; entre ellas se consulta la generación DSP sin crear generaciones nuevas.

`GET_STATUS` expone `upmixing_enabled`, `upmixing_immersivity`, `wfs_enabled` y `wfs_spread`. El ACK de socket no se presenta como efecto audible: la entrega se confirma solo cuando `dsp_applied_generation` alcanza la generación solicitada y se identifica el actuador espacial que consumió el bloque. Si el arbitraje no es verificable, FotoHolo se abstiene de enviar el control. La Ruta A obtiene las 64 bandas del PCM de PlaybackCaptureService; la Ruta B obtiene las 64 bandas de su propio PCM dentro de omega_effect. Ambas calculan el sesgo graves/agudos y el flux transitorio con límites y rampa de control equivalentes, pero cada una analiza exclusivamente la ruta que posee. Genezy/disposición de bocinas aporta el ancho base, no sustituye el análisis espectral. La captura Bark de Ruta A se mantiene excluida cuando el efecto global posee la salida. Ruta B incorpora ahora un observador nativo read-only de 64 bandas Bark dentro de `omega_effect`: analiza el PCM que ya procesa AudioFlinger, calcula el mismo sesgo graves/agudos y flux transitorio, y modula en el callback siguiente el propietario espacial vigente (WFS, HOA o Mid/Side). El control `SET_FOTOHOLO_STATE` viaja por socket → daemon → snapshot ABI v8 (bits 6..15 libres de `temporal_field`) → `omega_effect`; no requiere capturar otra vez el audio ni procesarlo dos veces. La intensidad se cuantiza a 9 bits, el desplazamiento queda limitado a ±0.25 y la observación se decima a un máximo de 48 kHz para acotar coste en rutas USB de alta frecuencia. La aplicación de snapshot confirma consumo de generación, pero la mejora audible y la estabilidad sostenida siguen requiriendo validación en Moto G85 con reproducción real y A/B nivelado. Mid/Side acepta anchura cero como mono válido cuando HOA y WFS están desactivados; la ruta global ya no confunde ese valor con un parámetro ausente.

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
3. **Propietario espacial Ruta B:** WFS/HOA/Mid-Side se elige desde el estado vivo del daemon; se preservan/restauran bases y se detectan cambios externos del usuario. `GET_STATUS` expone los flags y valores necesarios.
4. **Entrega de Ruta B:** el ACK conserva la generación publicada y el hilo de control consulta `GET_STATUS` hasta observar `dsp_applied_generation` suficiente. No se reenvía el mismo SET para simular progreso.
5. **Pendiente de dispositivo:** demostrar en Moto G85 que las bandas avanzan con reproducción real y caducan al detener la captura; correlacionar generación publicada/aplicada y frames; comprobar por separado altavoz, USB-C, cable y Bluetooth.
6. **Pendiente de acústica:** A/B nivelado, clipping, polaridad, latencia y underruns. El read-back de generación confirma consumo de un bloque, no mejora perceptual ni respuesta medida en DAC/recinto.

## Implementación añadida en la rama de integración

- `FotoHoloEngine` publica un timestamp monotónico del último cuadro Bark válido, además de las 64 bandas, energía, reparto graves/agudos, centroide y flux. El timestamp procede del consumo JNI de PCM real y se conserva cuando no hay una muestra nueva, para poder detectar datos obsoletos; las lecturas de más de 1 s se rechazan.
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


## Ruta B FotoHolo nativa — integración de software

- `SET_FOTOHOLO_STATE` publica `enabled` e `intensity` desde la app. El demonio conserva el estado y codifica los controles en bits libres de `temporal_field`; no cambia `OMEGA_CTRL_VERSION` ni el tamaño de `OmegaDspSnapshot`.
- `FotoHoloBarkMeter` vive por instancia de `omega_effect`, usa 64 filtros band-pass Bark con estado persistente y no asigna memoria ni toma locks en el callback. Analiza el PCM procesado real de AudioFlinger y entrega una desviación limitada al siguiente callback.
- El actuador se selecciona según el mismo arbitraje del snapshot: WFS primero, HOA después y Mid/Side al final. La modulación no se suma a los tres a la vez.
- Desactivar FotoHolo, activar Bypass, perder señal o entrar en bypass térmico devuelve la desviación a cero; el valor base del usuario se conserva.
- El procesamiento global está cableado a nivel de código; no equivale a certificación de audio audible. Aún se exige CI verde, carga del módulo en Moto G85, reproducción real de apps externas, generación DSP aplicada, medición de coste/underruns y A/B nivelado por salida.
