# Candidato de reparación sobre v2.5.193 (484)

Sin nueva versión publicada ni sustitución del release existente. La entrega se prepara en la rama de trabajo; integración a main y release requieren el mecanismo de publicación autorizado.

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
