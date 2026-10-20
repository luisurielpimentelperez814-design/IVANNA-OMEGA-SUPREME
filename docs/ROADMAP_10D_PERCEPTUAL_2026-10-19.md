# Roadmap de implementación 10D perceptual — 2026-10-19

## Propósito y lenguaje técnico

“Googleplex” se interpreta como una meta de escala, cobertura y rigor, no como una capacidad física literal. “10D” se define aquí como diez ejes perceptuales/operativos independientes y verificables dentro de una escena audiovisual. No son diez dimensiones físicas ni garantizan por sí mismos una experiencia superior. Cada eje debe tener un contrato de control, estado efectivo confirmado, degradación segura y prueba.

## Modelo de diez ejes propuesto

1. **Azimut** — dirección horizontal de cada fuente.
2. **Elevación** — dirección vertical dependiente de HRTF/altavoces y contenido.
3. **Distancia** — nivel, espectro y relación directo/reverberante calibrados.
4. **Movimiento** — trayectorias temporales suaves y continuidad.
5. **Rotación de cabeza** — solo con sensor compatible y datos de orientación válidos.
6. **Personalización HRTF** — sujeto/dataset identificado; fallback declarado.
7. **Geometría de sala** — dimensiones y posiciones medidas, no solo preset.
8. **Campo reverberante** — respuesta impulsional/T60 medida y límites de seguridad.
9. **Envolvimiento y separación de fuentes** — coherencia de fase, canales y control de artefactos.
10. **Sincronía táctil/visual** — háptica y visualización solo en dispositivos compatibles, con límites de latencia medidos.

Los ejes 1–4 forman la base espacial perceptual. Los ejes 5–10 son capacidades opcionales: si faltan sensores, datasets, canales o hardware, la interfaz debe marcarlos “no disponible” y no simular una confirmación.

## Fase 0 — Seguridad de la verdad del sistema (P0)

- Eliminar los valores de respaldo que se presentan como telemetría real. Los getters de diagnóstico deben distinguir valor válido, fallo, librería ausente y dato obsoleto.
- Cambiar la UI de “ON” por estados explícitos: solicitado, aplicado/confirmado, degradado, no disponible y error.
- Los fallos de arbitraje SHM deben ser cerrados por defecto: no informar adquisición/liberación exitosa cuando el puente está ausente o falla.
- No usar RSSI Wi-Fi como coordenada acústica. Exigir proveedor compatible, calibración y confianza antes de cualquier inferencia espacial.
- Mantener ruta de audio segura cuando falle un sensor, un bridge, un dataset o el daemon.

## Fase 1 — Fuente única de verdad de escena

Definir un modelo versionado de escena que contenga:
- coordenadas y unidades SI;
- ruta de salida y formato (auriculares, AUX, USB DAC, altavoces);
- HRTF y procedencia;
- estado de calibración y timestamp;
- configuración de cada eje;
- confirmación/read-back del motor;
- estado de degradación y motivo.

Un solo arbitro decide la ruta DSP activa. HRTF binaural, HOA, WFS, ensanchamiento estéreo y campo temporal no deben encadenarse accidentalmente. Las transiciones deben ser suaves y con pruebas anti-click.

## Fase 2 — Cableado extremo a extremo

Por cada control y eje verificar: evento UI → validación → persistencia → JNI/servicio → motor/daemon → read-back → UI → prueba de regresión. Inventariar rutas de navegación y callbacks vacíos; ningún control se considera terminado solo porque compila.

## Fase 3 — Calibración real de PZ y sala

- Resolver el conflicto entre el layout por defecto y los presets históricos Sony MHC-PZ1D.
- Presets etiquetados como “referencia” hasta que se midan dimensiones, posiciones, respuesta impulsional y nivel.
- Captura de micrófono con permisos explícitos, control de calidad de señal y reintento; nunca aplicar calibración basada en una captura inválida.
- Guardar antes/después, límites y mecanismo de reversión.

## Fase 4 — HRTF y elevación

- Registrar qué dataset y sujeto están activos, tasa de muestreo, rejilla azimut/elevación y método de interpolación.
- Distinguir HRIR medida, interpolada y sintética.
- Probar ambos oídos, continuidad angular, ITD/ILD, normalización, latencia y ausencia de NaN/denormales.
- No afirmar personalización anatómica si no existe una medida válida del usuario.

## Fase 5 — Rendimiento y pruebas

En cada SHA candidato:
1. Compilación Android y validación de APK.
2. Build nativo ARM64 y validación de ZIP/Magisk si corresponde.
3. CTest y pruebas de contrato.
4. ASan/UBSan y puertas estáticas.
5. Pruebas de persistencia, librería JNI ausente, daemon desconectado, permisos denegados, sensores obsoletos y salida no compatible.
6. Verificar que APK, ZIP, manifiestos, changelog y release pertenecen al mismo commit.
7. No cambiar la versión de release hasta que el conjunto pase.

## Fase 6 — Evaluación física y perceptual

Usar protocolo repetible en hardware objetivo:
- latencia extremo a extremo y jitter;
- underruns y estabilidad térmica;
- respuesta en frecuencia, true peak, clipping y distorsión;
- coherencia ITD/ILD y localización por ángulo;
- A/B ciego con volumen igualado y criterios preregistrados;
- registrar modelo de teléfono, versión Android, salida de audio, auriculares/altavoces y sala.

Las pruebas sintéticas de host prueban propiedades del código, no la percepción humana. Los resultados deben etiquetarse como host, dispositivo, laboratorio o usuario.

## Criterios de aceptación 10D

- Diez ejes documentados, con unidades/rangos, independencia demostrada y dependencias de hardware explícitas.
- Cada control tiene read-back o una indicación honesta de “no confirmado”.
- Ningún dato ficticio se presenta como medición.
- No hay doble espacialización no intencional ni regresión de clipping, cortes o latencia.
- Las pruebas automatizadas pasan en el commit exacto y los artefactos son reproducibles/identificables.
- Las pruebas perceptuales apoyan cualquier afirmación comercial de mejora.
- Si no hay soporte, el eje se degrada de forma visible y segura.

## Estado de inicio

Este archivo define el plan y la puerta de aceptación; no afirma que las fases estén implementadas. Prioridad inmediata: corregir telemetría de respaldo y semántica de fallo de los puentes antes de ampliar procesamiento. Después cerrar una única ruta DSP, fijar geometría y HRTF, integrar ejes, ejecutar CI y validar hardware. La etiqueta “10D” solo debe publicarse como capacidad verificada cuando pase los criterios anteriores.


## Extensión transversal: refinamiento SAFMUS del hardware del dispositivo

Esta extensión no crea un undécimo eje perceptual ni un segundo motor: SAFMUS supervisa las capacidades físicas que ya utiliza IVANNA, y cada subsistema existente conserva la propiedad de su sesión y su ruta de procesamiento.

### Recursos cubiertos

- **Cámara / FotoHolo / ARCore:** consultar capacidades Camera2, respetar al propietario de la sesión, mantener coherencia temporal entre imagen, pose y profundidad, y ajustar solo modos anunciados por el dispositivo. No reconfigurar una sesión ARCore desde un segundo controlador.
- **Micrófono / captura / medición:** usar exclusivamente una sesión autorizada, respetar el permiso de micrófono y los indicadores de privacidad, comprobar formato/ruta/efectos activos y evitar encadenar cancelación de ruido, AGC o supresión duplicados.
- **Altavoz integrado / cable / USB / Bluetooth:** arbitrar una única ruta DSP, consultar endpoint y formato reales, evitar doble espacialización/virtualización/reverb y aplicar perfiles específicos solo cuando exista evidencia válida de la ruta y, para corrección física, medición real.
- **Otros recursos compatibles:** sensores, GPU, memoria y aceleración solo se aprovechan cuando existe una API soportada, presupuesto medido y reversión segura. El sondeo de capacidades no implica que el hardware esté optimizado.

### Gate común ya añadido a SAFMUS

El kernel compartido expone una política sin dependencias Android que exige capacidad detectada, permiso vigente, sesión propietaria confirmada, telemetría fresca, configuración soportada, presupuesto de recursos válido y rollback disponible. Después de aplicar un cambio, la optimización solo se considera confirmada si el read-back efectivo coincide. Las pruebas JVM cubren ausencia de cada precondición y desajuste de read-back; la auditoría de cableado de CI comprueba que estas puertas y pruebas permanezcan presentes.

**Límite de implementación:** la política compartida y sus pruebas son una base de seguridad; no significan que los HAL de cámara/micrófono, el firmware del fabricante ni todas las rutas de altavoz ya reciban ajustes. Cada adaptador físico debe integrarse y probarse por separado con APIs permitidas, y la optimización debe permanecer desactivada si no existe read-back fiable. Ninguna afirmación de mejora física se acepta sin medición en el dispositivo.
