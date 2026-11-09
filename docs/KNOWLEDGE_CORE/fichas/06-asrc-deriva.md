# Ficha 06 — ASRC / deriva de reloj

**Dominio:** B (matemáticas/DSP) · **Estado:** medido, servo en repo, inserción por interposición
**Fuentes:** `SONDA_PAL_2026-11-07.md`; `docs/ASRC_MOTO_G85_IMPLEMENTATION_DEEP_DIVE.md`.

## Concepto

El reloj del DAC no corre exactamente a 48 000 Hz: deriva unos ppm. Si la
fuente entrega muestras a 48 000 Hz exactos y el DAC las consume a 47 999.7 Hz,
el buffer se vacía o se llena: underruns/overruns, clics, deriva de fase. Un
ASRC (Asynchronous Sample Rate Converter) de ratio variable corrige la deriva
re-muestreando en continuo, con un servo que estima el ratio desde una muestra
pareada (frames + timestamp monotónico en una sola llamada).

## Definición y fórmulas

- **Deriva en ppm:** `ppm = (f_real − f_nominal)/f_nominal × 10⁶`.
  −6.25 ppm a 48 kHz = −0.3 Hz = 1 muestra de desfase cada ~3.3 s.
- **Muestra pareada:** `pal_stream_get_mmap_position` → (position_frames,
  time_nanoseconds) en una sola llamada. NO usar `pal_session_time` (struct
  de 32 bytes sin mapa verificado).
- **Servo:** estima el ratio desde la pendiente frames/tiempo; el ASRC aplica
  el ratio con interpolación (Variable ASRC del repo).
- **Actuador:** NO existe actuador hardware de ratio variable en PAL para
  música (verificado por desensamblado): `RAT_RENDER` es tag AGM 0xC0000022
  de A2DP/ABR; `devicePPMFC` es del grafo de voz SCO. El actuador es el propio
  ASRC de IVANNA en el write-stage (`pal_stream_write`).

## Mediciones (Moto G85, en vivo, 2026-11-07)

| Ruta | Deriva medida | Underruns / overruns |
|---|---|---|
| USB (dongle JieLi) | **47999.7 Hz (−6.25 ppm)** | 199 / 523 (2.29M writes) |
| Altavoz interno | ~0 (±20 ppm de ruido) | 199→201 / 523→531 en ~7 min |
| FastMixer | rate 0.999976, `Timestamp corrected: no` | Jitter ±14 ms USB vs ±1 ms altavoz |

- Card 1 (blair, primaria): 196 controles, **cero** clk/clock/pll/asrc/ppm/ratio.
- `pal_buffer` verificado: `{ void *buffer; size_t size; }` (primeros 16 bytes).
- Interposición LD_PRELOAD probada (contadora v2): ~275 llamadas/s, sonido
  preservado, fix dlopen (RTLD_NEXT devuelve NULL por RTLD_LOCAL).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `PalAsrcWriteStage` | `hal_integration/moto_g85/ivanna_pal_asrc_write_stage.hpp` | Adaptador + `PalAsrcOutputPath` + Variable ASRC (`dsp/variable_asrc.hpp`). |
| Servo | `hal_integration/` | Estima ratio desde muestra pareada. |
| Sonda | `hal_integration/tools/probe_moto_g85_pal.sh` | Empaqueta en un solo .tar.gz en /sdcard/Download. |

## Preguntas abiertas (para la evolución)

1. ¿Formato PCM por stream (rate/canales/bits) vía hook de `pal_stream_open`
   + autenticación empírica de offsets? (Bloqueo actual del write-stage ASRC.)
2. ¿Fuente de posición mmap validada en el stream activo (hoy
   `pal_stream_get_mmap_position` no es dlsym-able desde la .so)?
3. ¿Servo por ruta (USB −6 ppm, interno ~0) con cambio automático al
   cambiar de salida?
4. ¿Validación: deriva ~0 y menos underruns/overruns tras cerrar el lazo?
