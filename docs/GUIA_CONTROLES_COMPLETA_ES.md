# Guía de controles de IVANNA OMEGA SUPREME

Esta guía explica en lenguaje cotidiano para qué sirve cada familia de controles, cuándo conviene utilizarla y qué límites tiene. Los nombres exactos pueden variar según la pantalla o el módulo instalado. Si un control muestra «no disponible», «—» o un motor no cargado, no existe confirmación de que la función esté operativa en esa ruta.

## CONTROL — sonido principal

| Control | Qué hace | Cuándo usarlo / precaución |
|---|---|---|
| Modo DSP / +NHO / +Spatial / +HRTF | Selecciona el conjunto de procesamiento habilitado. | Empieza con DSP y añade etapas de una en una. |
| Automático / manual | Permite adaptación o mantiene los ajustes bajo control del usuario. | Manual para pruebas A/B; automático para escuchar sin ajustes constantes. |
| Preset / perfil | Carga un grupo de parámetros. | Compara al mismo volumen; usa un perfil neutro como referencia. |
| Exciter / armónicos | Añade armónicos y puede aumentar presencia o brillo. | Sube poco a poco; demasiado produce aspereza y sibilancia. |
| EQ | Atenúa o realza frecuencias. | Corrige problemas concretos antes de hacer grandes realces. |
| Compresor — umbral | Fija el nivel desde el que comienza la compresión. | Un umbral más bajo suele hacer que actúe sobre más partes de la señal. |
| Compresor — relación | Define cuánto se reducen los niveles por encima del umbral. | Una relación excesiva reduce la dinámica y puede sonar aplastada. |
| AntiDolby | Activa o desactiva la etapa compatible disponible en el motor. | Compara con el mismo volumen; no mejora necesariamente todo el contenido. |
| Anchura / width / spread | Cambia la amplitud estéreo o espacial percibida. | Evita ajustes extremos si las voces pierden el centro o el mono cambia mucho. |
| Hi-Res | Abre ajustes relacionados con reproducción de alta resolución. | La etiqueta no garantiza que archivo, Android y DAC estén usando una tasa concreta. |
| EQ / COMP | Abre los ajustes detallados de ecualización y compresión. | Cambia un parámetro cada vez. |
| Telemetría / Profiler | Presenta datos disponibles del motor, carga, latencia y ruta. | Un valor ausente no significa cero ni prueba que una función esté activa. |
| Perfiles adaptativos | Permite elegir comportamiento por contexto. | Elige el contexto real y compara con modo manual. |

## Sala, AUX y campo espacial

| Control | Qué hace | Cuándo usarlo / precaución |
|---|---|---|
| Medir sala | Solicita una medición acústica usando el micrófono y el permiso correspondiente. | Mide en silencio, en el punto de escucha. |
| Corrección de sala | Aplica corrección cuando hay medición válida y el motor lo admite. | Está pensada principalmente para altavoces en una habitación. |
| Borrar medición | Elimina los datos de medición guardados por ese flujo. | Úsalo si cambiaste de sala, posición o sistema. |
| Campo AUX / crossover | Activa el procesamiento de campo indicado para salida por cable o amplificador. | Empieza apagado y evita duplicar crossovers del receptor. |
| Upmix HOA / inmersividad | Distribuye contenido en un campo espacial cuando el motor lo admite. | Efecto creativo; compara también con estéreo intacto. |
| Dinámica espacial temporal | Modula la amplitud espacial con la evolución de los transitorios. | Mantén intensidad moderada si la escena parece moverse artificialmente. |
| Háptica de graves | Sincroniza vibración compatible con la energía de graves estimada. | No aumenta los graves acústicos que llegan al oído y depende del hardware. |
| WFS | Activa un renderizador de frente de onda virtual. | Requiere una ruta/arreglo compatible; no crea un arreglo físico de altavoces usando solo audífonos. |
| Preset 7ch | Envía una geometría inicial de siete canales. | Es un punto de partida, no una calibración automática de la sala. |
| Oyente X | Posición izquierda/derecha en la sala. | Mide aproximadamente desde una pared de referencia. |
| Oyente Y | Altura del punto de escucha. | Usa la altura aproximada de los oídos sobre el suelo. |
| Oyente Z | Posición de frente a fondo. | Ajusta según la distancia real del asiento. |
| Giro horizontal | Rota la referencia del oyente y recalcula coordenadas. | Deja 0° si miras al frente de referencia. |

## SOUND — percepción y efectos

| Control | Qué hace | Cuándo usarlo / precaución |
|---|---|---|
| Tipo de contenido | Selecciona un perfil de reproducción contextual. | Elige la categoría más cercana al contenido. |
| Loudness trim | Compensa sonoridad según la implementación disponible. | Puede ayudar a bajo volumen; apágalo si cambia demasiado el timbre. |
| HRTF | Usa funciones de transferencia de cabeza/oído para posicionamiento binaural. | Principalmente para audífonos; depende del perfil y la ruta real. |
| Adaptación binaural | Permite ajustar parámetros binaurales con datos disponibles. | Desactívala para una comparación fija y repetible. |
| Órbita lenta 3D | Añade movimiento lento a la escena. | Es creativo, no fiel a una mezcla estática; desactívalo si distrae. |
| Convolución híbrida + sala activa | Combina convolución y componentes de sala compatibles. | Reduce si el contenido ya tiene mucha reverberación. |
| Ángulo / anchura binaural | Ajusta orientación y extensión de la escena virtual. | Usa cambios pequeños y conserva el centro vocal. |
| Inverse cochlear / cóclea | Controla el procesamiento auditivo inverso del módulo correspondiente. | Experimental; compara a volumen igualado y apaga ante fatiga o aspereza. |

## BRAIN — adaptación y laboratorio

| Control | Qué hace | Cuándo usarlo / precaución |
|---|---|---|
| Modo manual | Evita cambios automáticos en los parámetros compatibles. | Útil para repetir pruebas y comparar resultados. |
| Población evolutiva | Define el número de soluciones candidatas. | Valores mayores pueden consumir más CPU y tiempo. |
| Generaciones | Define cuántas rondas ejecuta el proceso evolutivo. | Aumenta solo en experimentos controlados. |
| Iniciar / detener | Controla el proceso de laboratorio, no la reproducción musical. | Requiere motor nativo disponible. |
| Reset | Restablece el estado del experimento. | Guarda primero los resultados que quieras conservar. |
| Medir | Solicita una medición del módulo de laboratorio activo. | Comprueba qué mide realmente el resultado; no siempre es una medición acústica física. |
| Automático del laboratorio | Permite ejecutar la política automática disponible. | Desactívalo para controlar cada paso manualmente. |
| Guardar EVO / Restaurar EVO | Guarda o recupera estado evolutivo cuando el puente confirma la operación. | Sin confirmación del motor no se debe asumir que se guardó. |
| Ejes avanzados | Abre ajustes experimentales de reconstrucción, percepción y sincronización. | Cambia un eje por vez y conserva una referencia conocida. |

## ADAPTIVE — decisiones

| Control | Qué hace | Cuándo usarlo / precaución |
|---|---|---|
| Intensidad adaptativa | Limita cuánto puede cambiar los parámetros la política adaptativa. | Empieza bajo; reduce si hay bombeo o cambios de timbre. |
| Perfil / contexto | Indica el contexto que guía las decisiones compatibles. | Selecciona el uso real y compara contra modo manual. |
| Estado / confianza / telemetría | Expone el estado interno y la disponibilidad de decisiones. | La confianza del modelo no equivale a calidad sonora objetiva. |

## SPATIAL — HRTF y geometría

| Control | Qué hace | Cuándo usarlo / precaución |
|---|---|---|
| Sujeto HRTF | Selecciona el conjunto de datos de cabeza/oído. | Si no se confirma la carga, no des por aplicado el cambio. |
| Latentes HRTF | Ajustan dimensiones del modelo cuando el conjunto y el panel lo permiten. | Son controles avanzados; modifica uno por vez. |
| Distancia / elevación / azimut | Colocan la fuente virtual en relación con el oyente. | Comprueba que el cambio de posición sea coherente con el movimiento esperado. |
| Vista de sala / reflexiones | Visualiza geometría o estimaciones acústicas disponibles. | La visualización no equivale a medir físicamente cada reflexión. |

## PROFILER — estado y diagnóstico

| Indicador | Qué significa | Cómo interpretarlo |
|---|---|---|
| Beacon de efecto global | Disponibilidad de una fuente correlacionada para observar la cadencia del efecto. | «No disponible» significa que falta evidencia; no prueba actividad global. |
| Reloj hardware / tasa / deriva | Solo se puede mostrar con productor PCM validado. | No confundir con el reloj de AudioTrack. |
| Core ASRC software | Estado del servo software de corrección de deriva, si está conectado. | No implica control del oscilador físico del DAC. |
| Reloj AudioTrack | Cadencia/posición de la ruta AudioTrack propia de la app. | No representa todas las apps ni demuestra reloj físico del DAC. |
| CPU, memoria, latencia, underruns | Muestra mediciones y contadores que estén disponibles. | Compara con el mismo dispositivo, carga y ruta. |
| Ruta de salida | Dispositivo que Android informa como ruta efectiva durante la reproducción. | Solicitar un DAC no garantiza que Android lo haya seleccionado. |

## Qué usar según tu equipo

- **Audífonos con cable:** empieza con perfil neutro; prueba EQ suave y después HRTF/anchura.
- **Audífonos Bluetooth:** confirma la ruta real y recuerda que el códec y la latencia dependen del teléfono y del receptor.
- **Estéreo por AUX:** empieza con efectos de campo apagados, evita duplicar procesamiento del amplificador y sube el volumen gradualmente.
- **DAC USB-C:** conecta antes de reproducir y verifica el dispositivo enrutado. La lectura de ruta no demuestra por sí sola la tasa física ni el control del reloj del DAC.
- **Receptor multicanal:** valida el mapa de canales y cada altavoz; un campo WFS virtual no reemplaza un arreglo físico.

## Reglas seguras para todas las pruebas

1. Empieza con perfil neutro y volumen bajo.
2. Cambia un solo control por vez.
3. Compara activado/desactivado a volumen percibido similar.
4. Si aparecen clipping, fatiga, voces huecas, imagen inestable o chasquidos, vuelve al último ajuste estable.
5. Si aparece «no disponible», «—» o «motor no cargado», considéralo no verificado; no es una medición de cero.
6. La disponibilidad depende del hardware, Android, permisos, ruta y build instalado. No todos los controles funcionan en todas las salidas.
