package com.ivanna.omega.core

import android.util.Log

/**
 * JNI bindings for IVANNA OMEGA SUPREME native library.
 * Compiled into libivanna_omega.so — unified entry point for DSP, PI-LSTM,
 * evolutionary kernel, phase oracle, and spatial engine.
 */
object IvannaNativeLib {
    /**
     * FIX (crash #2): El init original capturaba UnsatisfiedLinkError pero no
     * guardaba ningún flag, así que las llamadas a `external fun` posteriores
     * (nativeSetCompressorParams, nativeSetHarmonicGain, nativeSetSpatialAngleRad,
     * nativeSetSpatialWidthDirect, nativeStartEvoThread, etc.) en MainActivity.onCreate()
     * seguían lanzando UnsatisfiedLinkError y crasheaban la app.
     * Ahora se expone `isLoaded` para que MainActivity y callbacks puedan hacer guard.
     * Patrón idéntico al de DSPBridge y OmegaEngine.
     */
    private val loaded = NativeLibraryLoader.ensureLoaded()

    val isLoaded: Boolean get() = loaded

    // ═══════════════════════════════════════════════════════════════════════
    //  DSP Core (ivanna_omega_jni.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitDSP(sampleRate: Int): Boolean
    external fun nativeProcessBlock(
        inL: FloatArray, inR: FloatArray,
        outL: FloatArray, outR: FloatArray,
        frames: Int
    )
    external fun nativeSetParams(params: FloatArray)
    external fun nativeResetDSP()
    external fun nativeGetClipCount(): Int
    external fun nativeResetClipCount()

    // ═══════════════════════════════════════════════════════════════════════
    //  PDEngine: NHO + BiquadEnvelopeBank + CueBasedSpatial
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitPILSTM()
    external fun nativeSetAlpha(v: Float)
    external fun nativeSetBeta(v: Float)
    external fun nativeSetGamma(v: Float)
    external fun nativeSetDelta(v: Float)
    external fun nativeSetEta(v: Float)
    external fun nativeSetHarmonicGain(v: Float)
    external fun nativeSetHRTFEnabled(en: Boolean)
    external fun nativeSetAdaptEnabled(en: Boolean)
    external fun nativeSetNPMax(v: Float)
    external fun nativeSetReflectionGain(i: Int, g: Float)
    external fun nativeSetReflectionDelay(i: Int, d: Float)

    // ═══════════════════════════════════════════════════════════════════════
    //  Evolutionary Kernel (evolutionary_kernel.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitializeEvolution(populationSize: Int, generations: Int): Boolean
    external fun nativeGetBestFitness(): Double
    external fun nativeGetGeneration(): Int
    external fun nativeEvolveStep(): Boolean
    external fun nativeSetMutationRate(rate: Float)
    external fun nativeGetMutationRate(): Float

    // ═══════════════════════════════════════════════════════════════════════
    //  Phase Oracle (phase_oracle.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativePredictSamples(audioBuffer: FloatArray, sampleCount: Int): FloatArray
    external fun nativeGetPhaseState(): Float
    external fun nativeSetPhaseParameters(alpha: Float, beta: Float, gamma: Float): Boolean

    // ═══════════════════════════════════════════════════════════════════════
    //  Spatial Engine (spatial_jni.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitSpatialEngine(sampleRate: Int, bufferSize: Int): Boolean
    external fun nativeRenderSpatialBlock(
        inputBuffer: FloatArray,
        outL: FloatArray, outR: FloatArray,
        posX: Int, posY: Int, posZ: Int, mu: Int
    ): Int
    external fun nativeReleaseSpatialEngine(): Boolean
    external fun nativeGetSpatialState(): String
    external fun nativeSetSpatialParams(params: String): Boolean

    // ═══════════════════════════════════════════════════════════════════════
    //  EvolutionaryKernel ↔ PDEngine (pd_engine.hpp evo_thread_)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeStartEvoThread()
    external fun nativeStopEvoThread()
    external fun nativeGetEvoBestFitness(): Float

    // ═══════════════════════════════════════════════════════════════════════
    //  EvolutionaryKernel: persistencia (evolutionary_kernel.cpp)
    //  nativeSetEvoSavePath() DEBE llamarse antes de DSPBridge.init(), ya que
    //  start_evo_thread() dispara evo_initialize_population() -> intenta
    //  cargar el save-state en ese preciso momento.
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeSetEvoSavePath(path: String)
    external fun nativeSaveEvoState(): Boolean
    external fun nativeLoadEvoState(): Boolean

    // ═══════════════════════════════════════════════════════════════════════
    //  FIX v3.0: cableado GlassCard "COMPRESOR" y "NHO / ESPACIAL"
    //  (ivanna_omega_jni.cpp — g_comp.setThreshold/setRatio, g_pd.set_spatial_*)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeSetCompressorParams(thresholdDb: Float, ratio: Float, attackMs: Float, releaseMs: Float)
    external fun nativeSetSpatialAngleRad(rad: Float)
    external fun nativeSetSpatialWidthDirect(width: Float)

    // ═══ FASE 2: aplicación del ControlFrame desde el hilo de control ═══
    // Llama a control_apply_frame() en C++, que fusiona:
    //   - UnifiedControlFrame (YAMNet + PhaseOracle + Evo genome)
    //   - Sesgo aprendido por (contexto, param) vía LearningBias.jniGetBias
    // y publica un ControlFrame nuevo al bus seqlock. NUNCA llamar desde
    // el hilo de audio (nativeProcess / g_pd.process_block).
    external fun nativeApplyControlFrame(): Int

    // Setter del contexto activo ("genre:rock", "preset:Warm", ...). El C++
    // lo pasa a LearningBias.jniSetActiveContext().
    external fun nativeSetLearningContext(ctx: String)

    // ═══ FASE 4B: telemetría del ciclo adaptativo real ════════════════════
    // Snapshot POD de 10 floats — fuente única de telemetría del lazo
    // adaptativo, alimentado por nativeProcess() (audio thread) y consumido
    // por la UI/logs (con throttle recomendado ≥500 ms). Layout fijo:
    //   [0] rms                       (lineal, salida procesada)
    //   [1] peak                      (lineal, salida procesada)
    //   [2] gain_reduction_db         (post gainReductionLinearToDb)
    //   [3] target_gain               (0..2)
    //   [4] compressor_amount         (0..1)
    //   [5] exciter_reduction         (0..1)
    //   [6] spatial_width             (0..1.5, sugerido por el motor)
    //   [7] safety_margin             (1 sano → 0 crítico)
    //   [8] voice_protection_amount   (0..1)
    //   [9] adaptive_applied_count    (bloques con consumeIfNewer==true)
    external fun nativeGetAdaptiveTelemetry(): FloatArray?
    external fun nativeIsAdaptiveEngineRunning(): Boolean
    // FloatArray[3]: [low, mid, high] — amplitud lineal RMS (0..1)
    // Disponible cuando el ADE está activo y hay señal de audio real.
    external fun nativeGetBandEnergies(): FloatArray?

    // ═══ ADAPTIVE ENGINE MAGISTRAL ═════════════════════════════════════════
    // Motor inteligente que convierte CUALQUIER melodía en deleite auditivo
    // Analiza características de audio en tiempo real y ajusta parámetros
    
    /** Crear instancia del Adaptive Engine */
    external fun nativeCreateAdaptiveEngine(): Long
    
    /** Analizar buffer de audio y calcular parámetros óptimos */
    external fun nativeAnalyzeAudio(audioBuffer: FloatArray)
    
    /** Obtener parámetros adaptativos suavizados (12 floats):
     *  [0] compressor_threshold (dB)
     *  [1] compressor_ratio
     *  [2] exciter_amount (0-1)
     *  [3] stereo_width (0-2)
     *  [4] eq_bass (dB)
     *  [5] eq_mid (dB)
     *  [6] eq_treble (dB)
     *  [7] overall_gain (master)
     *  [8] compressor_attack (ms)
     *  [9] compressor_release (ms)
     *  [10] spatial_intensity (0-1)
     *  [11] safety_margin (0-1)
     */
    external fun nativeGetAdaptiveParameters(): FloatArray
    
    /** Obtener características analizadas del audio (8 floats):
     *  [0] rms (volumen)
     *  [1] peak (pico máximo)
     *  [2] percussiveness (0-1, cuánto ataque)
     *  [3] tonality (0-1, música vs ruido)
     *  [4] reverb_amount (0-1)
     *  [5] dynamic_range (0-1)
     *  [6] spectral_centroid (Hz)
     *  [7] spectral_spread (Hz)
     */
    external fun nativeGetAudioCharacteristics(): FloatArray
    
    /** Destruir instancia del Adaptive Engine */
    external fun nativeDestroyAdaptiveEngine()
}
