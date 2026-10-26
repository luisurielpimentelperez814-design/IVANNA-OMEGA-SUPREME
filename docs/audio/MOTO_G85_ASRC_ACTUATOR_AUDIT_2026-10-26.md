# Auditoría de actuadores de audio y controles QTI/PAL — Moto G85

- **Fecha reportada por el dispositivo:** 2026-10-26 00:44:13 CST
- **Alcance:** interpretación de la salida de auditoría aportada por el usuario; no es una inspección independiente del dispositivo ni del código propietario.
- **Estado:** no se ha identificado ni validado un actuador ASRC de razón variable para la ruta USB activa.

## 1. Controles de mixer USB observados

El mixer identificado como `USB Audio` expone seis controles:

- `Playback Channel Map` (INT, 2 valores; `3 4`)
- `Capture Channel Map` (INT, 2 valores; `0 0`)
- `PCM Playback Switch` (BOOL; `On`)
- `PCM Playback Volume` (INT; `7023`)
- `Mic Capture Switch` (BOOL; `On`)
- `Mic Capture Volume` (INT; `7023`)

**Interpretación limitada:** en esta enumeración no aparece un control explícito de razón ASRC, frecuencia fraccional ni tasa de remuestreo. Esto no demuestra que no exista un actuador interno accesible por otra interfaz PAL/AGM/DSP; solo establece que no se identificó en estos seis controles de mixer. No se deben modificar los controles de volumen, mapas de canales o interruptores como sustituto de un actuador ASRC.

## 2. Interfaces y símbolos observados

La salida compartida reporta:

- En `/vendor/lib64/libar-pal.so`: referencias a `mixer_ctl_set_value`, `mixer_ctl_set_enum_by_string`, `agm_session_set_params`, `agm_session_set_non_tunnel_mode_config`, `compress_set_codec_params` y el símbolo `pal_get_param`.
- En `/vendor/lib64/libpalclient.so`: `pal_stream_set_param`, `pal_stream_get_param`, `pal_set_param`, `pal_get_param`, `pal_stream_set_device`, `pal_stream_set_buffer_size`, `pal_stream_set_volume` y `pal_stream_set_mute`.
- En `/vendor/lib64/hw/audio.primary.blair.so`: referencias a las API anteriores y símbolo `StreamOutPrimary::configurePalOutputStream()`.

Estas interfaces muestran puntos candidatos para investigar configuración de streams/sesiones. **No identifican por sí mismas** un identificador de parámetro ASRC, la estructura/payload requerido, sus unidades, el rango admitido, la sesión concreta que transporta la salida USB activa ni una confirmación de que un cambio fue aplicado al remuestreador.

Los tamaños de símbolo reportados por la herramienta no se consideran prueba del comportamiento interno de esas funciones.

## 3. Propiedades y procesos

La auditoría reporta `vendor.audio.feature.usb_offload.enable=true`, `vendor.audio.feature.usb_offload_burst_mode.enable=true`, `init.svc.audioserver=running`, `init.svc.vendor.audio-hal=running` y el proceso `ivanna_daemon --realtime`.

Son indicadores de configuración/estado de procesos. No prueban que la reproducción concreta esté usando USB offload, que IVANNA comparta el handle PAL propietario, ni que el demonio pueda comandar un ASRC físico.

## 4. Conclusión de ingeniería

La auditoría reduce la probabilidad de que el actuador sea un control simple expuesto por el mixer USB, pero no permite descartar un mecanismo interno de PAL/AGM o DSP. El siguiente trabajo no debe ser una búsqueda genérica adicional de cadenas: debe identificar la ruta de configuración de la sesión PAL/AGM activa y encontrar evidencia verificable de un parámetro de razón variable y su semántica.

Antes de habilitar el servo deben cumplirse todas estas condiciones:

1. Vincular la salida USB activa al objeto/handle PAL propietario real.
2. Obtener frames y timestamp monotónico correlacionados del mismo flujo.
3. Identificar la API real del actuador, su payload, unidades, límites y semántica de aceptación.
4. Verificar la razón aplicada mediante lectura de retorno o telemetría fiable; una llamada exitosa a un setter genérico no basta.
5. Ejecutar pruebas de aceptación y rechazo controlados en el dispositivo, con registro de ratio solicitado/aplicado, frames, timestamps, underruns y deriva.
6. Mantener la corrección bloqueada mientras cualquiera de estas puertas esté incompleta.

No se debe inferir un parámetro a partir de nombres de símbolos, escribir controles de mixer a ciegas, modificar volumen como si fuera ASRC, ni abrir un PCM paralelo desde el demonio.

## 5. Estado seguro

- Servo: **bloqueado**.
- Ratio de corrección: **1.0 como objetivo seguro**, solo si el actuador real acepta y confirma ese valor.
- Si no existe un actuador identificado o no puede verificarse la aceptación: reportar **actuador no identificado / estado aplicado desconocido**, no declarar que el hardware está fijado a 1.0.
- No afirmar corrección física del reloj del DAC. Una eventual ASRC de software puede compensar la cadencia de un flujo, pero no disciplina directamente el oscilador físico.

## Fuente de evidencia

Este documento interpreta el texto de auditoría proporcionado en la conversación el 2026-10-26. No se adjuntó el informe bruto completo ni se realizó una nueva lectura remota del dispositivo. Las conclusiones se limitan a los campos y símbolos incluidos en ese texto.
