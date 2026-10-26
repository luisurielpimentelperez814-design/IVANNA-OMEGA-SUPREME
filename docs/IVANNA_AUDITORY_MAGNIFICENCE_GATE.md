# IVANNA Gate de Magnificencia Auditiva

Estado inicial: protocolo y herramienta de medición de WAV. No representa una certificación del Moto G85 ni de ninguna ruta física.

## Estados obligatorios

- **GREEN / AUDITORY_MAGNIFICENCE_VALIDATED**: pasan todas las puertas críticas del software y existen evidencias de las rutas físicas incluidas en el alcance; se documenta una comparación perceptual nivelada para las mejoras que se afirman.
- **AMBER / PARTIAL_EVIDENCE**: pasan pruebas de software, pero faltan pruebas de dispositivo, ruta, captura o escucha controlada. No declarar magnificencia alcanzada.
- **RED / BLOCKED**: falla una prueba crítica, hay regresión, clipping, cortes, estado DSP no aplicado, artefactos inválidos o resultados inconsistentes.
- **NOT_TESTED**: no existe evidencia suficiente. Nunca convertir ausencia de datos en PASS.

No se permite promediar una falla crítica con puntuaciones altas de otras áreas. Las condiciones de aprobación deben ser por función y por ruta, y las mediciones deben guardar el umbral, valor, unidad, resultado, herramienta, commit y procedencia.

## Herramienta de captura WAV

`scripts/ivanna_audio_certify.py` analiza WAV PCM entero de 8/16/24/32 bits. Informa frecuencia de muestreo, canales, duración, RMS/peak/DC por canal, clipping, balance RMS y, opcionalmente, residual/THD+N aproximado de un tono conocido y comparación SNR contra una referencia alineada.

Uso:
- `python3 scripts/ivanna_audio_certify.py capture.wav --json-out capture.json`
- `python3 scripts/ivanna_audio_certify.py capture.wav --tone-hz 1000 --json-out tone.json`
- `python3 scripts/ivanna_audio_certify.py after.wav --reference before_aligned.wav --json-out comparison.json`
- `python3 -m unittest tests.test_ivanna_audio_certify -v`

El SNR comparativo exige muestras alineadas y la misma frecuencia de muestreo y cantidad de canales. El THD+N estimado incluye toda la distorsión de la cadena de captura; no es certificación de laboratorio por sí solo. El analizador de WAV no determina por sí mismo el enrutamiento de Android, el códec Bluetooth negociado, la latencia de extremo a extremo ni el nivel acústico SPL.

## Matriz de cobertura

| Dominio | Evidencia necesaria |
|---|---|
| EQ, tono y bypass | Null/SNR contra referencia, barrido de respuesta, margen antes de clipping |
| Limiter, compresor, excitador | Barridos de nivel, ganancia aplicada, THD+N con seno limpio y recuperación transitoria |
| FIR, resampler y ASRC | Rizado de banda pasante, rechazo fuera de banda/alias, respuesta a impulso y deriva |
| HRTF, ITD y espacial | Impulso por canal, ITD medido, respuesta por frecuencia y escucha controlada |
| Bluetooth | Dispositivo activo, códec/tasa negociados con evidencia, estabilidad larga, cortes y deriva |
| DAC USB-C | Ruta identificada, cambios de sample rate, captura de salida y balance de canales |
| Efectos system-wide | Capturas independientes de reproductor propio y aplicaciones de terceros, ruta de efecto comprobada |
| Daemon, SHM y control | Petición-respuesta, generación/estado aplicado, reconexión, estrés y errores recuperables |
| FotoHolo/AR acústico | Exportación íntegra, evidencia de tracking y límites de incertidumbre; no presentar LiDAR sintético como medida física |
| IA y presets | Intención → parámetros validados → confirmación del estado realmente aplicado |
| Termal y rendimiento | Prueba sostenida, temperatura, CPU, latencia, underruns y cambios de governor |
| Release | Commit exacto, versión, SHA-256 de APK/ZIP, integridad de ELF/ABI y manifiesto de compilación |

## Secuencia por ruta

Repetir separadamente en altavoz, Bluetooth A2DP y DAC USB-C, si están disponibles. Para cada caso, conservar baseline y IVANNA activa con la misma fuente, volumen, dispositivo y configuración. Ejecutar tono de prueba a nivel seguro, barrido logarítmico, silencio/ruido, voz y música de referencia. Hacer al menos tres repeticiones y adjuntar las capturas originales, JSON, logs, identidad de ruta y SHA del build. Medir latencia con un loopback o estímulo/captura timestampados; no inferirla de tamaño de bloque.

Las afirmaciones de mejora perceptual requieren comparación A/B con nivel igualado y, cuando sea posible, a ciegas. Mayor volumen, graves o anchura aparente no prueban mayor fidelidad.

## Política de publicación

Una release solo puede llamarse **build-verified** si el mismo commit pasa tests nativos, JVM, build Android y verificación de APK/ZIP. Solo puede llamarse **device-validated** para las rutas con evidencia real adjunta. Solo puede llamarse **acoustically validated** con una cadena de medición apropiada y descrita. CI sintético no certifica el teléfono físico ni el receptor Bluetooth remoto.

Al inicio, el estado correcto de hardware es NOT_TESTED hasta que existan informes reales del Moto G85. El Gate debe degradarse a AMBER o RED automáticamente si faltan evidencias o fallan las puertas críticas.
