# Evidencia dinamica del HAL (solo lectura)

Herramientas para cerrar los datos que faltan antes de tocar `audio.primary.blair.so`. Ninguna modifica el HAL, buffers ni propiedades.

- `pcm_owner.sh` (root): que proceso tiene abierto cada `/dev/snd/pcmC*D*p` y el estado del stream.
- `pal_stream_write_probe.js` (Frida): handle, puntero/tamano de `pal_buffer`, primeras palabras y tasa de `pal_stream_write`.

Por que NO se parcha el HAL todavia: un error en el camino de escritura deja el dispositivo sin audio; el ABI de `pal_buffer` solo se conoce en sus dos primeros campos; formato y tasa reales no estan verificados; el reloj PAL actual es de software (CLOCK_MONOTONIC), asi que no hay un actuador ASRC de hardware que disciplinar. La propiedad `wrap.*` no afecta servicios de init. Es una inferencia (no un hecho) que `pcmC0D2p` sea offload USB en el ADSP.
