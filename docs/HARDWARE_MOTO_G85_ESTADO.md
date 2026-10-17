# Integración de hardware y permisos — Moto G85 5G (estado a v2.5.64)

Base: auditoría de `main` en v2.5.61 (352). Alcance de esta versión: **capa de detección de capacidades + política de permisos**, cableadas a un primer consumidor real. No es la integración completa del prompt maestro; esta tabla dice exactamente qué está hecho y qué no.

**Limitación explícita:** no hubo acceso al Moto G85 durante este trabajo ni SDK de Android en el entorno. Nada de lo marcado «pendiente de prueba física» se midió en el teléfono. Los tests JVM de `PermissionPolicy` se compilaron y ejecutaron con Kotlin real (7/7); el código que usa APIs de Android lo compila el CI.

## Hallazgos de la auditoría (demostrados en el código)

| # | Hallazgo | Acción |
|---|---|---|
| 1 | `FOREGROUND_SERVICE_MEDIA_PLAYBACK` declarado, pero el único servicio (`PlaybackCaptureService`) usa solo `FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION` | **Quitado** |
| 2 | El micrófono ya se pide solo al usar una función (medición de sala, asistente); no hay petición al arrancar ni cámara en el manifest | Se conserva |
| 3 | Tras una denegación permanente el sistema no vuelve a mostrar el diálogo: la medición de sala solo decía «se necesita el permiso» | **Corregido**: `PermissionPolicy` + `PermissionGate` mandan a Ajustes con explicación |
| 4 | No existía una capa única de capacidades | **Añadida** `HardwareProbe` (arranque, sin permisos, tag `IvannaHW`) |
| 4b | La captura de sistema exigía `RECORD_AUDIO` pero se lanzaba desde 3 botones sin pedirlo antes (AudioRecord sin inicializar, fallo mudo); el asistente no releía el permiso tras volver de Ajustes | **Corregido en 2.5.63** |
| 5 | `POST_NOTIFICATIONS` no declarado; con servicio en primer plano en Android 13+ la notificación no se muestra sin él | **Pendiente** (requiere flujo de petición propio) |
| 6 | Permisos de sistema/protegidos (`READ_LOGS`, `PACKAGE_USAGE_STATS`, `CAPTURE_AUDIO_OUTPUT`, `BIND_AUDIO_EFFECT_SERVICE`, `MEDIA_CONTENT_CONTROL`) se declaran por el módulo Magisk | **No se tocan sin evidencia**; revisar uno a uno con el módulo instalado |
| 7 | El CI compila `assembleRelease` pero no ejecuta `testDebugUnitTest` | **Pendiente** (ya figuraba en el checklist DEH6) |
| 8 | «Fotoholografiananotomix» como arquitectura de app **no existe** en el código: solo `research/deh6_optics` (simulación Python, excluida del APK a propósito) | Ver abajo |

## Estado por función

| Función | Estado |
|---|---|
| Detección de sensores (acelerómetro, giroscopio, magnetómetro, proximidad, rotation vector) | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA |
| Detección de rutas de salida (altavoz, cable, USB, A2DP), frecuencia nativa, baja latencia declarada | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA |
| Soporte de captura PCM16 mono (48/44.1/16 kHz) sin capturar | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA |
| Detección de cámaras (solo lista; la app no las abre) | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA |
| Vibración y control de amplitud (existe interruptor de háptica de graves en la UI) | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA |
| Versión de OpenGL ES, Vulkan, tasa de refresco | PARCIAL (extensiones GL requieren contexto GL; no consultadas) |
| Política de permisos (`PermissionPolicy`) | IMPLEMENTADA Y PROBADA (7 tests JVM) |
| Gestión de revocación/denegación permanente en medición de sala | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA |
| Mismo gestor en captura de sistema (MediaProjection) y asistente de voz (v2.5.63) | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA |
| Audio Focus, `AudioDeviceCallback`, USB Audio | Ya existentes en el repo; no re-auditados a fondo en esta versión |
| Fotoholografía·Nanotomix (v2.5.64): bandas Bark reales → ancho estéreo acotado (±0.25 máx., slew 0.2/s) vía `nativeSetSpatialWidthDirect` → `StereoWidener`; escena 3D paramétrica; intensidad y bypass A/B | IMPLEMENTADA, PENDIENTE DE PRUEBA FÍSICA (lógica pura probada: 11 tests JVM; efecto audible solo con captura Ruta A activa) |
| Seguimiento por cámara / visión | NO SOPORTADA: la app no usa cámara; no se añade sin un algoritmo que la justifique |
| Profundidad, LiDAR, seguimiento ocular | NO SOPORTADA POR EL HARDWARE |
| Holograma óptico volumétrico en pantalla | NO SOPORTADA: una pantalla convencional no lo genera; DEH6 es simulación de host |
| Puente DEH6 → audio | BLOQUEADA POR DEPENDENCIAS: requiere contrato definido y A/B que demuestre mejora (ver `research/deh6_optics/docs/INTEGRATION_PLAN.md`) |
| Batería de pruebas sostenidas (latencia, CPU, térmica, interrupciones) | BLOQUEADA POR DEPENDENCIAS: exige el teléfono físico |

## Procedimiento de prueba física (Moto G85, a ejecutar por el usuario)

1. Instalar la APK del release v2.5.62. Abrir **sin** conceder nada: confirmar que no aparece ningún diálogo al arrancar.
2. `adb logcat -s IvannaHW` (o Termux con root): copiar las líneas «IvannaHW». Contrastar sensores/rutas con lo que el teléfono tiene de verdad; reportar cualquier «no detectado» inesperado.
3. CONTROL → medición de sala: (a) denegar → mensaje claro y el resto de la app funciona; (b) denegar de nuevo con «No volver a preguntar» y pulsar otra vez → debe abrir Ajustes; (c) conceder en Ajustes y volver → mide; (d) con la medición ya hecha, revocar el micrófono en Ajustes → la siguiente medición vuelve a explicar/abrir Ajustes.
4. Conectar/desconectar DAC USB-C y auriculares con la app abierta y repetir el paso 2 tras reiniciar la app (el sondeo se hace al arrancar).
5. Medidas de latencia, CPU, temperatura y batería: registrar con las herramientas del repo (`scripts/benchmark_device.sh`), con ruta, códec y frecuencia anotados. No se incluyen cifras aquí porque no se han medido.

## Siguiente

- Mostrar `HardwareProbe.last` en un panel de diagnóstico y atar a él qué controles se ofrecen.
- Decidir `POST_NOTIFICATIONS`.
- Añadir `testDebugUnitTest` al CI.
- Fotoholografía·Nanotomix en Ruta B (módulo): hoy la telemetría Bark solo existe con captura Ruta A; evaluar `rt_band_meter` como fuente.
- Medir en el Moto G85 el efecto del ancho adaptativo con A/B igualado en sonoridad (bypass incluido en la tarjeta).

## Prueba física de Fotoholografía·Nanotomix
1. SPATIAL AUDIO → tarjeta FOTOHOLOGRAFÍA. Sin captura activa debe decir «Sin señal» y no tocar el ancho.
2. Activar la captura (Ruta A) y reproducir música: la escena reacciona al espectro.
3. Activar «Modular el ancho»: «Ancho tuyo → aplicado» debe moverse, nunca más de ±25 % × intensidad, sin saltos.
4. Alternar «Bypass A/B» a mitad de una pista: el ancho vuelve exactamente al tuyo; comparar de oído a igual volumen.
5. Mover el slider de ancho de CONTROL/SONIDO con el efecto activo: la base nueva se respeta.
6. Registrar CPU/temperatura con el efecto 10 min encendido (`scripts/benchmark_device.sh`).
