# ADR-0002: Reloj de hardware y ASRC en la ruta de salida HAL

- **Estado:** Aceptado como contrato arquitectónico; integración en HAL propietario pendiente.
- **Fecha:** 2026-10-21
- **Alcance:** salida de audio Android, DAC interno y DAC USB cuando el HAL exponga posición/timestamp y un ASRC de razón variable.

## Contexto

Una aplicación puede estimar la cadencia de su propio `AudioTrack` y corregir el remuestreo de ese flujo. Eso no equivale a leer ni disciplinar el oscilador físico del DAC, ni controla otras aplicaciones. Para controlar el reloj de salida del dispositivo se necesitan dos elementos que solo conoce el propietario del stream HAL: una posición de frames asociada a un timestamp del driver y una interfaz real que acepte la razón del ASRC ya presente en esa ruta.

El repositorio contiene un servo de usuario para el reproductor propio y primitivas C++ reutilizables para una integración HAL. Estas primitivas no crean por sí solas una API propietaria ni prueban que el HAL del dispositivo las invoque.

## Decisión

1. Mantener separado el servo de `AudioTrack` del controlador del reloj de hardware del HAL.
2. Alimentar `HardwareClockTracker` exclusivamente con pares de frame-position/timestamp obtenidos de una misma consulta exitosa del HAL/driver. No sustituirlos por la hora de entrada del callback ni por marcas de tiempo tomadas de forma independiente.
3. Ejecutar `HalOutputStreamClockControl::controlTick()` en una cadencia de control acotada, fuera del callback de render/escritura de tiempo real.
4. Enlazar `ApplyRatio` al ASRC de razón variable que ya pertenezca a la ruta. No añadir una segunda conversión sin una decisión de arquitectura y una prueba de latencia/calidad.
5. Limitar la corrección a ±500 ppm, slew máximo de 25 ppm por actualización y adquisición de bloqueo tras tres muestras válidas.
6. Ante timestamp inválido/regresivo, discontinuidad, rechazo del ASRC o pérdida de lock, ordenar razón unitaria. Si el actuador no confirma la orden, marcar la razón como desconocida y deshabilitar la corrección.
7. Reportar de forma diferenciada «estimación», «razón solicitada», «razón aceptada» y «comportamiento medido». Ninguna de estas etiquetas demuestra por sí sola que el PLL físico se haya disciplinado.

## Componentes

- `app/src/main/cpp/include/audio_hardware_clock.hpp`: valida pares de posición/timestamp y estima la deriva.
- `app/src/main/cpp/include/audio_hardware_clock_rate_servo.hpp`: calcula una corrección acotada.
- `app/src/main/cpp/include/hal_output_rate_control.hpp`: contrato del actuador de razón variable.
- `hal_integration/ivanna_audio_clock_reporter.hpp`: helper de publicación que el HAL debe llamar con valores reales.
- `hal_integration/ivanna_hal_output_stream.hpp`: punto de integración reutilizable para consulta y actuador reales.
- `app/src/main/cpp/tests/test_audio_hardware_clock_bridge.cpp` y `test_hal_output_rate_control.cpp`: pruebas host de contratos y fallos simulados.

## Estado de implementación

Los helpers y las pruebas host están presentes. El código de integración en `hal_integration/` es un seam reutilizable, no evidencia de que el HAL Qualcomm/PAL del dispositivo ya lo invoque. Para declarar la integración física cerrada se requiere el código fuente del target HAL correspondiente o un cambio verificable en ese target, compilación de dicho target y telemetría en el dispositivo. El test de un ASRC simulado no sustituye esta evidencia.

El servo de `AudioTrack` del reproductor propio es un alcance distinto y no debe describirse como control global del DAC/HAL.

## Consecuencias

- Se puede probar en host el cálculo, el bloqueo, los límites, el rechazo del actuador y la vuelta a razón unitaria.
- Los DAC USB se deben configurar con una tasa anunciada por el dispositivo cuando Android exponga capacidades; un fallback conservador no prueba que la ruta física se haya activado.
- La liberación puede certificar el build y las pruebas que realmente ejecutó CI, pero no una calibración física no ejecutada.
- La integración se considera incompleta hasta que la API de reloj y el actuador del HAL de destino estén enlazados y verificados.
