# overlay.d.disabled/ — inyección LD_PRELOAD del ASRC v5, ACTIVA POR DEFECTO desde v2.5.397
#
# `ivanna_audio.rc` replica la stanza completa de `vendor.audio-hal` con
# `override` + `setenv LD_PRELOAD /vendor/lib64/libivanna_asrc_write_stage.so`.
#
# Desde v2.5.397 (directiva del propietario, 2026-11-11) el ASRC v5 se ancla
# AUTOMÁTICAMENTE: customize.sh copia este archivo a `overlay.d/` y crea el
# marcador `.preload_optin` en cada instalación, así que post-fs-data.sh no
# lo retira en el arranque y el HAL carga la .so v5 al iniciar. Este archivo
# queda en overlay.d.disabled/ como FUENTE (el instalador lo copia a
# overlay.d/); no se mueve de aquí.
#
# HISTORIAL: la 384 lo activaba y el teléfono no arrancó 2 veces; la 385+
# lo desactivó por defecto (el HAL arrancaba STOCK y la .so v5 quedaba
# inerte). La .so v5 es fail-closed y YA fue verificada en vivo en este
# teléfono (deriva +58.055 ppm, ratio 0.999993750, 2026-11-10).
#
# DESACTIVAR (rollback): borrar `overlay.d/ivanna_audio.rc` y
# `.preload_optin` y reiniciar. El safe-mode del módulo
# (`post-fs-data.sh`, `activate_safe_mode`) también retira
# `overlay.d/ivanna_audio.rc` si hay 3 arranques fallidos.
