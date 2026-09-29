# CHANGELOG — v2.3.16 (2316) — Supreme Acoustic Stability Guard

1. **SupremeAcousticStabilityGuard (`app/src/main/cpp/supreme/SupremeAcousticStabilityGuard.hpp`)** — Capa permanente RT-safe (`alignas(64)`, lock-free, 0 malloc) con instrumentación por etapa (`StageBlockMetrics`, `FirstFaultReport`), arbitraje de ruta única (`SinglePathArbitrationState`), límite de crecimiento de energía por módulo (`enforceStageEnergyCeiling`), filtro bloqueador DC de 2º polo (3.5 Hz), gobernador continuo de headroom C2 (`0.92f` con aproximación racional de Padé) y continuidad Hermite C1 en fronteras de bloque.
2. **Corrección de núcleo Volterra H2 (`volterra_h2_symmetric.cpp`)** — Corregido el desplazamiento off-by-one del índice circular `d_idx` que hacía que el tap `k=0` leyera `t-63` en lugar de `x[n]`, eliminado el filtro peine fijo de 1 muestra (`0.08f * x[n-1]`) y reemplazado el recorte duro `std::clamp` por techo racional C2.
3. **FIFO de latencia cero y arbitraje espacial (`IvannaFusionCore.h/.cpp`)** — `resetFifo()` ahora inicializa `m_outFifoCount = 0` eliminando las caídas periódicas a cero (efecto hélice/rotor) en bloques no múltiplos de 128, y se arbitra `HoaBinauralDecoder` vs `WfsRenderer` vs `stereoWidth` para impedir doble/triple espacialización.
4. **Eliminación de bombeo de ganancia y doble procesado en Kotlin (`SafetyLimiter.cpp`, `IvannaAgentCore.kt`, `PlaybackCaptureService.kt`, `IvannaBridgePlayer.kt`, `IvannaSpatialEngine.kt`, `OmegaVibratoryProcessor.kt`)** — Conteo exclusivo de sobrecargas reales en `SafetyLimiter`, histéresis de recuperación en `DspControlAgent`, arbitraje de ruta única cuando `DSPBridge` C++ o el daemon Magisk ya procesaron el bloque, interpolación fraccional de retardo ITD y envolvente estéreo vinculada L/R.

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
