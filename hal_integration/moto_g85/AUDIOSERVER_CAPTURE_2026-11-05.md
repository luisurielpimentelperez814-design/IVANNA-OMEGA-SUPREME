# Captura del HAL de audio — Moto G85 (2026-11-05)

Fuentes: `audioserver_maps.txt` (mapa de memoria del proceso `android.hardware.audio.service_64`, 461 regiones `.so`), `audioserver_fds.txt`, log de Magisk (`persist.ivanna.version=v2.5.346`) y auditoría de `init_boot`. Solo lectura; sin medición de audio.

## Hechos (leídos en las capturas)
- El proceso es `/vendor/bin/hw/android.hardware.audio.service_64` y ya tiene cargados `audio.primary.blair.so`, `libar-pal.so`, `libagm*.so` y `libar-gsl.so`. El stack PAL/AGM vive **en ese proceso**, no en el daemon ni en la app.
- **Las 461 regiones `.so` pertenecen al dispositivo del vendor original (`fe:0a`).** Ninguna viene del dispositivo donde Magisk monta los módulos (`254:56` = `fe:38`). No hay `libomega_effect.so`, `libivanna_pal_presence_probe.so` ni `libaudio_probe.so`; `libdownmix.so` mapeada es la original.
- `wrap.android.hardware.audio.service_64=LD_PRELOAD=/vendor/lib64/libaudio_probe.so` está definida y la librería **no** está mapeada: `wrap.*` no se aplica a este servicio de init.
- Magisk monta sobre `/vendor/etc/init/android.hardware.audio.service_64.rc` el `.rc` de `ivanna_hal_fix`; sin efecto observable en el HAL.
- Los mounts de módulos para efectos van a `/vendor/etc/audio_effects.xml` y `/system/etc/audio_effects.xml` (dos módulos distintos, en conflicto), mientras `/vendor/etc/audio/sku_blair/audio_effects.xml` sigue siendo el original de `erofs`.
- Nodo PCM abierto en esta captura: `pcmC1D2p` (en la anterior fue `pcmC0D2p`): la numeración cambia entre capturas, no se debe fijar. También `/dev/aud_pasthru_adsp` y `/dev/msm_audio_ion`.
- La imagen `init_boot` revisada no contiene nada de audio ni de IVANNA; su fingerprint es Android 14 y no prueba ser la activa.

## Inferencias (no verificadas)
- El HAL probablemente lee la configuración de efectos del `sku_blair`, no la ruta que sobreescribe el módulo; por eso `libomega_effect.so` no se carga. Falta confirmarlo con el log del HAL.
- Los mounts de Magisk llegan después de que `init` parseó el `.rc`; por eso el `.rc` sustituido no cambia el entorno del servicio.
- No se capturó `mountinfo` del HAL: **no se sabe** si ese proceso ve los archivos del módulo.

## Consecuencias para el puente PAL/ASRC
1. Hoy no corre código de IVANNA dentro del HAL; sin eso no hay productor de reloj PAL (frames + timestamp pareados de `pal_stream_get_mmap_position`) ni actuador de tasa. **El puente no se puede dar por conectado.**
2. `init_boot` no es el lugar (coincide con la auditoría adjunta). No se parchea `audio.primary.blair.so`.
3. Activar la inserción Ruta B (efecto en el HAL) mientras la Ruta A procesa la captura de reproducción duplicaría el DSP; requiere decisión explícita.
4. Siguiente evidencia útil, en el teléfono con root: `ivanna_control.sh preload reach` (añade `mountinfo` y dominio SELinux del HAL) y `hal_integration/moto_g85/pal_evidence/pcm_owner.sh`.
