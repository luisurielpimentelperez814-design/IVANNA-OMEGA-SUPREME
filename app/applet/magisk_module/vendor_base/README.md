# vendor_base/ — estado real (auditoría 2026-08-11)

Estos dos archivos **no son de IVANNA-OMEGA-SUPREME**. Son copias de los
`audio_effects.xml` originales de OEM/AOSP para dos SKU de dispositivo
(`blair`, `holi` — codenames Xiaomi/Redmi). `sku_holi_audio_effects.xml`
incluso conserva su copyright original de The Linux Foundation (2018-2019).

Verificado con grep en todo el módulo: **ningún script** (`customize.sh`,
`post-fs-data.sh`, `service.sh`, `uninstall.sh`) referencia este directorio.
No se despliegan al instalar, no se usan para restaurar el efecto original
al desinstalar, no participan en el build. Están completamente desconectados.

Lo más probable es que se guardaron aquí como referencia mientras se
diseñaba `magisk_module/system/vendor/etc/audio_effects.xml` (el archivo
que sí es la fuente de verdad activa hoy), y quedaron sin limpiar.

No se borran por la regla del proyecto (no borramos, solo mejoramos/
reparamos/optimizamos) — pero si algún día `uninstall.sh` necesita
restaurar el `audio_effects.xml` original de un dispositivo específico,
este sería el lugar correcto para hacerlo, y ahí sí cobrarían utilidad real.
Hasta entonces, tratar como referencia histórica, no como parte activa
del módulo.
