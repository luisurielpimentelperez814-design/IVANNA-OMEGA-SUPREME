# IVANNA-ASRC-X1 — estado de integración

## Estado honesto
- Núcleo variable float32 estéreo: implementado como componente C++ con ring fijo, interpolación cúbica, ratio acotado y objetivo atómico.
- Controlador de reloj: componente aislado y fail-closed; exige validación explícita de ruta antes de TRACKING.
- Integración de producción: BLOCKED. El snapshot IVHC no contiene identidad de stream y el productor actual elige el primer PCM playback con tasa nominal. No prueba que el reloj corresponda al AudioTrack/PCM que consume el ASRC.
- Moto G85 altavoz/USB-C: NEEDS_DEVICE_PROBE. No se han ejecutado pruebas físicas ni se declara éxito.
- No hay todavía un feature flag de usuario conectado: el nuevo núcleo está compilado pero no invocado por la ruta de producción, por lo que queda inerte. No habilitarlo hasta resolver la ruta.

## Ruta detectada
Audio decoder/DSP Kotlin -> StereoAudioResampler.kt -> AudioTrack. En paralelo: hal_clock_producer.cpp -> mmap IVHC -> HardwareClockReader. No está demostrado que ambos extremos sean el mismo flujo. El nuevo núcleo aún no reemplaza el resampler Kotlin ni se invoca desde JNI.

## Contrato
PCM float32 estéreo intercalado; ratio input/output inicialmente 0.9990–1.0010; pendiente configurable por defecto 50 ppm/s; proceso sin heap/locks/syscalls. Datos no finitos se reemplazan por cero y se cuentan. El interpolador cúbico no garantiza por sí solo alias rejection de -100 dB: requiere filtro de banda limitada y medición.

## Pruebas y release
Se añadieron tests host para configuración, ratio, bloques variables, NaN/Inf y estados fail-closed. No se declara que estén verdes: no se ejecutó CMake/CTest ni build APK en esta sesión. THD+N, aliasing, rendimiento y prueba de estrés de 10 minutos pendientes. No publicar release de producción hasta que CI y Moto G85 completen el protocolo.

## Bloqueos siguientes
1. Añadir identidad de stream/ruta al contrato IVHC desde el dueño real de salida.
2. Integrar el núcleo en el seam PCM que escribe a AudioTrack y preservar bypass bit-transparente.
3. Añadir cola SPSC de ratio y logs ASRC-CTRL throttled.
4. Ejecutar CTest/build APK y corregir fallos.
5. Probar altavoz y USB-C DAC por separado durante 10 min; comparar off/on, deriva, underruns y artefactos. Bluetooth requiere prueba separada.


## Distinción de rutas (actualización 2026-10-24)

La ruta **de reproductor propio** ya cablea corrección de tasa por software: `IvannaBridgePlayer` aplica `StereoAudioResampler` al PCM decodificado, y el `AudioTrackBufferPll` calcula la corrección desde frames enviados menos frames consumidos en timestamps de ese mismo `AudioTrack`. La corrección se entrega a `StereoAudioResampler.setClockCorrectionPpm()`; si falla `getTimestamp()`, el control se reinicia y la corrección vuelve a cero. Esto es una ASRC software válida para el flujo que IVANNA reproduce y posee.

La pieza nativa `VariableAsrc` es un componente separado y todavía no sustituye a ese resampler en la ruta de reproducción. Tampoco se debe alimentar el PLL de `AudioTrack` con muestras PAL de otro flujo: son dominios/identidades distintos hasta que una integración demuestre lo contrario.

Por tanto, el bloqueo pendiente no es «encontrar un control ASRC físico»: es integrar una corrección software en el punto correcto si se busca controlar audio de sistema/terceros, y enlazar la telemetría PAL al propietario real del stream. Un daemon externo que abre otro PCM no equivale al flujo activo. La ruta de reproducción propia debe conservar unity-ratio/bypass si sus timestamps son inválidos; el efecto global requiere integración de HAL/efecto compartido y pruebas de dispositivo.


## Estimación matemática para la captura USB (2026-10-27; provisional)

**Separación crítica:** estas ecuaciones estiman magnitudes a partir del resumen de telemetría aportado; no convierten el reloj USB observado en reloj del AudioTrack propio ni demuestran que PAL/ALSA aplique ASRC. El controlador de producción debe usar únicamente timestamps y contadores emparejados del mismo stream.

### Modelo de frecuencia y deriva

Con tasa nominal f0 y tasa estimada fm:

~~~text
error_ppm = 1e6 * (fm / f0 - 1)
delta_frames_per_second = fm - f0
drift_ms_per_second = 1000 * (fm - f0) / f0
~~~

Si —y solo si— el campo reportado localSR=48000.2 Hz representa una estimación válida y comparable con el nominal de 48,000 Hz:
- Error estimado = 1e6 * (48000.2 / 48000 - 1) = +4.17 ppm.
- Exceso aproximado = 0.2 frame/s = 12 frames/min.
- Deriva acumulada sin corrección = 120 frames en 10 min, equivalente a 2.50 ms a 48 kHz.

Esto es una **estimación condicional**, no una medición certificada del oscilador USB: hay que confirmar la definición, ventana y unidad de localSR, y que la estimación esté referida al endpoint USB real. No aplicar automáticamente -4.17 ppm al reproductor IVANNA.

Si 0.999997 fuera un ratio aplicado, representaría (0.999997 - 1) * 1e6 = -3 ppm. El nombre del campo por sí solo no prueba que sea el ratio de ASRC; se conserva como dato sin atribución hasta identificar su productor.

### Control de cola en la ruta propiedad de IVANNA

Para el reproductor de archivos, se calcula la ocupación con contadores del mismo AudioTrack:

~~~text
q[k] = submittedFrames[k] - timestamp.framePosition[k]
q_target = max(1, floor(bufferCapacityFrames / 2))
e[k] = clamp((q[k] - q_target) / bufferCapacityFrames, -1, +1)
I[k] = clamp(I[k-1] + e[k] * dt, -4, +4) seconds
u_target_ppm = clamp(-(800 * e[k] + 120 * I[k]), -500, +500)
u[k] = u[k-1] + clamp(u_target_ppm - u[k-1], -25, +25)
step = inputRate / targetRate / (1 + u[k] * 1e-6)
~~~

La polaridad negativa es intencional: cola por encima del objetivo significa que el productor va adelantado, por lo que se reduce la tasa de salida del remuestreador. El límite de ±500 ppm y el slew de ±25 ppm por actualización son límites del controlador, no evidencia de que el reloj físico se haya corregido. Si falla el timestamp, la ruta debe volver a corrección cero.

### Lectura prudente de las métricas entregadas

- Jitter corregido sigma = 0.0085 ms (8.5 µs) frente a jitter raw sigma = 0.082 ms (82 µs): cociente aproximado 0.104, es decir, 89.6% menos desviación estándar según esos dos campos. No demuestra causalidad ni calidad de reloj hasta verificar que usan la misma población, unidades y método.
- “Timestamp corrected: no” solo describe esa capa/indicador; no descarta remuestreo en otra capa.
- Los contadores underruns/overruns de FastMixer no deben etiquetarse como fallos del endpoint USB sin confirmar su origen.

### Prueba inmediata, segura y reproducible

1. Compilar/instalar el APK producido por CI y probar el reproductor propio de IVANNA primero. Esta ruta ya tiene el lazo AudioTrackBufferPll → StereoAudioResampler.setClockCorrectionPpm(); el componente C++ VariableAsrc no es el actuador activo de ese reproductor.
2. Registrar, por intervalo monotónico, timestamp frame position/time, frames aceptados por el mismo track, capacidad, ocupación, correctionPpm, tasa de entrada/salida, route/device ID y resets por timestamp inválido. No combinar muestras de PAL con las de AudioTrack.
3. Hacer tres corridas de 10 min con la misma pista y ruta: corrección desactivada (baseline), corrección activa, y corrección activa con cambio de ruta USB desconectada/reconectada. Comparar deriva de ocupación, resets, discontinuidades, underruns reportados por la capa correcta y escucha A/B a nivel igualado.
4. Repetir por separado con DAC USB y Bluetooth; una mejora en el reproductor propio no demuestra mejora de audio de terceros ni corrección del oscilador remoto.
5. Gate: no activar el núcleo C++ nuevo ni declarar “USB ASRC validado” hasta pasar tests de ratio positivo/negativo, saturación, slew, bloques fragmentados, continuidad, entrada NaN/Inf, overrun y reset, además de build/CTest y prueba física.

**Resultado actual:** la estimación central es aproximadamente +4.17 ppm solo bajo la interpretación indicada; la corrección apropiada para la ruta IVANNA sigue saliendo del error de ocupación medido en su propio AudioTrack. La unión PAL/USB a esa ruta no está demostrada y queda explícitamente bloqueada para evitar corregir un reloj con muestras de otro dominio.
