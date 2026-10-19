# Auditoría integral para 5D perceptual — 2026-10-19

## Alcance y criterio

Revisión estática del árbol `main` observado durante esta auditoría. Se inspeccionaron README, ficha de ingeniería, datos HRTF, `RoomGeometryConfig.hpp`, `SupremeFiveAxesScreen.kt`, `SupremeAxesPrefs.kt`, pruebas nativas de los cinco ejes, auditoría de cableado y configuración de CI/versionado.

Esta auditoría **no equivale** a ejecutar CI, instalar el APK, medir el sistema PZ del usuario, ni validar audio en un dispositivo físico. No se declara una versión 5D terminada ni una mejora audible sin esas evidencias.

## Dictamen ejecutivo

IVANNA contiene una base real de procesamiento espacial, mediciones de sala, HRTF/HRIR, geometría de escena, WFS experimental, controles de cinco ejes, háptica y un sondeo Wi-Fi. La existencia de estos componentes no demuestra que todos estén activos simultáneamente en la ruta de reproducción ni que el resultado sea una reconstrucción física 5D.

**Definición técnica necesaria:** “5D literal” no es una quinta dimensión física que pueda crearse por software de audio. El objetivo implementable es una experiencia **5D perceptual**, con cinco grados de control independientes, medibles y sincronizados. Propuesta de ejes verificables:
1. Azimut: izquierda/derecha y posiciones laterales.
2. Elevación: arriba/abajo, dependiente de HRTF individual y contenido.
3. Distancia/profundidad: señales directas, nivel, filtrado y primeras reflexiones coherentes.
4. Movimiento: trayectorias dinámicas con continuidad temporal, seguimiento de cabeza cuando exista sensor compatible.
5. Envolvimiento temporal/táctil: envolvente, ambiente y háptica sincronizada únicamente cuando el dispositivo la soporte.

Estos ejes son dimensiones perceptuales de control, no dimensiones espaciales físicas adicionales. No se deben llamar “medidos” si solo son valores de configuración o síntesis.

## Hallazgos por prioridad

### P0 — bloquea una afirmación de 5D terminado

- **No hay certificación física:** la ficha de ingeniería reconoce que la validación en dispositivos reales es limitada. CPU, latencia de extremo a extremo, underruns, clipping, temperatura, salida AUX/USB-C y mejora audible requieren medición en hardware.
- **Estado solicitado frente a estado efectivo:** la auditoría de cableado del 2026-10-18 ya identificaba la necesidad de read-back y confirmación real. Los toggles y llamadas JNI no prueban por sí mismos que el motor haya aplicado el parámetro.
- **Telemetría de cinco ejes:** `SupremeFiveAxesScreen.kt` consulta wrappers JNI cada 250 ms, pero su modelo `SupremeAxesLiveTelemetry` contiene valores iniciales concretos de aspecto medido (por ejemplo, retardo submuestra, derivada de fase, espigas SNN, notch e ITD). Si una lectura falla y un wrapper devuelve el valor de respaldo, la UI puede mostrar una cifra plausible sin indicar que la muestra no es válida. Hay que auditar cada `safeGet...`, definir validez/antigüedad/proveedor y mostrar “sin lectura” cuando no exista dato real.
- **HRTF individual:** el repositorio incluye datasets medidos y un camino de fallback sintético. La documentación HRTF especifica que el camino actual de carga usa una rebanada azimutal del dataset para el recurso predeterminado; por tanto, la presencia de SOFA completo no garantiza elevación personalizada completa. Hace falta distinguir sujeto medido, interpolación, fallback y calibración del usuario en la telemetría.
- **Wi-Fi no es reconstrucción 3D/5D:** el sondeo RSSI observa redes y fuerza de señal. RSSI/RTT estándar no equivalen a CSI; no se debe usar como coordenada física ni como disparador directo de cambios DSP. La geometría Wi-Fi requiere hardware/proveedor CSI compatible, calibración y evaluación aparte.
- **PZ / geometría:** `RoomGeometryConfig.hpp` contiene un layout de referencia Sony MHC-PZ1D de 7 × 4 × 4 m (112 m³), además de un layout por defecto diferente. El changelog histórico también contiene otra geometría de 112 m³ (8 × 5 × 2.8 m). Estos son presets de configuración, no evidencia de que se haya medido la sala actual ni de que el sistema PZ concreto esté calibrado. Antes de activar corrección automática, unificar la fuente de verdad del preset y pedir/verificar dimensiones, posiciones y respuesta impulsional reales.
- **Un solo espacializador efectivo:** mantener arbitraje explícito entre HRTF binaural, HOA, WFS, ensanchador estéreo, campo temporal y cualquier ruta de daemon/Magisk. Doble procesamiento puede crear filtro peine, ecos o pérdida de localización.

### P1 — ingeniería necesaria para cerrar la ruta

1. Crear una tabla de contrato por cada eje: UI → validación/rango → estado persistido → JNI → motor/daemon → confirmación/read-back → telemetría válida.
2. Reemplazar valores por defecto con apariencia de medición por un estado explícito de validez; nunca mostrar cifras sintéticas como telemetría real.
3. Definir un único objeto de escena 5D y un solo arbitro de salida. Cada eje debe tener unidad, rango, fuente, timestamp/validez y política de degradación.
4. Separar los perfiles de auriculares, USB-C DAC, AUX y altavoz. No asumir que una HRTF binaural funciona igual en altavoces físicos.
5. Usar calibración de sala basada en captura real; conservar estado anterior y revertir si la medida es inválida o la mejora no supera el criterio definido.
6. Añadir pruebas de integración para parámetros de los cinco ejes, persistencia/reinicio, ausencia de JNI, daemon desconectado, lectura obsoleta, revocación de permisos y conflictos de ruta.
7. Ejecutar CTest, ASan/UBSan, comprobaciones estáticas, build Android, inspección APK/ZIP y verificación de hashes sobre el mismo SHA de `main`.
8. Ejecutar protocolo físico repetible en el teléfono objetivo y la configuración PZ real: latencia, respuesta en frecuencia, ITD/ILD, true peak/THD, cortes, consumo/temperatura y pruebas A/B ciegas.

### P2 — criterios de aceptación del objetivo 5D perceptual

- Cada uno de los cinco ejes puede activarse y modificarse de forma independiente, sin que un eje sobrescriba silenciosamente a otro.
- La interfaz muestra “activo”, “degradado”, “no disponible” o “sin medición” según confirmación real del motor, no según el toggle.
- La escena mantiene continuidad de fase y amplitud durante cambios y no añade un segundo espacializador inadvertido.
- La HRTF utilizada y su origen quedan identificados; fallback y personalización no se confunden con mediciones del usuario.
- Si falla la medición o la ruta de salida, se desactiva solo la función dependiente, se mantiene el audio seguro y se informa al usuario.
- La build y los artefactos corresponden al mismo commit, con pruebas verdes y hashes verificables.
- La mejora perceptual solo se afirma tras pruebas A/B controladas; los tests de host validan código, no percepción humana.

## Estado de las pruebas disponibles en el repositorio

`telemetry/ctest_summary.json` contiene una instantánea de 414/414 pruebas nativas aprobadas. Esto es evidencia útil de esa instantánea, pero no sustituye una ejecución CI asociada al SHA auditado, ni valida Android en hardware. No se certifica en este documento que la release actual esté publicada o que la compilación más reciente esté verde.

## Orden de ejecución recomendado

1. Cerrar primero telemetría veraz y confirmación del estado efectivo.
2. Auditar y fijar la única ruta DSP espacial activa, con tests de regresión.
3. Unificar el preset PZ y separar claramente valores de referencia de medidas reales.
4. Integrar y verificar los cinco ejes perceptuales con degradación segura.
5. Ejecutar CI y pruebas de instalación; después, medir el equipo físico.
6. Publicar una versión nueva solo cuando las puertas automáticas y los artefactos coincidan con el mismo commit.

## Conclusión

El repositorio contiene piezas relevantes para construir una experiencia 5D perceptual, pero el criterio magistral no es sumar más módulos: es demostrar una única cadena de audio coherente, calibrada, observable y reversible. Hasta cerrar P0 y completar las pruebas físicas, la descripción correcta es **base 3D espacial con cinco ejes experimentales en integración**, no “5D literal” certificado.
