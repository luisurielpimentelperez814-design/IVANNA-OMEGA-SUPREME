# ACTA OFICIAL DE INGENIERÍA — Inserción del reloj de hardware y servo ASRC en HAL

- **Código:** ACTA-ENG-2026-10-HAL-CLOCK-01
- **ADR relacionado:** ADR-0002 — Hardware Audio Clock & ASRC HAL
- **Estado:** diseño y contrato de control documentados; integración física en el HAL del dispositivo pendiente de evidencia.
- **Objetivo:** disciplinar la relación entre los frames de salida y el reloj de hardware usando el ASRC de razón variable que ya exista en la ruta real, sin introducir resamplers redundantes ni modificar el audio desde un hilo de tiempo real no propietario del hardware.

## Decisión arquitectónica

La medición de reloj y el servo pertenecen junto al flujo de salida del HAL. Un efecto insertado en AudioFlinger o una estimación basada únicamente en la cadencia de callbacks de la app no demuestra ni controla el oscilador físico del DAC.

La integración se divide en un adaptador de salida con dos dependencias verificables: (1) una muestra emparejada y monotónica de posición de frames y timestamp del mismo dominio de reloj; (2) un callback que ordene la razón al ASRC real de la ruta. Ningún offset, layout de estructura privada PAL, identificador de parámetro o firma propietaria debe adivinarse.

## Contrato del servo

- Exigir tres muestras válidas consecutivas antes de habilitar corrección.
- Limitar la corrección a ±500 ppm y la variación a 25 ppm por actualización.
- Rechazar timestamps ausentes, regresivos o discontinuos, tasas no finitas/no positivas y deriva fuera de rango.
- Ante pérdida de bloqueo, muestra inválida o rechazo del actuador, deshabilitar la corrección y solicitar razón unitaria (1.0).
- Si no puede confirmarse que el actuador aceptó la razón unitaria, reportar estado desconocido/deshabilitado; no afirmar que el hardware quedó corregido.
- Ejecutar la cadencia de control fuera del callback de render/escritura de tiempo real. El camino de audio no debe asignar memoria ni bloquearse.

## Evidencia y límites actuales

El repositorio contiene las primitivas `HardwareClockTracker`, `HardwareClockRateServo` y `HalOutputRateControl`, junto con pruebas de contrato. La integración Qualcomm PAL tiene un seam, pero la evidencia inspeccionada no identifica todavía de forma concluyente la consulta de timestamp emparejada con frames ni el actuador de razón variable de la implementación propietaria del Moto G85.

Por tanto, esta acta **no certifica** que el HAL del teléfono esté modificado, que el PLL físico esté disciplinado ni que el DAC acepte la razón solicitada. La corrección debe permanecer deshabilitada hasta validar ambos callbacks con evidencia de fuente/símbolos y prueba en dispositivo.

## Plan de verificación obligatorio

1. Inspeccionar la implementación exacta de PAL/HAL y la ruta de salida activa en el dispositivo objetivo.
2. Identificar y documentar la API real para obtener frame position y timestamp correlacionados.
3. Identificar y documentar la API real del ASRC existente, rango de razón, unidad y resultado de aceptación.
4. Ejecutar pruebas host para lock, dirección de corrección, límites, slew, timestamp inválido, reset y rechazo del actuador.
5. Compilar el target Android/HAL correspondiente; un build de la app no equivale a compilar el HAL propietario.
6. En Moto G85, registrar telemetría antes/después, razón solicitada/aceptada, underruns y estabilidad; confirmar con una ruta de medición independiente cuando sea posible.
7. Mantener la razón en 1.0 y reportar «no disponible» si falta cualquier evidencia de reloj o actuador.

## Matriz de aceptación

| Puerta | Evidencia requerida | Resultado permitido |
|---|---|---|
| Contrato C++ | pruebas automatizadas en host | servo acotado, fail-closed |
| PAL timestamp | fuente/API real y muestras emparejadas | reloj observable |
| Actuador ASRC | API real y confirmación de aceptación | razón comandable |
| Build del target | compilación del HAL/target que contiene el seam | integración compilable |
| Prueba en dispositivo | telemetría y mediciones reproducibles | integración física validada |

## Control de cambios

La inserción física se considera completada únicamente cuando todas las puertas anteriores tienen evidencia. No se debe etiquetar un release como «HAL ASRC activo» mientras el seam siga sin enlazar a la consulta real de reloj y al actuador real.
