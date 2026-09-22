# CHANGELOG — v2.3.11

1. **Fix RT real (audio)** — `SyntheticHRTF::generate()`/`generateFromDataset()` construían un `HRIRPair` nuevo (dos `std::vector<float>`) en cada llamada, con `malloc`/`free` dentro del hilo de audio (`HRTFConvolver::process()`, SCHED_FIFO) en cada crossfade de azimut — es decir, en cualquier movimiento de fuente con HRTF activo. Ahora usa un scratch persistente preasignado; cero allocs por llamada. Test de regresión añadido (`test_hrtf_convolver_rt_safety`, barrido continuo de azimut bajo ASan/UBSan).
2. **Fix build (link)** — `music_intelligence/{MusicFeatureExtractor,MusicIntelligenceEngine,ImeBridge}.cpp` faltaban en el target `ivanna_omega` (solo estaban en `omega_effect`); `libivanna_omega.so` fallaba con `undefined symbol: imeFeedBlock/imeDecideNowJson/imeSharedOpaque`.
3. **Fix build (compilación)** — `resetFifo()` duplicado en `IvannaFusionCore.h` y `classifier` usado fuera de su ámbito en `omega_process()` (introducidos por la integración anti-Dolby).
4. **Fix build (Kotlin/JNI)** — `IvannaNativeLib.nativeSetWfsSpeakerLayout` no tenía declaración `external fun` ni wrapper JNI pese a que `WfsCalibrationManager.kt` ya lo llamaba. El daemon ya aceptaba `wfsSpeakerX/Y/Z` en el comando `SET_WFS`; solo faltaba el puente app→daemon.
5. **Versionado** — `module.prop`/`update.json` se desincronizaron del contenido real durante varios builds seguidos con `version` sin cambiar: Magisk no detectaba actualización disponible pese a que el zip publicado sí tenía binarios nuevos. Se sube a `v2.3.11` para que el chequeo automático vuelva a funcionar.

# CHANGELOG — surgical-hardening-v5

1. **Fusión Magisk idempotente** — `magisk_module/customize.sh` ya no duplica `omega_effect` al reinstalar y aborta si el XML fusionado queda inconsistente.
2. **Release hardening** — el workflow deja de aceptar `libomega_effect.so` vacío/ausente y ahora valida ELF + export de `AUDIO_EFFECT_LIBRARY_INFO_SYM` antes de empaquetar.
3. **Cadena de releases consistente** — `update.json` queda publicado para clientes Magisk y el workflow puede subir APKs/ZIP a GitHub Releases cuando se empuja una tag `v*`.
4. **Versionado alineado** — APK y módulo Magisk quedan sincronizados en la línea `1.8 / 1800` para evitar desajustes de soporte y distribución.

# CHANGELOG — surgical-hardening-v4

1. **Dedup estructural** — se eliminaron árboles C++ duplicados y quedó una sola fuente activa por dominio.
2. **Consolidación de binarios** — las superficies JNI del APK quedaron unificadas en `libivanna_omega.so`.
3. **Vectorización completa** — Gammatone13 ganó ruta NEON y se extendió FTZ/DAZ + prioridad de audio a los hot paths.
4. **Testing real** — se añadió `cpp/tests/` con GTest y se corrigió la inestabilidad numérica de Gammatone13 detectada por la suite.
5. **Auditoría de memoria / concurrencia** — se fijaron órdenes de memoria explícitos y se validó la suite con ASan/TSan.
6. **Benchmarks** — se agregó `tools/benchmark_suite.cpp` y `docs/BENCHMARKS.md` con protocolo y referencias públicas comparativas.
7. **Limpieza final** — README, `.gitignore` y changelog quedaron alineados con la arquitectura consolidada.
