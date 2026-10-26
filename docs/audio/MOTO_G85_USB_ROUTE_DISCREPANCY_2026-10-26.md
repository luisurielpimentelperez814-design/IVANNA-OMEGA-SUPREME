# Moto G85 — discrepancia de ruta USB/ALSA y mixer

- **Fecha reportada por el dispositivo:** 2026-10-26 00:46:14 CST
- **Fuente:** auditoría textual aportada en conversación; no se hizo una nueva lectura del teléfono.
- **Objetivo:** registrar qué demuestra esta captura y qué falta antes de asociar el servo ASRC a una salida real.

## Hallazgos de esta captura

1. `/proc/asound/cards` lista `card 0 [fmidi]` (MIDI Gadget) y `card 1 [blairqrdsndcard]` (blair-qrd-snd-card).
2. `/proc/asound/pcm` lista `01-02: USB_AUDIO-RX` como playback y `01-14: MI2S-LPAIF-RX-PRIMARY` como playback.
3. La consulta de mixer mostrada devuelve `Mixer name: 'f_midi'` y cero controles. Por tanto, **esta consulta no es evidencia sobre los controles del mixer USB de audio**. Un reporte anterior que identificaba `USB Audio` y seis controles debe mantenerse separado hasta conocer el comando exacto, la selección de tarjeta/mixer y la salida sin recortar de cada ejecución.
4. Los logs de IVANNA nombran `/dev/snd/pcmC1D14p`, que corresponde al PCM enumerado como dispositivo 14 (`MI2S-LPAIF-RX-PRIMARY`), no al dispositivo 2 (`USB_AUDIO-RX`). Esto no demuestra por sí solo un error: puede ser una ruta diferente o un log de otra sesión. Sí impide afirmar que esos logs describan la ruta USB activa.
5. Los logs muestran `pub=0 rech=0`, ausencia de publicador ligado al stream PAL/HAL propietario y servo bloqueado. El ratio `1.000000` es el estado/objetivo neutro reportado por el software; no confirma el ratio de un ASRC físico.
6. La respuesta `SET_SUPREME_AXES` aplicada y el proceso `ivanna_daemon` activo demuestran actividad del plano de control, no una conexión al handle PAL propietario ni una corrección del reloj USB.

## Estado de ingeniería

- Ruta USB activa correlacionada con un handle PAL propietario: **no demostrada**.
- Mixer consultado en esta captura: **`f_midi`, cero controles**; no válido para concluir sobre el mixer USB de audio.
- Identidad de la ruta asociada a `pcmC1D14p`: **MI2S según la enumeración aportada**, no `USB_AUDIO-RX`.
- Publicador de muestras PAL/HAL activo: **no demostrado; logs indican que no existe**.
- Actuador ASRC de razón variable y ratio aplicado verificable: **no identificado**.
- Servo: **debe permanecer bloqueado**.

## Evidencia mínima siguiente

Antes de modificar código o habilitar el servo, capturar en una misma sesión de reproducción:

1. Enumeración completa de tarjetas/PCM y el estado de cada PCM relevante antes y durante la reproducción.
2. Comando exacto usado para consultar mixer, selección de tarjeta/mixer y salida completa; verificar que el nombre de mixer corresponde al dispositivo de audio, no al gadget MIDI.
3. Ruta de salida reportada por AudioFlinger para la misma sesión y correlación con el dispositivo/handle PAL propietario.
4. Una consulta de posición/timestamp obtenida del mismo stream propietario, con dominio temporal y semántica documentados.
5. Identificación independiente del actuador de ratio, contrato del payload, límites y lectura/telemetría del ratio aplicado.

No abrir un PCM paralelo desde el daemon, no escribir controles de mixer por tanteo, y no usar `BT SLIMBUS CLK SRC`, volumen ni mapas de canal como sustituto de ASRC. Los nombres de nodos ALSA enumerados no demuestran por sí solos qué ruta está reproduciendo audio en ese instante.

## Relación con la auditoría ASRC

Complementa `MOTO_G85_ASRC_ACTUATOR_AUDIT_2026-10-26.md`: este documento registra la nueva discrepancia entre tarjeta/mixer/PCM, sin reinterpretar los datos anteriores como una captura equivalente.
