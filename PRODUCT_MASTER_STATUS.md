# Estado maestro del producto — IVANNA OMEGA SUPREME

**Última revisión:** 2026-10-18  
**Versión declarada:** v2.5.74 · versionCode 365 (contrastado en `version.properties` y `magisk_module/module.prop`)  
**Baseline auditado:** `main` en `953620d8b1c0b988b0ff240e0c980cf1ebb4fa49`  
**Auditoría integral en caliente:** [AUDITORIA_INTEGRAL_2026-10-18.md](AUDITORIA_INTEGRAL_2026-10-18.md)

## Estado verificado

| Área | Estado observado |
|---|---|
| Android | minSdk 28 · targetSdk 35 · JDK 17 · Kotlin/JNI · C++20 · NDK 26.1.10909125 |
| Versión de APK y módulo | Metadatos alineados en v2.5.74 (365); esto no prueba por sí solo que el artefacto del último commit se haya construido |
| CTest nativo | 412/412 tests pasaron en el workflow consultado del baseline |
| Compilación Release del APK en baseline | Falló en `SaFEngine.kt`, `CsiFeatureExtractor.kt` y `WifiSensingAutoStart.kt` |
| Puertas estáticas del baseline | Fallaron por wrappers JavaScript mal clasificados, un header de test bajo producción y una cifra documental obsoleta |
| Rama de auditoría | Contiene correcciones para los hallazgos anteriores y para el manejo de claves; aceptación pendiente de CI |
| HRTF/SOFA | Parser Kotlin con pruebas; todavía no está conectado a la ruta de reproducción. El loader nativo documenta `VALID_NOT_PARSED` y reconstrucción PCA |
| Wi-Fi/CSI | Fallback de metadatos RSSI; no se encontró un proveedor CSI registrado/verificado |
| IA genérica | El clasificador CRNN TFLite existe; `AIInferenceEngine` es pass-through y el fine-tuning no está conectado a inferencia real |
| Audio en hardware | Latencia, underruns, clipping, THD y mejora A/B no verificados en un teléfono durante esta auditoría |
| Protección de `main` | GitHub informó que la rama no está protegida; el CI no bloquea por sí solo pushes directos |

## Regla de estado

No usar «100 % listo», «cero brechas», «mejor sonido demostrado» ni «CI verde» sin evidencia del mismo commit. El estado del artefacto debe provenir del workflow y sus hashes; la mejora de sonido requiere una prueba física controlada.

