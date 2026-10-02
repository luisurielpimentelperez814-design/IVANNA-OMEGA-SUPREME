# IVANNA OMEGA SUPREME - Refactoring v2.4.0

## Resumen

Este refactor consolida la configuración de compilación, elimina dependencias duplicadas y deja el proyecto en una base moderna y estable para Android/Kotlin.

## Cambios clave

- Kotlin actualizado a 2.4.20.
- Kotlin Compose plugin actualizado y normalizado.
- Gson actualizado a 2.14.0.
- Coroutines y Serialization unificadas a 1.11.0.
- Firebase BoM actualizado a 34.17.0.
- Security Crypto actualizado a 1.1.0.
- Media actualizado a 1.8.0.
- Compose Material Icons actualizado a 1.7.8.
- `.gitignore` refinado para Android/Kotlin.
- `version.properties` y `magisk_module/module.prop` sincronizados.

## Validaciones

- `validateUnifiedVersion` ejecutado antes de `preBuild`.
- El build falla si `version.properties` y `module.prop` no coinciden.
- Se evita la resolución de conflictos de librerías mediante `resolutionStrategy`.

