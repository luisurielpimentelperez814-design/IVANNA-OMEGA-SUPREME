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
