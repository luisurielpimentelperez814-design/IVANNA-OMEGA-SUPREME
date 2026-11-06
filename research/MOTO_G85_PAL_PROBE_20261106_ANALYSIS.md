# Moto G85 — Análisis de la captura `ivanna_pal_probe_20261106_061915` (2026-11-06)

Fuente: `report.txt` (1 549 líneas, solo lectura, ejecutado como `u:r:magisk:s0`). Este documento solo afirma lo que el reporte contiene.
**No hay aquí evidencia de carga de la sonda en el HAL** (la captura no incluye `AT_SECURE`, orden de RC ni la etiqueta de `libaudio_probe.so`). La interceptación sigue **sin demostrar**.

## Hechos verificados en el dispositivo
| Hecho | Evidencia en el reporte |
|---|---|
| Android 15 / SDK 35 (se cierra la discrepancia con la auditoría del 2026-11-04) | `ro.build.version.release=15`, `sdk=35`; `ro.vendor.build.fingerprint` es de **Android 14** (vendor 14 bajo sistema 15) |
| Dispositivo | `moto g85 5G`, `malmo`, plataforma `blair`, `ro.hardware=qcom` |
| La salida primaria (I/O handle 13 = `AudioOut_D`) va al DAC USB-C | `Output devices: AUDIO_DEVICE_OUT_USB_HEADSET` (`card=0;device=0`, «USB-Audio»); el módulo `primary` lista `USB Headset Out` entre sus dispositivos ⇒ el flujo pasa por `audio.primary.blair` → PAL |
| **Formato en la frontera AudioFlinger→HAL = PCM 16 bits** | `HAL format: 0x1 (AUDIO_FORMAT_PCM_16_BIT)` en `AudioOut_D` (192 frames = 4 ms, FastMixer), `AudioOut_15` (1920 frames = 40 ms) y los demás hilos; 48 000 Hz |
| Deriva de reloj real medida | `localSR` 48000.1 y 47999.7 Hz (≈ ±2 ppm), jitter std 0.2–0.5 ms; `Timestamp corrected: no` |
| Hay API de posición/tiempo en PAL | `pal_get_timestamp`, `pal_stream_get_mmap_position`, `agm_get_session_time` (11 y 10 referencias) |
| **No hay control ASRC/rate-adapter visible** | sección 5 (tinymix) vacía; la «única» pista del resumen es el propio título de sección. No se encontró módulo/parámetro ASRC del fabricante |

## Consecuencias para PAL/ASRC
1. **`processS32` NO es la ruta correcta para este flujo.** Con la evidencia anterior, el búfer que recibe `StreamOutPrimary::write` es S16. `PalAsrcBridge` ya tiene `processS16`; el punto de integración debe despachar por el formato real del stream (no suponer S32). Si PAL convierte a 24/32 bits más abajo (`pal_stream_write`), eso aún no está medido: **pendiente** (requiere la sonda cargada o `pal_stream_attributes`).
2. Sin ASRC de hardware expuesto, el ajuste fino de reloj es **por software** (re-muestreo IVANNA con la deriva observada, ±ppm), no control de un bloque del DSP.
3. El hilo `AudioOut_D` usa FastMixer a 4 ms: cualquier etapa en `write` debe respetar ese presupuesto (sin asignación, sin bloqueo).

## Lo que sigue sin demostrarse
- Que `libaudio_probe.so` esté cargada en el HAL (necesita `AT_SECURE`, etiqueta SELinux de la `.so` y orden de RC: ejecutar `hal_integration/moto_g85/pal_preload/collect_hal_preload_phase1.sh`).
- Que `pal_stream_write` vs `ipc_pal_stream_write` sea la ruta efectiva y el formato/alineación reales allí.
- Cualquier efecto audible de `PalAsrcOutputPath`.
