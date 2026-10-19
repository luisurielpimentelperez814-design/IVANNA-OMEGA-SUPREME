# Propuesta de arquitectura: FotoHolo Wi-Fi sensing (CSI) y geometría acústica

## Qué demuestra la investigación

La investigación académica confirma que las variaciones de la señal Wi-Fi pueden utilizarse para reconocer algunas actividades/movimientos, estimar ocupación y realizar detección sin cámara. Un trabajo de 2025 estudió reconocimiento de cinco movimientos con CSI; otros estudios evalúan ocupación por habitación. Esto no significa que cualquier teléfono Android pueda extraer CSI ni que una red Wi-Fi normal produzca un escaneo 3D exacto de una casa o del cuerpo.

Referencias:
- Zhang et al., *A Bi-LSTM-based Wi-Fi CSI approach for non-intrusive human behavior recognition in smart buildings*, Energy and Buildings (2025), DOI 10.1016/j.enbuild.2025.116059.
- Jung et al., *Implementing Wi-Fi CSI-based room-level occupancy Estimation*, Journal of Building Engineering (2025), DOI 10.1016/j.jobe.2025.113155.
- de Armas et al., *EHUNAM, a WiFi CSI-based dataset for human and machine sensing*, Scientific Data (2025), DOI 10.1038/s41597-025-06238-4.
- *Time matters: Empirical insights into the limits and challenges of temporal generalization in CSI-based Wi-Fi sensing*, Internet of Things (2025), DOI 10.1016/j.iot.2025.101634.

## Objetivo técnico

Añadir un módulo experimental, opt-in y local-first para investigar CSI y combinarlo con sensores del propio teléfono. No afirmar capacidad antes de detectar y probar un camino de adquisición real.

### Fases

1. **Detección de capacidades**
   - Identificar versión Android, fabricante/modelo y APIs disponibles.
   - Comprobar si el dispositivo expone CSI mediante API pública/soportada o si se requiere hardware auxiliar (por ejemplo, un nodo CSI compatible).
   - No confundir RSSI de Wi-Fi estándar con CSI. RSSI puede ayudar a observar cambios generales, pero no sustituye CSI de alta resolución.
   - Si no existe adquisición compatible, informar “Este teléfono/router no admite esta medición” y no simular datos.

2. **Sesión de medición voluntaria**
   - Botón explícito “Iniciar medición Wi-Fi”.
   - Explicar qué se medirá, qué precisión se espera y si otras personas presentes podrían ser detectadas.
   - Pedir que todas las personas potencialmente detectables acepten participar; si no, limitar la sesión o no iniciarla.
   - Indicador visible, duración limitada, botón detener, sin servicio oculto ni ejecución permanente en segundo plano.
   - Procesamiento local por defecto y sin guardar trazas CSI crudas salvo consentimiento separado.

3. **Sensores del teléfono**
   - Combinar CSI real (si está disponible) con acelerómetro/giroscopio para compensar movimiento del teléfono; cámara/profundidad puede apoyar medidas de distancias en una sesión separada y visible.
   - RSSI, brújula o acelerómetro no deben presentarse como escaneo anatómico o plano arquitectónico.
   - Para geometría, mostrar rangos estimados, incertidumbre y procedimiento de calibración. Validar contra medidas de referencia.

4. **Modelos de estimación**
   - Tareas iniciales permitidas: presencia/ocupación por zona, movimiento grueso y cambios en el entorno.
   - No prometer reconstrucción anatómica detallada ni identificación biométrica. Cualquier investigación posterior de pose/respiración exige conjunto de datos apropiado, validación independiente y consentimiento específico.
   - No inferir salud, identidad ni atributos sensibles de asistentes.
   - Mostrar confianza, condiciones de fallo y fecha de calibración; abstenerse cuando el entorno cambie o la confianza sea baja.

5. **Uso acústico**
   - La estimación espacial solo aporta contexto a la calibración de audio; la respuesta de la sala se mide mediante micrófono/procedimiento acústico separado.
   - No cambiar automáticamente el volumen maestro. DSP dentro de límites, bypass, historial de cambios y reversión inmediata.

6. **Datos y consentimiento**
   - Preferencias separadas para descubrimiento local, adquisición CSI, micrófono, cámara/profundidad y adaptación DSP.
   - Revocar debe detener capturas y limpiar datos locales derivados si el usuario así lo solicita.
   - No transmitir identificadores de red, trazas CSI o datos corporales a la nube por defecto.
   - No escanear redes ajenas, eludir permisos del sistema ni activar medición sin una acción clara del usuario.
   - La autorización de una persona no equivale al consentimiento de todos los demás que puedan ser detectados.

## Criterios de aceptación

- Tests unitarios de estado de consentimiento, revocación y bloqueo por defecto.
- Prueba de dispositivo: confirmar que la API/hardware devuelve CSI auténtico; no usar datos ficticios para aparentar éxito.
- Dataset de evaluación separado de entrenamiento y escenarios/horarios distintos para medir generalización.
- Matriz de precisión, falsos positivos, falsos negativos, sensibilidad a posición del router/personas y deriva temporal.
- Comparación geométrica con mediciones de referencia y rango de error visible.
- Revisión de seguridad, privacidad y términos de cada plataforma.
- CI Android verde y pruebas en hardware antes de marcar el módulo como estable.

## Estado

La pantalla de consentimiento ahora incluye una opción para probar mediciones Wi-Fi en equipos compatibles y explica las limitaciones en lenguaje común. Esta documentación es un plan de ingeniería y una especificación de investigación; no significa que el repositorio ya contenga un receptor CSI, un modelo entrenado, escaneo anatómico ni calibración espacial validada.


## Implementación por niveles con Magisk/root

La aplicación incluye ahora una ruta de sondeo opt-in con dos vías:
1. API pública de Android cuando se cumplen los permisos y servicios requeridos.
2. Backend Magisk/root que intenta ejecutar `iw dev` y un escaneo de la interfaz detectada mediante nombre validado. Se limita a metadatos estándar de redes y RSSI, no recopila ni afirma CSI.

La sesión debe ser iniciada explícitamente tras consentimiento y detenida por la pantalla propietaria cuando deja de estar en primer plano. Los resultados se etiquetan con su procedencia; los rechazos, permisos ausentes o resultados no nuevos no se convierten en mediciones inventadas. El backend root no modifica configuración del router ni intenta cambiar el modo del controlador.

### Siguiente nivel para CSI real

Para cada familia de hardware se necesita un adaptador mantenido y probado que exponga muestras CSI reales: chipset, versión de kernel/driver, firmware, bandas soportadas, API del proveedor, formato de muestras, timestamps y métricas de calidad. La detección debe ser por capacidad verificada y no por marca/modelo inferido. No se debe tratar la presencia de `iw`, root, Wi-Fi RTT o RSSI como prueba de que CSI está disponible.

La compatibilidad multi-hardware se consigue con adaptadores intercambiables y degradación transparente, no prometiendo que todos los módems Android puedan exponer CSI con software únicamente.
