# FotoHolo + IVANNA — investigación Wi-Fi Sensing para adaptación acústica

Fecha de revisión: 2026-10-18

## Objetivo del producto

Usar cambios físicos medidos en el canal Wi-Fi como fuente contextual auxiliar para la experiencia de audio de IVANNA: detectar que el entorno parece haber cambiado, solicitar una nueva calibración acústica autorizada y elegir un perfil DSP solo cuando las mediciones acústicas lo respalden. La ruta de producción se limita a la adaptación del audio; no se diseña para vigilar personas ni inferir anatomía.

## Conclusiones de investigación

1. Wi-Fi Sensing es un campo real y estandarizado. IEEE 802.11bf-2025 es una enmienda publicada que amplía capacidades MAC/PHY para WLAN sensing en bandas sub-7 GHz y determinadas bandas de 60 GHz. No significa que todo router o teléfono existente exponga CSI automáticamente.
2. CSI es más informativo que RSSI, pero no es una fotografía. CSI puede representar amplitud/fase por subportadora y sus variaciones; depende de chipset, firmware, ancho de banda, antenas, geometría, sincronización, multitrayecto y calibración. RSSI es un indicador agregado y no permite recuperar de forma única geometría 3D, materiales, anatomía o número de personas.
3. Hay plataformas prácticas para un primer prototipo CSI. Espressif mantiene esp-csi con ejemplos de adquisición y análisis para familias ESP32 compatibles. Es una ruta de prototipo con nodo dedicado, no un método que convierta cualquier antena pasiva en CSI.
4. La generalización es un reto de investigación. Los modelos CSI pueden variar entre habitaciones, posiciones, hardware y tiempo; hacen falta pruebas fuera de la habitación y dispositivo de entrenamiento. No anunciar precisión universal sin ensayos.
5. Android limita el escaneo convencional. WifiManager exige permisos y condiciones de ubicación según versión y aplica throttling. Los escaneos de redes no son un flujo CSI continuo ni garantizan baja latencia. Root/Magisk puede habilitar acceso a comandos del driver, pero no añade capacidades ausentes del chipset o firmware.
6. El sonido requiere medición acústica directa. El sondeo Wi-Fi puede sugerir que el entorno cambió, pero no medir por sí solo la respuesta en frecuencia de altavoces, RT60, impulso acústico ni posición de micrófono. La corrección EQ/RIR debe depender de RoomProbe y mediciones acústicas y de las rutas DSP existentes, no de inferencias Wi-Fi aisladas.

## Arquitectura propuesta

Dispositivo Wi-Fi o nodo CSI dedicado
-> proveedor específico validado (chipset + firmware + formato de muestra)
-> marcas de tiempo, calidad y normalización de tramas
-> detección de cambio ambiental (contexto, sin atribución personal)
-> si hay cambio relevante: solicitar/repetir RoomProbe acústico
-> validar respuesta impulsional, curva medida y calidad
-> seleccionar corrección EQ o perfil RIR limitado y reversible
-> aplicar por las rutas DSP existentes, con bypass y rollback

### Contrato de datos

- Fuente y versión de proveedor, modelo de chipset, firmware, tasa de muestras, número de subportadoras/antenas, marca de tiempo y pérdida de paquetes.
- Calidad CSI por trama; estado de calibración y detección de datos obsoletos.
- Separar estrictamente WIFI_METADATA_ONLY de CSI_VERIFIED.
- La puntuación de confianza del procesamiento no equivale a probabilidad de presencia de una persona.
- No producir volumen, EQ, RT60 o geometría a partir de RSSI por sí solo.
- No persistir SSID/BSSID ni CSI crudo por defecto; consentimiento separado para capturas de investigación y conjuntos de entrenamiento.

## Plan de ejecución por fases

### Fase A — línea base de audio (sin CSI)

- Confirmar el camino de RoomProbe a RoomCorrectionEq, bus de control, JNI y daemon cuando aplique.
- Registrar curva medida, ruido de fondo, posición del micrófono, nivel de reproducción y confiabilidad.
- Garantizar bypass, límites de ganancia, crossfade, restauración y evitar duplicar procesamiento entre rutas A/B.
- No aplicar corrección automática si la calibración es inválida o el micrófono/altavoz no se puede identificar.

### Fase B — piloto CSI de hardware fijo

- Usar un kit compatible documentado (por ejemplo ESP32 con esp-csi) o un AP/cliente que exponga CSI real.
- Sincronizar muestras y registrar cambios de entorno sin identificar personas.
- Validar detección de “posible cambio de escena” con puertas/muebles y cambios de posición de dispositivos; medir falsos positivos, latencia y estabilidad.
- No afirmar reconstrucción 3D ni conteo de personas en esta fase.

### Fase C — adaptación acústica contextual

- CSI solo propone: “posible cambio del entorno; volver a medir audio”.
- RoomProbe valida el cambio con medición de audio.
- El motor de decisión recomienda una corrección limitada; la aplicación automática es opcional y reversible.
- Comparar respuesta antes/después, estabilidad, clipping, latencia y percepción en escucha ciega. Si CSI y audio discrepan, manda la medición acústica.

### Fase D — geometría / reconstrucción experimental (fuera de producción)

- Investigar solo con hardware multienlace calibrado, anclas conocidas, dataset etiquetado y medición de referencia independiente.
- Validar en cuartos, hardware y fechas no vistos durante entrenamiento.
- La reconstrucción 3D no entra en producción hasta demostrar precisión, límites y repetibilidad. No se requiere anatomía para corregir acústica.

## Criterios de aceptación antes de activar automatización

- Pruebas unitarias del parser/proveedor, formatos inválidos, timestamps, pérdida de muestras, inicio/parada y revocación.
- Validación en dispositivo físico y firmware exactos.
- Umbral de falsos positivos definido para detector de cambio; pruebas en varias habitaciones y posiciones.
- RoomProbe debe confirmar el cambio acústico antes de aplicar EQ o RIR.
- Toda modificación tiene límites de ganancia, crossfade, deshacer y registro de fuente/razón.
- Sin proveedor CSI verificado: modo metadatos, sin reconstrucción ni cambios automáticos de DSP.
- No declarar “tiempo real” hasta medir latencia extremo a extremo en hardware.

## Fuentes primarias y técnicas

- IEEE, IEEE 802.11bf-2025 — WLAN Sensing: https://standards.ieee.org/ieee/802.11bf/11574/
- NIST, IEEE 802.11bf: Enabling the Widespread Adoption of Wi-Fi Sensing: https://www.nist.gov/publications/ieee-80211bf-enabling-widespread-adoption-wi-fi-sensing
- Android Developers, Wi-Fi scanning overview (permisos y throttling): https://developer.android.com/develop/connectivity/wifi/wifi-scan
- Android API, WifiManager: https://developer.android.com/reference/android/net/wifi/WifiManager
- Espressif, esp-csi (captura CSI y ejemplos para hardware compatible): https://github.com/espressif/esp-csi
- Espressif, ESP-CSI Programming Guide: https://documentation.espressif.com/esp-csi/master/README.html
- Estudio de generalización temporal CSI, Internet of Things (2025): https://doi.org/10.1016/j.iot.2025.101634

## Estado actual en IVANNA-OMEGA-SUPREME

El adaptador universal existente define proveedores CSI verificados, procesamiento de características y una compuerta de preparación 3D. No registra aún un proveedor CSI concreto ni implementa reconstrucción 3D validada. El camino de escaneo Android/root produce metadatos/RSSI. Uso de producción recomendado: CSI como contexto ambiental; RoomProbe como autoridad de medición acústica; DSP limitado, reversible y respaldado por medición.
