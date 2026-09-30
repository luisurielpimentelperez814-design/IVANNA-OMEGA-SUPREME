# Matriz de Cableado Extremo a Extremo (`WIRING_MATRIX.md`)

Registro verificable de cada símbolo JNI, función `external fun`, header C/C++ y consumidor Kotlin conectado de extremo a extremo.

| Fase | Componente / Símbolo | Kotlin (`external fun` + Consumidor) | C++ (`Java_*` / TU en CMake) | Test Host (`CTest`) | Evidencia |
|---|---|---|---|---|---|
| 0.1–0.7 | Herramientas de verdad y línea base | `scripts/check_jni_wiring.py`, `scripts/check_header_wiring.py`, `scripts/check_rt_safety.py`, `scripts/check_build_flags.py`, `scripts/check_docs_claims.py` | `.githooks/pre-commit`, `.github/workflows/tests-host.yml` | `150/150` (`telemetry/ctest_summary.json`) | `docs/WIRING_BASELINE.txt:1-142` |
| 1.1–1.2 | `IvannaNativeLib.nativeImeSetEnabled` / `nativeImeDecideNow` | `IvannaNativeLib.kt:265-266`, `MusicIntelligenceWorker.kt:61,74,97` (con fallback `"{}"`), `MusicIntelligencePanel.kt:25-65` | `jni/ivanna_ime_jni.cpp:19,25` en `CMakeLists.txt:126` | `test_ime_bridge` (`app/src/main/cpp/tests/test_ime_bridge.cpp:40-50`) | `nm -D` exporta `Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetEnabled` y `nativeImeDecideNow` |
| 1.3 | `IvannaNativeLib.nativeEvolveStep` / `nativeGetMutationRate` / `nativeSetMutationRate` / `nativeInitializeEvolution` / `nativeGetGeneration` | `IvannaNativeLib.kt:85,94-97`, `BrainScreen.kt:260,274`, `CmaEsFitnessPanel.kt:46,98` | `jni/evolutionary_kernel_jni.cpp:17-89` + `evolutionary_kernel_v2.cpp:393-413` en `CMakeLists.txt:120` | `test_evolutionary_kernel` (`EvolutionaryKernelV2.UnifiedJniSymbolsEndToEnd`) | Paquete corregido a `com_ivanna_omega_core_`, definición JNI única |
| 1.4 | `IvannaNativeLib.getCochlearIntensity` / `setCochlearIntensity` (fuente única Cochlear) | `IvannaNativeLib.kt:281-287,337-340`, `NativeBridge.kt:13-16`, `IvannaSpatialNative.kt:65-71`, `CochlearInverseCard.kt:62` | `jni/ivanna_spatial_jni.cpp:365-472` + `jni/ivanna_omega_jni.cpp:2576-2620` | `test_cochlear_inverse_model` + `test_unified_master_v3_safety_bench` | `Java_com_ivanna_omega_core_IvannaNativeLib_getCochlearIntensity` creado; 0 `external fun` sin símbolo |
| 1.5 | Limpieza de menciones ambiguas `external fun` (`del`, `lanza`, `sin`, `lo`, `existía`) | `AudioEngine.kt:145`, `UsbAudioProManager.kt:564`, `IVANNAApplication.kt:358`, `PiLstmBridge.kt:84,198`, `SoundScreen.kt:238` | N/A (higiene léxica JNI) | `scripts/check_jni_wiring.py` | 0 comentarios ambiguos con `external fun` |


