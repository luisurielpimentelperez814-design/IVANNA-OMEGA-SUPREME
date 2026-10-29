# v2.5.194 (485) — reparación SAF, SAFMUS y controles de audio

Versión preparada para publicación mediante el flujo verificado de GitHub Actions. Los resultados locales y los pendientes físicos se detallan por separado; la disponibilidad del release debe comprobarse en GitHub.

- SAF: limita el paso natural real en coordenadas normalizadas a 0.5 sigma de norma conjunta; conserva dominio ±3 sigma y dirección del gradiente. Corrige la norma anterior que medía el gradiente sin precondicionar. Puede cambiar la trayectoria y velocidad de adaptación; no implica convergencia perceptual demostrada.
- Persistencia SAF: rechazo transaccional de datos truncados, no finitos, negativos/fuera del modelo; roundtrip float exacto; reemplazo por rename. No se afirma durabilidad ante corte eléctrico (sin fsync nativo).
- SAF JNI: inicialización reportada desde resultado real; reset y recuperación publican latente al consumidor existente. Recuperación binaria Kotlin importa al motor antes de mostrar readback. El perfil recuperado/reset/feedback se publica también mediante el bus de control existente y solicita entrega al daemon; se distingue acuse de transporte de aplicación acústica.
- Calibración SAF: respuesta bloqueada hasta completar reproducción AudioTrack; fallos no cuentan como opiniones. Finalizar por límite de pasos no se anuncia como convergencia. Completar AudioTrack no prueba audibilidad física.
- SAFMUS: claves voxel numéricas sin colisiones dentro del dominio, manteniendo orden y conteos; aritmética de buffer sin overflow. No es un nuevo modelo entrenado ni DSP adicional.
- Perfil explícito DAC → AUX estéreo externo, separado de auriculares; ver AUDIO_CHAIN_MOTO_G85_STEREO.md.
- ABX: sorteo independiente por ensayo, muestras requeridas, confirmación renderer/sensor, restauración del seguimiento, resultados antiguos separados del protocolo v2.
- Web: demos optativas, sin métricas/calibraciones simuladas por defecto; comandos nativos acotados con lectura de retorno; autenticación compartida chat/imagen con token solo en memoria; instalación reproducible, carga de pestañas diferida y controles accesibles.

## Pendientes materiales

Prueba en Moto G85/DAC/Sony, cobertura por aplicación y perceptual; CSI sin proveedor; actuador HAL/reloj sin conexión verificada; loader SOFA crudo usa PCA, no lectura completa de mediciones; IA genérica permanece passthrough. Estos puntos no se declaran resueltos por cambios de interfaz.

El panel espacial deja de reinicializar SAF al abrirse o al desactivarlo: bypass envía ganancia cero y conserva la calibración. El modo de sala automático no sobreescribe una calibración personal con iteraciones registradas; para una sesión nueva se usa el reinicio explícito de calibración. El restore de HRTF tras reconexión USB consulta de nuevo el destino declarado en vez de imponer wet=1 sobre un equipo estéreo externo.

Manual del propietario incorporado: Sony MHC-PZ1D, VIDEO/SAT AUDIO IN L/R, 2CH STEREO/2CH SURR, subwoofer físico y satélites que duplican delanteros (pp.68–69,79–80,98–99). Interfaz y protocolo no presentan esos altavoces como canales AUX independientes.

## Efectos y arranque refinados

- Los 15 perfiles AudioEffect usan las frecuencias que reporta el Equalizer del dispositivo: interpolación logarítmica de la curva de diez bandas a las bandas disponibles. Antes se usaban los primeros índices, incluso con cinco bandas de frecuencias diferentes.
- Perfil base, compensación ISO, ajuste manual, voz y SAF se componen sin acumular curvas ni sustituirse en cada sondeo. El margen se calcula tras la suma: los objetivos de banda no superan 0 mB. Esto no demuestra el pico real de un ecualizador OEM ni de la salida analógica.
- El control de ancho usa todo su intervalo [0,2] dentro del límite stock existente. Los sondeos SAF no vuelven a configurar la dinámica ni anulan el alivio de clipping/graves. Un fallo en EQ no impide intentar el limitador de la sesión.
- El perfil base personalizado se guarda y restaura; los offsets temporales no se incorporan al perfil guardado. El perfil inicial global coincide con IVANNA OMEGA; una instalación actualizada sin el antiguo flag conserva los ajustes existentes.
- Un primer arranque no aplica ni marca una curva ISO de fábrica como calibración medida. Las calibraciones ya guardadas se conservan; el formato antiguo no permite distinguir una curva de fábrica marcada anteriormente de una calibración introducida por el usuario.
- El restaurador reemplaza el segundo envío de parámetros fijos al daemon. Respeta la salida declarada DAC→estéreo, evita enviar un layout WFS multicanal de referencia a esa salida, mantiene bypass de HRTF/sala sintética y recupera SAF validado sin reiniciarlo ni multiplicar dos veces su intensidad.
- En un cambio de salida se solicita restaurar o poner en bypass el perfil SAF personal según la ruta. Permanece pendiente comprobar la aplicación acústica, todos los renderers activables por otras pantallas y la política real de audio de Android en el teléfono.

Validación local adicional: 41 pruebas JVM seleccionadas (15 nuevas), APK Android y puertas estáticas. Las 605 pruebas nativas previas se reutilizan porque no se cambió C++; las pruebas web previas corresponden a entradas web sin cambios. Ningún resultado acredita potencia acústica máxima, percepción 4D universal ni superioridad frente a productos de terceros.
