# Integración de las seis etapas de audio — FotoHolo Nanotomix

## Propósito

Este documento describe la cadena ejecutable del producto y separa procesamiento de audio, coordinación y visualización. La integración se considera efectiva solo cuando una etapa está en la ruta activa y su parámetro tiene un consumidor real; un control o una telemetría por sí solos no prueban procesamiento.

## Cadena objetivo y puntos de entrada

1. **ASRC y reloj de salida (reproductor propio).** `IvannaBridgePlayer` convierte el PCM decodificado a la frecuencia seleccionada para el `AudioTrack` con `StereoAudioResampler`. `AudioTrackBufferPll` ajusta el ratio dentro del límite de software cuando hay timestamps válidos y vuelve a corrección neutra cuando no los hay. Esto no controla el reloj del DAC/Bluetooth ni afecta por sí solo a otras aplicaciones.
2. **NHO / transitorios / dinámica.** El PCM remuestreado pasa por `DSPBridge.process` cuando la librería nativa está disponible. Los procesadores de respaldo Kotlin solo se ejecutan si la ruta nativa no ha procesado el bloque, para evitar duplicar NHO/Volterra/espacialidad.
3. **HRTF / SOFA y render binaural.** La ruta nativa coordina el render espacial y la personalización HRTF. La carga del dataset, el estado del motor y la aplicación efectiva son estados diferentes; el producto no debe mostrarlos como equivalentes.
4. **RIR y convolución.** El motor de sala consume una IR cargada y el nivel wet está limitado por la inteligibilidad vocal y los presupuestos de coherencia. La mera presencia de un archivo RIR no prueba que se esté convolucionando en el bloque activo.
5. **WFS / espacialidad / FotoHolo Nanotomix.** La escena FotoHolo visualiza datos acústicos agregados y parámetros espaciales. No genera hologramas ópticos físicos ni debe inventar mediciones del recinto. En la ruta nativa, la coordinación WFS debe ser consumida por FusionCore; esta revisión conecta el presupuesto de expansión de Acoustic Unity con la escala de seguridad de FusionCore.
6. **SAF y protección final.** SAF calcula/adapta parámetros de sala a través del bridge; la cadena DSP aplica protección de picos y estabilidad. SAF no sustituye al limitador y no se debe reportar convergencia como prueba de mejora perceptual.

## Cambio de esta revisión

En `omega_effect.cpp`, `AcousticUnityContext.wfsSpreadMultiplier` se calculaba por bloque pero no se pasaba al consumidor de coordinación espacial de FusionCore. Ahora se lee y valida como dato finito, se limita al rango documentado `[0.8, 1.35]`, y la expansión solicitada por encima de unidad se traduce en una atenuación conservadora de la escala adaptativa WFS. El consumidor existente mantiene su límite de seguridad `[0.5, 1.0]`; no se introduce un boost adicional ni una asignación en el hilo de audio.

## Criterios de no degradación

- Bypass y ruta nativa existentes se conservan; no se suman dos cadenas DSP al mismo bloque.
- Valores no finitos se ignoran y la escala efectiva permanece dentro del contrato del consumidor.
- HRTF, RIR, SAF y FotoHolo conservan sus APIs existentes; no se inventan read-backs.
- No se modifican XML del vendor, mixer controls, SELinux ni identificadores PAL del dispositivo.
- CI verde valida la compilación/pruebas del repositorio, no una prueba auditiva física.

## Validación requerida antes de publicación estable

1. Ejecutar los gates CI completos en el commit de integración.
2. En Moto G85 5G, probar altavoz, Bluetooth y USB-C/DAC por separado con nivel igualado; observar cortes, picos, latencia y cambios de escena.
3. Comparar bypass/activo y confirmar que la expansión WFS no empeora la correlación mono ni la inteligibilidad.
4. Si no hay read-back fiable o la ruta no está activa, presentar estado «no confirmado» en UI.

## Límites conocidos

- ASRC de software actualmente demostrado para el reproductor propio de IVANNA; no es un control universal del audio de otras apps ni un actuator ASRC confirmado del HAL Qualcomm.
- FotoHolo es una visualización/experiencia espacial sincronizada con telemetría disponible; no es una proyección óptica en el aire.
- No se afirma mejora audible ni certificación del Moto G85 hasta completar pruebas físicas A/B.
