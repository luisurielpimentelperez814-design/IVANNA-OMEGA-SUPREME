# FLANCO ACTIVO: CERRADO

## Historial de Flancos
* **FLANCO 0 — Base**: CERRADO. Commit 35f17f71. STL estática en libomega_effect.so y ivanna_daemon (0 dependencia de libc++_shared.so verificado en CI), installer Magisk estándar, fusión audio_effects.xml y anti-bootloop.
* **FLANCO 1 — Zip Magisk**: CERRADO. Commit a48b7c1d. update-binary estándar, tolerancia de .sofa en health_check, sanitización CRLF en metadatos, sintaxis POSIX ash en todos los scripts, y suite de validación en CI con unzip -t.
* **FLANCO 2 — APK**: CERRADO.
  1. Compilación `assembleRelease` firmada con `signingConfigs.release` (fallback debug keystore en CI y soporte de keystore personalizada).
  2. ABI unificada en `arm64-v8a` con `pickFirsts` coherentes para `libc++_shared.so` en la app y exclusión de `libomega_effect.so` (que usa STL estática para el módulo Magisk).
  3. Detección en `MagiskBridge.kt` alineada con las propiedades del módulo (`persist.ivanna.magisk_active`, `persist.ivanna.daemon_active`) y el socket abstracto `@omega_daemon_socket` (`LocalSocketAddress.Namespace.ABSTRACT`).
  4. CI actualizado para compilar, validar con `unzip -t`, verificar firmas y empaquetar el APK Release firmado como artefacto coincidente con la versión unificada del módulo.
