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
    private var loaded = false

    init {
        try {
            System.loadLibrary("ivanna_omega")
            loaded = true
            Log.i("IvannaNativeLib", "Native library loaded successfully")
        } catch (e: UnsatisfiedLinkError) {
            Log.e("IvannaNativeLib", "Failed to load native library", e)
        }
    }

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
    external fun nativeSetCompressorParams(thresholdDb: Float, ratio: Float)
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
}
