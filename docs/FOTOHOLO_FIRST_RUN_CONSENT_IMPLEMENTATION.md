# FotoHolo / IVANNA — consentimiento acústico de primer inicio

## Implementado en esta rama

- La aplicación muestra una divulgación antes de entrar a `OmegaApp` en el primer inicio.
- El usuario puede continuar con las funciones opcionales desactivadas.
- Preferencias separadas para calibración con micrófono, descubrimiento local de dispositivos, medición geométrica por cámara/sensor y DSP adaptativo.
- La pantalla explica los límites de Wi-Fi, la necesidad de permisos del sistema y la prohibición de asumir acceso universal al audio de plataformas.
- La pantalla no solicita permisos de Android de forma prematura. Cada permiso runtime o sesión MediaProjection debe solicitarse cuando el usuario abre la función correspondiente.

## Límites que no se deben ocultar

La pantalla registra preferencias en SharedPreferences, pero **el registro por sí solo no demuestra que todos los consumidores existentes respeten esos indicadores**. Antes de habilitar producción hay que conectar cada preferencia con su punto de entrada real (calibración, descubrimiento de red, cámara/profundidad y controlador DSP), añadir controles de revocación en Ajustes y probar que revocar una preferencia detiene la función y elimina datos asociados según corresponda. No presentar esta rama como una auditoría de permisos terminada.

## Contrato de privacidad obligatorio

1. Denegación por defecto para micrófono, cámara/geometría, descubrimiento local y cambios autónomos DSP.
2. Permiso Android solicitado justo antes del uso, con indicador visible, parada explícita y manejo de denegación permanente.
3. Sin micrófono ambiental oculto ni grabación persistente. Calibración voluntaria; procesamiento local por defecto; no subir audio crudo por defecto.
4. Wi-Fi/red local puede ayudar a descubrir dispositivos compatibles autorizados, pero no medir de forma fiable dimensiones físicas, respuesta acústica ni anatomía.
5. No escanear dispositivos no relacionados ni identificar asistentes. La personalización espacial corporal, si se añade, será auto-calibración voluntaria del propio usuario, no un sondeo de terceros.
6. La cámara/profundidad debe ser una sesión explícita con vista previa, incertidumbre, cancelación y eliminación; no inferir anatomía desde Wi-Fi.
7. DSP adaptativo: opt-in efectivo verificado por el controlador, límites de headroom/estabilidad, no elevar automáticamente el volumen maestro, registro de cambios, bypass y deshacer inmediato.
8. Metadatos musicales solo mediante APIs o interfaces autorizadas y de acuerdo con los términos de cada proveedor. No extraer streams, eludir DRM ni enviar contenido protegido a modelos sin autorización.
9. No enviar datos de sensores identificables a servicios remotos salvo que sea estrictamente necesario, explicado y autorizado por separado.
10. Añadir en Ajustes un panel para revisar, revocar y borrar consentimientos y calibraciones guardadas.

## Validación obligatoria antes de afirmar “terminado”

- Compilar Kotlin/Compose y ejecutar CI completa.
- Pruebas de primera instalación, actualización desde versión previa, reinicio y ausencia de pantalla repetida tras aceptar.
- Probar “Continuar sin funciones opcionales”.
- Confirmar que cada preferencia bloquea realmente el consumidor correspondiente y que revocarla detiene la captura.
- Probar micrófono denegado una vez, denegado permanentemente, revocado en Ajustes y MediaProjection cancelado.
- Probar rutas integradas, Bluetooth, USB-C/USB Audio y jack cuando haya hardware disponible; no atribuir respuesta física no medida.
- Validar calibración con referencia conocida y reportar incertidumbre. El micrófono del teléfono no es un sonómetro calibrado por defecto.
- Revisión de política y derechos por proveedor para cada integración musical.

## Capacidad real, no promesa

Este cambio agrega una puerta de divulgación/elección de primer inicio. No instala por sí solo sensores inexistentes, no entrena una red neuronal, no habilita captura universal de apps de música y no hace posible medir anatomía o dimensiones con Wi-Fi. La corrección acústica solo puede afirmarse tras pruebas instrumentales y validación de los consumidores reales.
