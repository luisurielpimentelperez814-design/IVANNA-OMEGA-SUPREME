# Ficha 20 — Codecs y transporte de audio

**Dominio:** Codecs y transporte · **Estado:** PCM en el motor; resto externo
**Fuentes:** `SONDA_PAL_2026-11-07.md`; `VEREDICTO.md` §1.

## Concepto

El audio viaja en formatos. **PCM** es el audio sin comprimir (muestras).
Los codecs con pérdida (Opus, AAC, LDAC, aptX) lo comprimen para transmitirlo.
El transporte (USB UAC2, Bluetooth A2DP) lo lleva al DAC. Cada eslabón tiene
su latencia, su ancho de banda y su calidad. IVANNA procesa PCM float a 48 kHz
en el motor; lo que llega antes (el codec del streaming) y después (el
transporte al DAC) condiciona lo que el motor puede hacer.

## Definición y valores

| Formato / transporte | Datos clave |
|---|---|
| **PCM** | Sin comprimir. IVANNA: PCM_FLOAT 48 kHz en el motor; HAL: PCM 16-bit 48 kHz (S16 en `StreamOutPrimary::write`). |
| **FLAC** | Sin pérdida, ~50% del tamaño PCM. Sin artefactos de codec. |
| **Opus** | Con pérdida, 6–510 kbps, latencia 5–66 ms. El mejor a bajo bitrate. Usado por YouTube Music / Spotify (Ogg Opus). |
| **AAC** | Con pérdida, 256 kbps típico (Apple Music). |
| **LDAC** | Bluetooth, hasta 990 kbps, 96 kHz/24-bit. Calidad cercana a sin pérdida a 990 kbps; cae a 330 kbps con interferencia. |
| **aptX / aptX HD** | Bluetooth, 352 kbps / 576 kbps, latencia ~40 ms (aptX LL ~40 ms). |
| **USB UAC2** | USB Audio Class 2: hasta 384 kHz/32-bit, latencia baja. El dongle JieLi del G85 es UAC2. **Su reloj deriva −6.25 ppm** (ficha 06). |
| **Bluetooth A2DP** | Perfil de audio BT. El codec se negocia (SBC/LDAC/aptX). Latencia típica 150–250 ms (SBC); la ruta de música NO tiene actuador ASRC (ficha 06). |
| **FastMixer** | AudioFlinger: 48 kHz, 192 frames = 4 ms por mezcla. |

- **Latencia total típica:** app → AudioFlinger (4 ms) → HAL → DAC (~10 ms)
  + motor IVANNA (2.67 ms bloque + 10.7 ms ORBE/HOLOS si activos).
  Medida en la UI: 142–174 ms (incluye buffers de la app).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| Motor | `ivanna_omega_jni.cpp` | PCM_FLOAT 48 kHz; SR aceptada 8–384 kHz. |
| HAL | `audio.primary.blair.so` | S16 48 kHz en `StreamOutPrimary::write`. |
| USB | Dongle JieLi (card 0) | UAC2; deriva medida −6.25 ppm. |

## Mediciones que lo respaldan

- HAL: PCM 16-bit 48 kHz; AudioOut_D 192 frames; deriva USB −6.25 ppm.
- **Sin medición de latencia extremo a extremo en dispositivo** (el repo no
  la tiene; la UI muestra un estimado).

## Preguntas abiertas (para la evolución)

1. ¿El codec de la fuente (Opus 128 kbps vs FLAC) cambia lo que el motor
   debe hacer? ¿Un "modo codec" que compense artefactos de Opus/AAC?
2. ¿Latencia extremo a extremo medida en el G85 por ruta (altavoz/USB/BT)?
3. ¿LDAC 990 kbps vs USB: cuál suena mejor en ABX con IVANNA?
4. ¿El motor debe procesar a 96 kHz cuando la fuente es 96 kHz (en vez de
   bajar a 48 kHz)?
