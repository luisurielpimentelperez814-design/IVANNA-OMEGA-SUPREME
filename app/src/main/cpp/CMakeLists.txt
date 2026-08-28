cmake_minimum_required(VERSION 3.22.1)
project(ivanna_omega_supreme)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compile flags ──────────────────────────────────────────────────────────
# FIX v1.5: elimina -ffast-math (rompe NEON en SD8 Gen2/3, genera NaN)
# FIX v1.5: añade -fno-fast-math, -fno-associative-math, -ffp-contract=off
set(BASE_FLAGS
    -O3
    -fno-fast-math
    -fno-associative-math
    -ffp-contract=off
    -Wall
    -Werror=return-type
    -Wno-unused-result
    -Wno-unused-variable
)

# ── ABI-specific flags ───────────────────────────────────────────────────────
if(ANDROID_ABI STREQUAL "arm64-v8a")
    list(APPEND BASE_FLAGS -march=armv8-a+fp+simd -funroll-loops -fno-exceptions -fno-rtti)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    list(APPEND BASE_FLAGS -march=armv7-a -mfpu=neon)
endif()

# ── Definiciones globales ─────────────────────────────────────────────────────
add_compile_definitions(OMEGA_LIMITER_ENABLED=1)

# ────────────────────────────────────────────────────────────────────────────
# Target: libivanna_omega.so (APK NDK library)
# ────────────────────────────────────────────────────────────────────────────
add_library(ivanna_omega SHARED
    # Bus SHM seqlock — reader del bus local del efecto (Ruta B,
    # telemetría UI "SIN AUDIO"). Antes NO se enlazaba en este target:
    # audioRouteBridgeLoop() no podía abrir OMEGA_EFFECT_LOCAL_BUS_PATH.
    daemon/core/omega_control_bus.cpp
    saf_optimizer.cpp
    saf_runtime.cpp
    SaFOptimizer.cpp
    SafGlobalBridge.cpp
    SafPcaDecoder.cpp
    SafSpatialModifier.cpp
    # JNI bridge consolidado (libivanna_omega.so único)
    jni/ivanna_omega_jni.cpp
    jni/ivanna_visualizer_jni.cpp
    jni/ivanna_adaptive_jni.cpp
        # Φ_SAF-Room^∞ JNI bridge
        jni/saf_room_jni.cpp
    jni/ivanna_visualizer_v2_jni.cpp
    jni/ivanna_visualizer_bark64_jni.cpp
    jni/ivanna_npe_jni.cpp
    jni/ivanna_jni_stub.cpp
    pi_lstm_bridge_jni.cpp
    spatial/spatial_jni.cpp
    jni/ivanna_spatial_jni.cpp

    # Spatial engine
    spatial/spatial_engine.cpp
    spatial/room_model.cpp
    spatial/ivanna_head_tracker.cpp
    spatial/ivanna_object_renderer.cpp
    spatial/hrtf_convolver.cpp
    spatial/RirDataset.cpp
    spatial/RirConvolver.cpp

    # DSP core
    dsp/ParametricEQ.cpp
    dsp/Compressor.cpp
    dsp/HarmonicExciter.cpp
    dsp/StereoWidener.cpp
    dsp/GainStage.cpp
    dsp/SafetyLimiter.cpp

    # SAF Phi_SAF_infinity HRTF adaptive bridge
    SaFOptimizer.cpp
    SafGlobalBridge.cpp
    SafModelLoader.cpp
    SafPcaDecoder.cpp
    SafSpatialModifier.cpp
    # SAF Spatial Runtime (solo si existe en el checkout)
    $<$<BOOL:${EXISTS_SAF_SPATIAL_RUNTIME}>:SafSpatialRuntime.cpp>
    # NOTA (auditoria 2026-08-24): SofaHRTFLoader.cpp y SafHRTFBridge.cpp
    # retirados del target — cadena muerta confirmada: SafHRTFBridge solo se
    # incluye a si mismo (0 call-sites), SofaHRTFLoader solo es usado por
    # SafHRTFBridge. La HRTF real llega via IHR1 -> HRTFBinLoader ->
    # SafHRTFDatasetBridge -> hrtf_convolver. Los archivos permanecen en el
    # repo por si se retoma la carga SOFA directa (libmysofa).
    HRTFBinLoader.cpp
    SafHRTFDatasetBridge.cpp
    # FIX CRÍTICO (auditoría 2026-08-10): SaFJniBridge.cpp faltaba aquí.
    # Es el ÚNICO archivo que define los símbolos JNI que SaFBridge.kt
    # declara como external (nativeSaFInit/nativeSaFFeedback/nativeSaFGetParams/
    # nativeSaFSaveState/nativeSaFLoadState/etc — verificado con grep en TODO
    # el árbol, ninguna otra .cpp los define). Sin esta línea, cada llamada
    # desde Kotlin lanzaba UnsatisfiedLinkError en runtime, silenciado por el
    # runCatching{} de SaFEngine.kt — la pantalla de calibración SAF (y el fix
    # de persistencia de esta misma sesión) nunca ejecutaba código nativo real
    # en el dispositivo, pese a compilar y no dar ningún error visible.
    SaFJniBridge.cpp

    # FIX (build): undefined symbol ivanna_saf_apply_latent al linkear
    # libivanna_omega.so. SaFJniBridge.cpp:40 la referencia extern pero la
    # definición real vive en omega_effect.cpp (target libomega_effect.so —
    saf_optimizer.cpp
    saf_runtime.cpp
    SaFOptimizer.cpp
    SafGlobalBridge.cpp
    SafPcaDecoder.cpp
    SafSpatialModifier.cpp
    SafModelLoader.cpp
    # proceso audioserver), inaccesible desde este target. Este stub define
    # el símbolo en el proceso app y publica el vector latente en un
    # snapshot seqlock (ivanna_saf_get_latent_snapshot) sin duplicar el
    # efecto. La definición de omega_effect.cpp queda intacta.
    saf_optimizer.cpp
    saf_runtime.cpp
    SaFOptimizer.cpp
    SafGlobalBridge.cpp
    SafPcaDecoder.cpp
    SafSpatialModifier.cpp
    SafModelLoader.cpp
    saf_latent_bridge.cpp

    # Evolutionary (OFFLINE ONLY)
    evolutionary_kernel.cpp
    neuromorphic/ivanna_neural_upmixer.cpp

    # System audio integration
    shm_hyperplane.cpp
    # FIX (auditoria 2026-08-07): omega_daemon.cpp SACADO de la libreria.
    # El archivo define un `int main()` completo (socket AF_UNIX, listen,
    # accept loop) — no es codigo de biblioteca, es el binario de un
    # daemon standalone. Estaba entrando en libivanna_omega.so pegando su
    # main() al .so de la app, junto con 3 helpers (set_realtime_priority,
    # parse_json_field, update_parameters_smooth) que ninguna otra TU del
    # arbol referencia (grep -rn confirma 0 callers cruzados). El binario
    # real que corre en Magisk es daemon/ivanna_daemon.cpp, compilado por
    # daemon/CMakeLists.txt como add_executable(ivanna_daemon
    # daemon/CommandServer.cpp
    # daemon/OmegaShmManager.cpp) y stageado
    # a magisk_module/system/bin/. Sacar este .cpp del target NO afecta el
    # daemon en produccion — solo deja de arrastrar dead-code + un main()
    # dentro de la shared library. Los otros dos daemons duplicados
    # (daemon/omega_daemon.cpp, sin CMakeLists que lo enlace) se atacan en
    # commits siguientes.

    # FIX (build): stub del puente omega_daemon → app process.
    # Define omega_daemon_get_shared_state() referenciado por
    # jni/ivanna_omega_jni.cpp:233 (audioRouteBridgeLoop). Devuelve
    # nullptr; el caller ya maneja ese caso con `if (!shared) continue;`
    # → el hilo puente se duerme silenciosamente. Ver el .cpp para el
    # plan de sustitución cuando se restaure el puente memfd real.
    omega_daemon_bridge_stub.cpp

    # Audio orchestrator (JNI para AudioEngine.kt) — FIX v1.5
    audio_orchestrator.cpp
    anti_dolby.cpp

    # Benchmark logger (JNI para benchmarking) — FIX v1.5
    benchmark_logger.cpp
    ivanna_benchmark_jni.cpp

    # Phase oracle (JNI para prediction) — FIX v1.5
    phase_oracle.cpp

    # System audio capture (JNI para SystemAudioCapture.kt) — FIX v1.5
    system_audio_capture.cpp

    # USB Audio Pro Manager (JNI para UsbAudioProManager.kt) — FIX v1.5
    usb_audio_pro_manager.cpp

    # ════════════════════════════════════════════════════════════════════════
    # NUEVA: Unified Engine Integration v2.0 (Fase 1-6)
    # ════════════════════════════════════════════════════════════════════════

    # Fase 1: Orquestador central (UnifiedControlFrame + control_apply_frame)
    audio_control_plane.cpp

    # Fase 4: NeuroCochlearManifold + VolterraH2Symmetric (motor cochlear)
    neuromorphic/neuro_cochlear_manifold.cpp
    neuromorphic/volterra_h2_symmetric.cpp
    hexagon/ivanna_fastrpc_client.cpp

    # Fase H: Hexagon DSP real loader + NPE engine (Phase H integration)
    hexagon/ivanna_dsp.cpp
    hexagon/ivanna_fastrpc_client_load.cpp
    neuromorphic/ivanna_npe_engine.cpp

    # Fase 3: Phase Oracle refinements (Kalman predictor + BiquadBank integration)
    # (header-only, no .cpp necesario)

    # Fase 5: Evolutionary Adapter (mapeo genoma → parámetros real-time)
    # (header-only, no .cpp necesario)

    # Fase 6: Kotlin integration (AudioPipeline, AudioEngine, OmegaEngineBridge)
    # (lado Kotlin, no .cpp aquí)

    # ============================================================================
    # Fase 4B: AdaptiveDecisionEngine (cierre del lazo adaptativo en producción)
    # Antes vivía aislado en experimental/, ahora entra al .so real para que
    # DSPBridge.nativeProcess() pueda publicar RawAudioMetrics y consumir
    # AdaptiveState. NO se crea una segunda implementación — único target.
    # ============================================================================
    experimental/adaptive_engine/adaptive_decision_engine.cpp

    # ============================================================================
    # IvannaLab — suite de medición de calidad (THD/IMD/LUFS/LRA/SNR/TP)
    # Estado: mediciones reales (DFT+Hann para THD/IMD, K-weighting+gating
    # BS.1770-4 para LUFS/LRA). Puente JNI en jni/ivanna_omega_jni.cpp
    # (nativeLabReset/nativeLabFeed/nativeLabMeasure/nativeLabReport,
    # expuestos en IvannaNativeLib.kt) — sin UI todavía que los invoque.
    # ============================================================================
    ivannalab/ivannalab.cpp
    #     IvannaAudioClassifier.cpp
    neuromorphic/volterra_h2_symmetric.cpp
    #     IvannaFusionCore.cpp
    #     EvolutionaryEQ.cpp   # AVISO 2026-08-08: 3 copias en el repo divergieron entre si;
    #                          # el kernel evolutivo real es evolutionary_kernel.cpp (API en C, distinta)
    #     HrtfManager.cpp
    #     Psychoacoustics.cpp
)

target_compile_options(ivanna_omega PRIVATE ${BASE_FLAGS})

target_include_directories(ivanna_omega PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/neuromorphic
    ${CMAKE_SOURCE_DIR}/spatial
    ${CMAKE_SOURCE_DIR}/visualizer
    ${CMAKE_SOURCE_DIR}/experimental/adaptive_engine
    ${CMAKE_SOURCE_DIR}/ivannalab
)

find_library(log-lib log)
find_library(aaudio-lib aaudio)

target_link_libraries(ivanna_omega
    ${log-lib}
    ${aaudio-lib}
    dl
)

# ────────────────────────────────────────────────────────────────────────────
# Target: libomega_effect.so (AudioFlinger GlobalEffect — Magisk module)
# ────────────────────────────────────────────────────────────────────────────
add_library(omega_effect SHARED
    omega_effect.cpp
    # FIX(build): omega_effect.cpp incluye IvannaFusionCore.cpp via unity-build
    # (#include "IvannaFusionCore.cpp" en línea 3), que a su vez instancia las
    # 4 clases siguientes. Sus .cpp deben compilarse como unidades separadas
    # para que el linker encuentre los símbolos que IvannaFusionCore necesita.
    # IvannaFusionCore.cpp NO se lista aquí — ya entra por el unity-include;
    # añadirlo aquí generaría ODR violation (símbolos duplicados al enlazar).
    HrtfManager.cpp
    EvolutionaryEQ.cpp
    Psychoacoustics.cpp
    IvannaAudioClassifier.cpp
    neuromorphic/volterra_h2_symmetric.cpp
    saf_optimizer.cpp
    saf_runtime.cpp
    SaFOptimizer.cpp
    SafGlobalBridge.cpp
    SafPcaDecoder.cpp
    SafSpatialModifier.cpp
    SafModelLoader.cpp
    spatial/ivanna_object_renderer.cpp
    spatial/hrtf_convolver.cpp
    spatial/ivanna_head_tracker.cpp
    HRTFBinLoader.cpp
    SafHRTFDatasetBridge.cpp
    daemon/core/omega_control_bus.cpp
    # FIX(build): RirDataset y RirConvolver usados desde omega_apply_room()
    # en omega_effect.cpp. Estaban listados sólo en ivanna_native (target del
    # APK) pero no aquí → símbolos indefinidos al enlazar libomega_effect.so.
    spatial/RirDataset.cpp
    spatial/RirConvolver.cpp
    # FIX (distorsion digital Ruta B): omega_process expandia el canal side
    # con s *= 1.2f (TinyML bloque 'Music') y pasaba por el RIR SIN limiter
    # al final — con señal caliente eso clippea (distorsion digital dura).
    # La Ruta A (ivanna_native) ya lo compila (linea dsp/SafetyLimiter.cpp
    # del target ivanna_omega); se añade aqui para que la cadena system-wide
    # tenga exactamente la misma proteccion anti-clip que la app.
    dsp/SafetyLimiter.cpp
)

target_compile_options(omega_effect PRIVATE ${BASE_FLAGS})

target_include_directories(omega_effect PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/spatial
)

target_link_libraries(omega_effect
    ${log-lib}
    android
    m
    dl
)

# Exclude from APK packaging (shipped via Magisk)
set_target_properties(omega_effect PROPERTIES
    EXCLUDE_FROM_ALL TRUE
)



# Auto stage libomega_effect.so for Magisk
# AUDIT FIX (staging path): el path era "${CMAKE_SOURCE_DIR}/../magisk_module"
# que desde app/src/main/cpp resuelve a app/src/main/magisk_module (inexistente).
# El daemon usa ../../../../magisk_module (raiz del repo) — se alinea el effect
# al mismo path para que el zip del modulo lleve ambos binarios.
add_custom_command(TARGET omega_effect POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_SOURCE_DIR}/../../../../magisk_module/system/vendor/lib64/soundfx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:omega_effect>
    "${CMAKE_SOURCE_DIR}/../../../../magisk_module/system/vendor/lib64/soundfx/libomega_effect.so"
    VERBATIM
)
add_custom_target(stage_omega_effect ALL DEPENDS omega_effect)

# IVANNA daemon standalone (Magisk)
add_subdirectory(daemon)


add_executable(test_oem_stability_suite
    regression/test_oem_stability_suite.cpp
)

target_link_libraries(test_oem_stability_suite
    gtest
    gtest_main
    pthread
)

target_include_directories(test_oem_stability_suite PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest/googletest/include
)

    
add_dependencies(test_oem_stability_suite gtest)
