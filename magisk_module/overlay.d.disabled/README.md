# overlay.d.disabled/ — inyección LD_PRELOAD del ASRC v5, DESACTIVADA por defecto
#
# `ivanna_audio.rc` replica la stanza completa de `vendor.audio-hal` con
# `override` + `setenv LD_PRELOAD /vendor/lib64/libivanna_asrc_write_stage.so`.
#
# Desde v2.5.385 este archivo NO vive en `overlay.d/` (Magisk importa todo
# lo que hay ahí al arrancar): el HAL arranca STOCK y la .so v5 queda
# inerte en el zip. Motivo: con la v2.5.384 activa, el teléfono no volvió
# a arrancar DOS veces tras instalar el módulo + reiniciar (reset de
# fábrica en ambas, 2026-11-10). La .so corre su constructor dentro del
# proceso del HAL al cargar; si el constructor o el enlace fallan, el HAL
# muere en cada inicio e init lo reinicia en bucle. El kill switch
# `pal_asrc.off` no cubre ese caso: solo actúa después de cargar la .so.
#
# OPT-IN (solo con el teléfono arrancando limpio y rollback a mano):
#   su -c 'cp /data/adb/modules/ivanna_omega_supreme/overlay.d.disabled/ivanna_audio.rc /data/adb/modules/ivanna_omega_supreme/overlay.d/ivanna_audio.rc'
# y reiniciar. Rollback: borrar `overlay.d/ivanna_audio.rc` y reiniciar.
# El safe-mode del módulo (`post-fs-data.sh`, `activate_safe_mode`) también
# retira `overlay.d/ivanna_audio.rc` si existe, igual que los XML de audio.
