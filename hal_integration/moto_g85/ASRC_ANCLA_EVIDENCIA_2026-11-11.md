# ASRC — Datos crudos del anclaje (evidencia)

> Los datos EXACTOS que se recabaron en el teléfono para analizar y deducir
> dónde se ancla el ASRC. Sin interpretación, sin resumen: la salida cruda
> de cada comando y cada medición, con su fecha y su fuente. A partir de
> estos datos se deduce el punto de anclaje. Actualizado 2026-11-11.

## DATO 1 — Fecha y dispositivo (2026-11-11 01:05:52 CST)

```
Wed Nov 11 01:05:52 CST 2026
moto g85 5G
15
motorola/malmo_gn/malmo:15/V1UOS35H.74-16-4-16/11943-1eca7:user/release-keys
```

Fuente: `date; getprop ro.product.model; getprop ro.build.version.release; getprop ro.build.fingerprint` (por SSH, como root).

## DATO 2 — Procesos de audio (ps -A)

```
root           614     2          0      0 rescuer_thread      0 I [uaudio_svc]
media          950     1   10889608   2752 do_sys_poll         0 S audioadsprpcd
audioserver   1448     1   11910984  11664 binder_thread_read  0 S android.hardware.audio.service_64
mediacodec    1597     1   11033056   6040 binder_thread_read  0 S media.audio.qc.codec
audioserver   1614     1   11749780  20232 binder_thread_read  0 S audioserver
```

Fuente: `ps -A | grep -i audio` (por SSH, como root).

**Lo que se deduce:** hay UN proceso que es el servicio HAL de audio:
`android.hardware.audio.service_64`, **PID 1448**, usuario `audioserver`.
Es distinto de `audioserver` (PID 1614, el servidor de audio de Android).
El HAL es el proceso 1448.

## DATO 3 — PID del HAL (confirmación)

```
1448
```

Fuente: `pidof android.hardware.audio.service_64`.

## DATO 4 — /proc/1448/maps: libar-pal.so (COMPLETO)

```
7b95691000-7b9575c000 r--p 00000000 fe:0a 10458281                       /vendor/lib64/libar-pal.so
7b95761000-7b95984000 r-xp 000d0000 fe:0a 10458281                       /vendor/lib64/libar-pal.so
7b95991000-7b9599c000 r--p 00300000 fe:0a 10458281                       /vendor/lib64/libar-pal.so
7b959ab000-7b959ac000 rw-p 0030a000 fe:0a 10458281                       /vendor/lib64/libar-pal.so
```

Fuente: `grep "libar-pal" /proc/1448/maps`.

**Lo que se deduce:** `libar-pal.so` (la librería PAL de Qualcomm) SÍ está
cargada dentro del proceso HAL (PID 1448). Su base (inicio del mapeo,
offset 0 del archivo) es **`0x7b95691000`**. El segmento ejecutable (r-xp)
empieza en `0x7b95761000` (offset de archivo 0xd0000).

## DATO 5 — /proc/1448/maps: audio.primary.blair.so (COMPLETO)

```
7b95241000-7b95263000 r--p 00000000 fe:0a 8062807                        /vendor/lib64/hw/audio.primary.blair.so
7b95271000-7b952bf000 r-xp 00030000 fe:0a 8062807                        /vendor/lib64/hw/audio.primary.blair.so
7b952c1000-7b952c3000 r--p 00080000 fe:0a 8062807                        /vendor/lib64/hw/audio.primary.blair.so
7b952d2000-7b952d3000 rw-p 00081000 fe:0a 8062807                        /vendor/lib64/hw/audio.primary.blair.so
```

Fuente: `grep "audio.primary.blair" /proc/1448/maps`.

**Lo que se deduce:** `audio.primary.blair.so` (el HAL propietario de
Motorola) SÍ está cargado en el MISMO proceso (PID 1448). Su base es
**`0x7b95241000`**. El propietario y la librería PAL viven en el mismo
proceso: ese proceso ES el dueño del audio.

## DATO 6 — /proc/1448/maps: libpalclient.so

```
(sin resultados)
```

Fuente: `grep "libpalclient" /proc/1448/maps`.

**Lo que se deduce:** `libpalclient.so` NO está mapeada en el HAL en este
momento. El thunk `libpalclient.so` existe en el repo como referencia, pero
no es el camino que usa el HAL vivo. El camino real es directo:
`audio.primary.blair.so` → `libar-pal.so` en el mismo proceso.

## DATO 7 — pal_stream_write en libar-pal.so (strings)

```
pal_stream_open
pal_stream_close
pal_stream_write
pal_stream_get_mmap_position
pal_stream_get_mmap_position
pal_stream_write
pal_stream_close
pal_stream_open
```

Fuente: `strings /vendor/lib64/libar-pal.so | grep -E "^pal_stream_(write|open|close|get_mmap_position)$"`.

**Lo que se deduce:** los símbolos `pal_stream_open`, `pal_stream_close`,
`pal_stream_write` y `pal_stream_get_mmap_position` EXISTEN en
`libar-pal.so`. `pal_stream_write` es el punto de escritura real: ahí es
donde el HAL entrega el PCM a PAL. Ahí es donde se ancla el ASRC.
(Cada símbolo aparece 2 veces: una en la tabla de símbolos dinámicos y
otra en la tabla de strings; eso es normal.)

## DATO 8 — cmdline del HAL

```
/vendor/bin/hw/android.hardware.audio.service_64
```

Fuente: `tr '\0' ' ' < /proc/1448/cmdline`.

**Lo que se deduce:** el proceso 1448 es el binario
`/vendor/bin/hw/android.hardware.audio.service_64`, el servicio HAL de
audio del sistema. No es un proceso de app ni un demonio de IVANNA.

## DATO 9 — Usuario del HAL

```
USER           PID NAME
audioserver   1448 android.hardware.audio.service_64
```

Fuente: `ps -o USER,PID,NAME -p 1448`.

**Lo que se deduce:** el HAL corre como usuario `audioserver` (no como
root, no como app). Por eso el LD_PRELOAD tiene que venir del init
(el .rc de Magisk), no de un proceso de usuario: solo init puede poner
`LD_PRELOAD` en el entorno de este servicio al arrancarlo.

## DATO 10 — Deriva de reloj medida (las mediciones que justifican el ASRC)

| Fecha | Medición | Valor | Fuente |
|---|---|---|---|
| 2026-11-07 | Deriva en vivo (bocina) | 47999.7 Hz vs 48000 nominal = **−6.25 ppm**; 199 underruns / 523 overruns | Sonda PAL en vivo |
| 2026-11-07 | Deriva v2 (bocina) | ~−13 ppm | Sonda PAL en vivo |
| 2026-11-07 | Deriva v2 (USB) | ~−70 ppm | Sonda PAL en vivo |
| 2026-11-09 | Deriva USB-C DAC | −99.99 ppm (saturó el clamp ±100 ppm) | Sonda PAL en vivo |
| 2026-11-09 21:12 CST | Servo saturado | −500 ppm (luego se vio que era transitorio de arranque; la deriva real volvió a −6.25 ppm) | Sonda PAL en vivo |
| 2026-11-10 04:40 CST | Probe v5 en vivo | **drift +58.055 ppm** (bocina), clock=write-timing, opens=8 | Probe v5 (ivanna_asrc_write_stage.c) |
| 2026-11-10 00:40 CST | Dump ASRC v5 | ratio 0.999993750, drift_ppm −6.250, clock=write-timing, sat=0 | `/data/vendor/ivanna_audio/pal_asrc_stage.txt` |

**Lo que se deduce:** el reloj del DAC NO es 48000 Hz exactos: deriva entre
−70 ppm y +58 ppm según la ruta (bocina/USB) y el momento (arranque vs
estable). Por eso hace falta el ASRC: corrige esa deriva en el write-stage.
El ratio inicial fijo es 0.99999375 (= 1 − 6.25 ppm, la deriva medida).

## DATO 11 — Offsets PAL reales (atributos del stream)

Los offsets hipotéticos (in_rate @12, in_bits @16, in_fmt @20, in_ch @24)
son **FALSOS** (todos ceros en este firmware). Los REALES, confirmados por
dump RAW del dispositivo el 2026-11-09:

| Campo | Offset real |
|---|---|
| in_rate | @56 (word 14) |
| in_bits | @60 (word 15) |
| in_fmt | @64 (word 16) |
| in_ch | @68 (word 17, low u16) |

Formato confirmado de música: **48000 Hz, 16-bit, fmt 1 (PCM), ch 2**.
Voz: 16000 Hz, 16-bit.

**Trampa medida:** los streams que llevan la música de verdad (w00=1 y
w00=2, con cientos/miles de writes) tienen sus primeros 128 bytes en CERO
— el formato NO se puede leer por stream en ellos. Por eso la .so se
configura para el formato CONOCIDO (48 kHz, 16-bit, estéreo) y usa los
offsets reales solo como cross-check.

Fuente: `hal_integration/moto_g85/pal_preload_probe/ivanna_asrc_write_stage.c`
(comentario "VERIFIED LAYOUT", 2026-11-09) + sonda de atributos en vivo.

## DATO 12 — Fuente de reloj: pal_stream_get_mmap_position

- **NO es dlsym-able** (ausente de la tabla de símbolos dinámicos de
  libar-pal.so).
- **EXISTE** en el binario en el offset ELF **0x2ec180** (un thunk de 4
  bytes: `StreamOutPrimary::GetMmapPosition` [audio.primary.blair.so
  0x58d40] → `pal_stream_get_mmap_position` [libar-pal.so 0x2ec180] →
  `SessionAlsaPcm::GetMmapPosition` → `pcm_mmap_get_hw_ptr`).
- Se resuelve SIN dlsym y SIN dlinfo: se lee la base de libar-pal.so de
  `/proc/self/maps` (DATO 4: `0x7b95691000`), se suma 0x2ec180 y se valida
  que los 4 bytes sean una instrucción `B` de AArch64 antes de llamarla.
- Devuelve (position_frames u32, time_ns i64 CLOCK_MONOTONIC) en una llamada.
- **Música = mixer regular, NO MMAP** (mmap da −ENOSYS en streams
  mixer/direct). El reloj real es write-timing (DATO 10).

Fuente: `ivanna_asrc_write_stage.c` (comentario "CLOCK SOURCE") +
`hal_integration/ivanna_pal_mmap_position_adapter.hpp`.

## DATO 13 — El binario ASRC v5 (lo que se ancla)

| Dato | Valor |
|---|---|
| Archivo | `magisk_module/system/vendor/lib64/libivanna_asrc_write_stage.so` |
| SHA-256 (verificado) | `ac1ef787942b6c86fd45ff4f6c0c28f0920c60ae0cd7f17b351bf5b96b13384a` |
| Tamaño | 1,252,048 bytes |
| ELF | 64-bit LSB shared object, AArch64, BuildID sha1 `c33de66a5450bf033116b3b7f80c75522f083e6b`, not stripped |
| Fuente | `hal_integration/moto_g85/pal_preload_probe/ivanna_asrc_write_stage.c` (1639 líneas) + `fir_v5_coeffs.h` (1038 líneas) |
| DT_NEEDED | libdl.so + liblog.so (únicas dependencias; el resto se resuelve en runtime contra la libc del HAL) |
| `dlinfo` | PROHIBIDO (no existe en el namespace vendor del HAL; el HAL entra en crash-loop, 2026-11-09) |

Verificación: `sha256sum` del archivo en el módulo coincide con
`libivanna_asrc_write_stage.so.PROVENANCE.txt`; `file` confirma AArch64;
`strings` confirma "IVANNA ASRC WRITE STAGE v5".

## DEDUCCIÓN (cómo estos datos llevan al punto de anclaje)

1. **DATO 2 + DATO 3:** el dueño del audio es el proceso 1448
   (`android.hardware.audio.service_64`). No es la app, no es audioserver.
2. **DATO 4 + DATO 5:** en ese proceso viven `audio.primary.blair.so`
   (el HAL propietario) y `libar-pal.so` (PAL). El dueño y la librería PAL
   están en el mismo proceso.
3. **DATO 7:** `pal_stream_write` existe en `libar-pal.so`. Es el punto
   donde el HAL entrega el PCM a PAL. Ese es el punto de escritura real.
4. **DATO 9:** el HAL corre como `audioserver`, así que solo init (vía el
   .rc de Magisk) puede ponerle `LD_PRELOAD` al arrancarlo. No se puede
   inyectar desde una app ni desde Termux en caliente.
5. **DATO 10:** el reloj deriva (−70 a +58 ppm), así que hace falta
   corregirla. El ASRC la corrige resampleando en el write-stage.
6. **DATO 11 + DATO 12:** el formato real es 48 kHz/16-bit/estéreo y el
   reloj se mide por mmap (offset 0x2ec180) o por write-timing. El ASRC
   usa esos datos para calcular el ratio.
7. **DATO 13:** la .so v5 es el código que hace todo eso, y su SHA está
   verificado.

**Conclusión:** el ASRC se ancla interponiendo `pal_stream_write` en el
proceso 1448 por LD_PRELOAD (el .rc `overlay.d/ivanna_audio.rc` lo pone en
el entorno del servicio al arrancar). Cada uno de estos datos es necesario
para llegar a esa conclusión; si falta uno, la deducción no se sostiene.
