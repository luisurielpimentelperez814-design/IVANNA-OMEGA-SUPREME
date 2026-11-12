package com.ivanna.omega.core

import android.content.Context
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

    // v2.5.369 (Genezi MHC-PZ1D): asset del paquete IVK1 del kernel Volterra.
    private const val GENEZY_VOLTERRA_ASSET = "kernels/genezy_mhc_pz1d.ivk1"
    // v2.5.369 (PZ1D, cadena refinada G85 + DAC + Genezi): asset del
    // paquete IVK1 DEFAULT de Volterra. El Genezi anterior queda como
    // alternativo (no se borra el asset).
    private const val PZ1D_VOLTERRA_ASSET = "kernels/moto_g85_pz1d.ivk1"
    // v2.5.369 (selector PZ1D/Genezi): device ids de los dos kernels
    // autorizados por la puerta de producción. El PZ1D refinado es el
    // DEFAULT; el Genezi es el alternativo.
    const val VOLTERRA_KERNEL_PZ1D = "MOTO_G85_PZ1D"
    const val VOLTERRA_KERNEL_GENEZI = "MOTO_G85_GENEZY"
    private const val TAG = "IvannaNativeLib"

    inline fun <T> guardedNative(fallback: T, block: () -> T): T {
        return try {
            if (!isLoaded) {
                Log.e("IvannaNativeLib", "guardedNative: native library not loaded. Returning fallback.")
                return fallback
            }
            block()
        } catch (e: UnsatisfiedLinkError) {
            Log.e("IvannaNativeLib", "guardedNative: UnsatisfiedLinkError - ${e.message}", e)
            fallback
        } catch (e: Throwable) {
            Log.e("IvannaNativeLib", "guardedNative: Exception - ${e.message}", e)
            fallback
        }
    }


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
    // FIX QUIRÚRGICO: nativeSetParams(FloatArray) sobreescribe TODO g_params
    // (drive/wet/mix/alpha/beta/gamma/freq/resonance incluidos) y dispara
    // setParams() en compresor/exciter/widener — si el caller solo llena
    // low/mid/high/master (como hacía AdaptiveBackend.applyEQ) el resto
    // llega en 0 y apaga esos motores. Este setter SOLO toca EQ+master y
    // SOLO reconfigura g_eq/g_gain en el motor nativo (ver ivanna_omega_jni.cpp).
    // Usar este método para EQ en tiempo real; NO usar nativeSetParams para eso.
    external fun nativeSetEQParams(low: Float, mid: Float, high: Float, master: Float)
    /** Presencia (dB, ±12) directa al EQ; no toca la ganancia armónica NHO. */
    external fun nativeSetPresenceDb(db: Float)
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
    external fun nativeSetEta(v: Float)
    external fun nativeSetSpatialWet(v: Float)
    /** Eje Supremo: activa/desactiva el motor Cochlear-PINN (alias para PiLstmBridge). */
    external fun nativeSetCochlearEnabled(enabled: Boolean)
    /** Ruta A: EQ evolutivo de 512 bandas (paridad con Ruta B). Por defecto activo una vez calibrado. */
    external fun nativeSetEvolutionaryEqEnabled(enabled: Boolean)
    /** True si la calibracion verifico y el efecto esta habilitado (estado, no medicion). */
    external fun nativeIsEvolutionaryEqActive(): Boolean
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
    // FIX: nativeGetBestFitness()/nativeGetGeneration()(jobject) declaraban
    // JNI respaldado únicamente por evolutionary_kernel.cpp (v1) — archivo
    // excluido del CMakeLists activo (evolutionary_kernel_v2.cpp lo
    // reemplazó como "drop-in replacement", ver su propio comentario de
    // cabecera). UnsatisfiedLinkError garantizado en cada llamada; los dos
    // call sites reales (IvannaControlPanel.kt, CmaEsFitnessPanel.kt) ya
    // migraron a nativeGetEvoBestFitness(), respaldada por v2. Eliminada
    // para no dejar otra declaración-trampa para el próximo caller.
    external fun nativeGetGeneration(): Int
    /** Ajusta en caliente población activa (32..128) y tope de generaciones (0 = sin tope) sin reiniciar. */
    external fun nativeSetEvolutionParams(populationSize: Int, maxGenerations: Int)
    external fun nativeEvolveStep(): Boolean
    external fun nativeSetMutationRate(rate: Float)
    external fun nativeGetMutationRate(): Float

    // ═══════════════════════════════════════════════════════════════════════
    //  Phase Oracle (phase_oracle.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativePredictSamples(audioBuffer: FloatArray, sampleCount: Int): FloatArray
    external fun nativeGetPhaseState(): Float
    external fun nativeGetPhaseEnergy(): Float
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
    /** Flags de compilación reales del .so cargado (inyectadas por CMake). */
    external fun nativeGetBuildFlags(): String
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
    // FIX (build roto — Kotlin: 'Too many arguments'): MainActivity.kt ya
    // llamaba a esta función con 4 argumentos (threshold, ratio, attackMs,
    // releaseMs) desde el loop del "Adaptive Engine" (nativeCreateAdaptiveEngine/
    // nativeGetAdaptiveParameters), pero la firma solo declaraba 2 — Compressor.h
    // SÍ tiene setAttack()/setRelease() reales, solo faltaba la conexión JNI.
    external fun nativeSetCompressorParams(thresholdDb: Float, ratio: Float, attackMs: Float, releaseMs: Float)
    external fun nativeSetSpatialAngleRad(rad: Float)
    /** Last finite, clamped spatial-angle command in radians; not an acoustic measurement. */
    external fun nativeGetSpatialAngleRad(): Float
    external fun nativeSetSpatialWidthDirect(width: Float)
    /** Ancho efectivo entregado al StereoWidener (base × escala térmica). Read-back, no medición de audio. */
    external fun nativeGetSpatialWidthDirect(): Float
    external fun nativeSetAntiDolbyIntensity(intensity: Float)
    /** Current Anti-Dolby DSP command value; not an acoustic-output measurement. */
    external fun nativeGetAntiDolbyIntensity(): Float

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
    external fun nativeSetAdaptiveControls(
        modeOrdinal: Int,
        intensityPercent: Float
    )

    external fun nativeGetAdaptiveTelemetry(): FloatArray?
    external fun nativeIsAdaptiveEngineRunning(): Boolean

    // FIX (conexión pantalla "Modo Manual"): pausa/reanuda el hilo de
    // control de AdaptiveDecisionEngine (Motor A) para que el modo manual
    // (AudioStateManager/DspStateUpdater) y el modo automático nunca
    // escriban compresor/exciter/ancho al mismo tiempo. Llamar false al
    // abrir la pantalla de modo manual, true al cerrarla.
    external fun nativeSetAdaptiveEngineEnabled(enabled: Boolean)
    // FloatArray[3]: [low, mid, high] — amplitud lineal RMS (0..1)
    // Disponible cuando el ADE está activo y hay señal de audio real.
    external fun nativeGetBandEnergies(): FloatArray?
    // Latencia round-trip del pipeline DSP (µs, CLOCK_MONOTONIC) — benchmark hardware
    external fun nativeMeasureRoundTripLatencyUs(): Long

    // FloatArray[8]: [activeRoute,rms,peak,voiceProtect,
    // compAmount,excReduction,spatialWidth,adaptiveActive]
    external fun nativeGetUnifiedPipelineStatus(): FloatArray?

    // FloatArray[4]: [orbe, holos, phi, volterra] — amounts 0..1
    // (volterra 1.0/0.0: no tiene amount, solo on/off). Telemetría en vivo
    // 1 Hz (v2.5.407, IVANNA.Telemetry): una sola llamada JNI por segundo.
    external fun nativeGetEffectAmounts(): FloatArray?

    // FloatArray[26]: [shedFraction 0..1, emaUs, peakUs (ventana 1 s),
    // blockBudgetNs, 21 floats de tiempos por etapa, sessionPeakUs] —
    // estado del gobernador de shed de carga DSP (v2.5.410; ventana de pico
    // y pico de sesión en v2.5.415). Telemetría en vivo 1 Hz: confirma si
    // la cadena alcanza el periodo sin loguear en el hilo RT.
    external fun nativeGetDspShed(): FloatArray?

    // FloatArray[6]: [holosTier, holosMeanCoherence, holosWet,
    // orbeMeanCoherence, orbeShed, orbeWet] — por qué ORBE/HOLOS están en
    // bypass (v2.5.415, bandera 5 de la 414). Telemetría en vivo 1 Hz.
    external fun nativeGetSpatialGateState(): FloatArray?


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

    // ── IvannaLab — medición THD/IMD/LUFS/SNR/peak/truepeak ──────────────────
    // Implementadas en ivanna_omega_jni.cpp:917-949, símbolo JNI ya apunta a
    // esta clase (Java_com_ivanna_omega_core_IvannaNativeLib_nativeLab*),
    // solo faltaban las declaraciones Kotlin.
    // ═══ NAEL / ISO 226:2023 — Equal Loudness Compensation ═══════════
    // Cuando activo, control_apply_frame() suma la corrección por banda
    // (foldeada a low/mid/high) sobre el ControlFrame antes de publicarlo
    // al bus seqlock. Sin coste en el hilo de audio: sólo lee output_lufs
    // ya publicado por g_loudnessMeter.
    external fun nativeSetNaelEnabled(enabled: Boolean)
    external fun nativeIsIso226Enabled(): Boolean

    /** FloatArray[10]: corrección en dB por banda ISO 1/1-oct:
     *  31.5 / 63 / 125 / 250 / 500 / 1k / 2k / 4k / 8k / 16k Hz. */
    external fun nativeGetNaelCorrections(): FloatArray?

    // ═══ IvannaLab auto-feed + Loudness + Telemetría LAB (FASE 2) ════════
    external fun nativeSetLabAutoEnabled(enabled: Boolean)
    external fun nativeIsLabAutoEnabled(): Boolean
    external fun nativeGetLabAutoFrameCount(): Int
    external fun nativeSetLoudnessTarget(targetLufs: Float)
    external fun nativeSetLoudnessTrimEnabled(enabled: Boolean)
    external fun nativeGetAudioSpectrum(): FloatArray?
    external fun nativeGetPerceptualCues(): FloatArray?
    external fun nativeRunNeuralBenchmarks(): FloatArray?
    external fun nativeSetBinauralEnabled(enabled: Boolean)
    external fun nativeSetBinauralPositionRad(azimuthRad: Float, aggressiveness: Float)

    // ═══ Music Intelligence Engine & Singularidad Atlas-Escena (IME 12D) ═════
    external fun nativeImeSetEnabled(enabled: Boolean)
    external fun nativeImeDecideNow(): String
    external fun nativeImeSetUseStatDereverb(enabled: Boolean)
    external fun nativeImeSetUsePhysicalEr(enabled: Boolean)
    external fun nativeImeSetShaperMode(mode: Int)
    external fun nativeImeSetManualStyleOverride(styleIdx: Int)
    external fun nativeImeSetUserWarmthOverride(warmthOrNeg: Float)
    external fun nativeImeSetMaxCeilings(maxInvGain: Float, maxProjWet: Float, maxExcWet: Float)
    external fun nativeImeSoftReset()

    fun imeSetEnabled(enabled: Boolean) {
        if (!isLoaded) return
        try { nativeImeSetEnabled(enabled) } catch (_: Throwable) {}
    }

    fun imeDecideNow(): String {
        if (!isLoaded) return "{}"
        return try { nativeImeDecideNow() } catch (_: Throwable) { "{}" }
    }

    fun imeSetUseStatDereverb(enabled: Boolean) {
        if (!isLoaded) return
        try { nativeImeSetUseStatDereverb(enabled) } catch (_: Throwable) {}
    }

    fun imeSetUsePhysicalEr(enabled: Boolean) {
        if (!isLoaded) return
        try { nativeImeSetUsePhysicalEr(enabled) } catch (_: Throwable) {}
    }

    fun imeSetShaperMode(mode: Int) {
        if (!isLoaded) return
        try { nativeImeSetShaperMode(mode) } catch (_: Throwable) {}
    }

    fun imeSetManualStyleOverride(styleIdx: Int) {
        if (!isLoaded) return
        try { nativeImeSetManualStyleOverride(styleIdx) } catch (_: Throwable) {}
    }

    fun imeSetUserWarmthOverride(warmthOrNeg: Float) {
        if (!isLoaded) return
        try { nativeImeSetUserWarmthOverride(warmthOrNeg) } catch (_: Throwable) {}
    }

    fun imeSetMaxCeilings(maxInvGain: Float, maxProjWet: Float, maxExcWet: Float) {
        if (!isLoaded) return
        try { nativeImeSetMaxCeilings(maxInvGain, maxProjWet, maxExcWet) } catch (_: Throwable) {}
    }

    fun imeSoftReset() {
        if (!isLoaded) return
        try { nativeImeSoftReset() } catch (_: Throwable) {}
    }

    // ═══ Supremacía Acústica: Volterra H2, FastRPC Hexagon cDSP & ATI ═════
    external fun nativeSetVolterraEnabled(enabled: Boolean)
    external fun nativeIsVolterraEnabled(): Boolean
    // v2.5.369 (Genezi MHC-PZ1D): carga un paquete IVK1 en el motor Volterra
    // H2 de Ruta A. Devuelve VolterraLoadStatus como Int (0 = Ok). Cargar !=
    // habilitar: el kernel solo suena si Volterra está encendido.
    external fun nativeLoadVolterraKernelPackage(pkg: ByteArray): Int
    // v2.5.369 (Genezi): ¿tiene el motor de Ruta A un kernel efectivo cargado?
    external fun nativeHasVolterraKernel(): Boolean
    // v2.5.369 (selector PZ1D/Genezi): REEMPLAZA el paquete IVK1 del motor
    // Volterra H2 de Ruta A en runtime. A diferencia de
    // nativeLoadVolterraKernelPackage (primera carga), espera a que el
    // motor quede en silencio (o detecta que no hay audio fluyendo) antes
    // de aplicar el paquete nuevo, así que funciona con música sonando y
    // en pausa. La puerta de producción se aplica al paquete nuevo ANTES
    // de tocar el motor; ante cualquier error el kernel anterior queda
    // intacto y el motor vuelve a habilitarse si Volterra estaba
    // encendido. Devuelve VolterraLoadStatus como Int (0 = Ok).
    // Requiere la vía nativa (ivanna_omega_jni.cpp,
    // nativeReplaceVolterraKernelPackage); si la .so aún no la tiene,
    // loadPackageWithReplace cae al fallback clásico (ver abajo).
    external fun nativeReplaceVolterraKernelPackage(pkg: ByteArray): Int
    // v2.5.369 (selector): device id del kernel Volterra ACTIVO en Ruta A
    // ("MOTO_G85_PZ1D", "MOTO_G85_GENEZY", o "" si no hay kernel).
    // Es la fuente de verdad para la UI (la preferencia persistida solo
    // dice cuál se pidió cargar).
    external fun nativeGetVolterraKernelId(): String

    /**
     * Device id del kernel Volterra que está sonando de verdad en Ruta A.
     * Prefiere la vía nativa (fuente de verdad); si la .so aún no la tiene
     * (UnsatisfiedLinkError), cae al id persistido cuando hay kernel
     * efectivo. "" = sin kernel (bypass). Nunca lanza.
     */
    fun activeVolterraKernelId(context: Context): String {
        if (!isLoaded) return ""
        val nativeId = try {
            nativeGetVolterraKernelId()
        } catch (_: Throwable) {
            ""
        }
        if (nativeId.isNotEmpty()) return nativeId
        return try {
            if (nativeHasVolterraKernel()) ParameterStore(context).getVolterraKernelId() else ""
        } catch (_: Throwable) {
            ""
        }
    }

    // ── Carga / reemplazo de kernel Volterra (selector PZ1D/Genezi) ──
    // La vía canónica es loadPackageWithReplace(): reemplazo nativo con
    // espera de silencio + detección de pausa, con fallback clásico
    // (carga + reintentos) si la .so aún no trae la vía nueva. Ambas
    // vías aplican la puerta de producción al paquete nuevo ANTES de
    // tocar el motor; ante cualquier error el kernel anterior queda
    // intacto. En éxito se persiste el id del kernel activo (para el
    // arranque y para la UI) y se escribe el marcador de selección de
    // Ruta B. Hilo de CONTROL, nunca el de audio.

    /**
     * Aplica un paquete IVK1 ya leído al motor de Ruta A, por la vía de
     * reemplazo si existe, o por la vía clásica con reintentos si no.
     * Devuelve VolterraLoadStatus como Int (0 = Ok; 20 = EngineBusy).
     * En fracaso, si había kernel efectivo y Volterra estaba encendido,
     * se restaura el motor para que un reemplazo fallido no deje
     * Volterra en bypass silencioso con el kernel anterior intacto.
     */
    private fun loadPackageWithReplace(pkgBytes: ByteArray): Int {
        if (!isLoaded) return -1
        // Vía nueva (JNI nativeReplaceVolterraKernelPackage).
        val replaceStatus = try {
            nativeReplaceVolterraKernelPackage(pkgBytes)
        } catch (_: UnsatisfiedLinkError) {
            null  // .so sin la vía nueva: fallback clásico
        } catch (_: Throwable) {
            null
        }
        if (replaceStatus != null) {
            if (replaceStatus != 0) restoreVolterraAfterFailedSwitch()
            return replaceStatus
        }
        // Fallback clásico: nativeLoadVolterraKernelPackage + reintentos.
        // La primera llamada ya deshabilitó el motor; el envelope fundea
        // en ~18 ms si hay audio, así que los reintentos encuentran
        // silencio y aplican. Funciona con música sonando; en pausa
        // devuelve EngineBusy (20) tras los reintentos.
        var last = try {
            nativeLoadVolterraKernelPackage(pkgBytes)
        } catch (_: Throwable) {
            -1
        }
        var tries = 0
        while (last == 20 && tries < 5) {
            try { Thread.sleep(60) } catch (_: InterruptedException) { Thread.currentThread().interrupt(); break }
            last = try {
                nativeLoadVolterraKernelPackage(pkgBytes)
            } catch (_: Throwable) {
                -1
            }
            tries++
        }
        if (last != 0) restoreVolterraAfterFailedSwitch()
        return last
    }

    /** Tras un reemplazo fallido, si había kernel efectivo y Volterra
     * estaba encendido, vuelve a habilitar el motor para que siga
     * sonando el kernel anterior (nunca dejar bypass silencioso). */
    private fun restoreVolterraAfterFailedSwitch() {
        try {
            if (nativeHasVolterraKernel() && nativeIsVolterraEnabled()) {
                nativeSetVolterraEnabled(true)
            }
        } catch (_: Throwable) {
        }
    }

    /**
     * Carga (o reemplaza) un kernel Volterra desde un asset: lee los
     * bytes, los aplica por loadPackageWithReplace y, solo en éxito,
     * copia el .ivk1 a `files/ivanna_omega/kernels/` (Ruta B), escribe
     * el marcador `selected_kernel.txt` con el device id (omega_effect
     * lo lee en init para ordenar sus candidatos) y persiste el id del
     * kernel activo. En fracaso no toca nada persistido: el kernel
     * anterior sigue siendo el activo.
     */
    private fun loadVolterraAsset(context: Context, asset: String, fileName: String, kernelId: String): Int {
        if (!isLoaded) return -1
        return try {
            val bytes = context.assets.open(asset).use { it.readBytes() }
            val st = loadPackageWithReplace(bytes)
            if (st == 0) {
                runCatching {
                    val dir = java.io.File(context.filesDir, "ivanna_omega/kernels")
                    dir.mkdirs()
                    java.io.File(dir, fileName).writeBytes(bytes)
                    java.io.File(dir, "selected_kernel.txt").writeText(kernelId)
                    ParameterStore(context).setVolterraKernelId(kernelId)
                }.onFailure { Log.w(TAG, "loadVolterraAsset: no se pudo copiar a files/ (Ruta B sin kernel)", it) }
            }
            st
        } catch (t: Throwable) {
            Log.e(TAG, "loadVolterraAsset: error", t)
            -1
        }
    }

    /**
     * Carga el kernel Volterra H1/H2 del Sony Genezi MHC-PZ1D (ESS Farina,
     * h1 normalizado por L1) en el motor de Ruta A, desde el asset
     * `kernels/genezy_mhc_pz1d.ivk1`. ALTERNATIVO en v2.5.369: el kernel
     * DEFAULT es el PZ1D refinado (`loadMotoG85Pz1dVolterraKernel`).
     * Si el PZ1D está activo, lo REEMPLAZA (con espera de silencio /
     * detección de pausa); si el Genezi ya es el activo, no hace nada
     * (devuelve 0). También copia el .ivk1 a
     * `files/ivanna_omega/kernels/` para que Ruta B (omega_effect, proceso
     * audioserver) pueda leerlo por ruta de archivo — mismo patrón que el
     * dataset HRTF (`files/ivanna_omega/hrtf/kemar.ihr1`).
     *
     * Cargar NO habilita Volterra por sí solo: el kernel suena si
     * Volterra está encendido (ON por defecto en v2.5.369; el usuario
     * puede apagarlo en CONTROL y persiste). Fail-closed: cualquier
     * error de lectura/validación deja el motor en bypass y devuelve el
     * código de error (VolterraLoadStatus; -1 = librería no cargada o
     * error de E/S). Hilo de CONTROL, nunca el de audio.
     */
    fun loadGeneziVolterraKernel(context: Context): Int {
        if (!isLoaded) return -1
        if (activeVolterraKernelId(context) == VOLTERRA_KERNEL_GENEZI) return 0
        return loadVolterraAsset(context, GENEZY_VOLTERRA_ASSET, "genezy_mhc_pz1d.ivk1", VOLTERRA_KERNEL_GENEZI)
    }

    /**
     * Carga el kernel Volterra H1/H2 REFINADO de la cadena Moto G85 5G +
     * DAC USB-C + Sony Genezi MHC-PZ1D (h1 normalizado por L1, h2 solo
     * diagonal) en el motor de Ruta A, desde el asset
     * `kernels/moto_g85_pz1d.ivk1`. Es el kernel DEFAULT de Volterra en
     * v2.5.369 (el Genezi anterior queda como alternativo). Si el Genezi
     * está activo, lo REEMPLAZA (con espera de silencio / detección de
     * pausa); si el PZ1D ya es el activo, no hace nada (devuelve 0).
     * También copia el .ivk1 a `files/ivanna_omega/kernels/` para Ruta B
     * (omega_effect busca primero el PZ1D y luego el Genezi).
     *
     * Cargar NO habilita Volterra por sí solo: el kernel suena si
     * Volterra está encendido (ON por defecto en v2.5.369). Fail-closed
     * como `loadGeneziVolterraKernel`. Hilo de CONTROL, nunca el de audio.
     */
    fun loadMotoG85Pz1dVolterraKernel(context: Context): Int {
        if (!isLoaded) return -1
        if (activeVolterraKernelId(context) == VOLTERRA_KERNEL_PZ1D) return 0
        return loadVolterraAsset(context, PZ1D_VOLTERRA_ASSET, "moto_g85_pz1d.ivk1", VOLTERRA_KERNEL_PZ1D)
    }

    /**
     * Punto de entrada único del selector de kernel Volterra (PZ1D /
     * Genezi). Carga el kernel pedido (reemplazando el activo si es
     * distinto) y persiste la elección solo en éxito, así que el
     * arranque siguiente carga el mismo kernel. Devuelve
     * VolterraLoadStatus como Int (0 = Ok; -1 = librería no cargada o
     * error de E/S; -2 = id desconocido; 20 = EngineBusy, p. ej. cambio
     * en pausa sin la vía nativa nueva — reintentar con música sonando).
     * Hilo de CONTROL, nunca el de audio.
     */
    fun setVolterraKernel(context: Context, kernelId: String): Int {
        return when (kernelId) {
            VOLTERRA_KERNEL_GENEZI -> loadGeneziVolterraKernel(context)
            VOLTERRA_KERNEL_PZ1D -> loadMotoG85Pz1dVolterraKernel(context)
            else -> -2
        }
    }
    external fun nativeSetFastRpcEnabled(enabled: Boolean)
    external fun nativeIsFastRpcEnabled(): Boolean
    external fun nativeSetAtiEnabled(enabled: Boolean)
    external fun nativeIsAtiEnabled(): Boolean
    // ═══ Acoustic Unity Engine (v2.5.15): interruptor y telemetría del lazo cerrado MEDIDO ═════
    // Telemetría [0..23]: ver nativeGetUnityTelemetry en ivanna_omega_jni.cpp.
    external fun nativeSetUnityEnabled(enabled: Boolean)
    external fun nativeIsUnityEnabled(): Boolean
    external fun nativeGetUnityTelemetry(): FloatArray
    // ═══ Impacto predictivo por bandas / PunchEngine (v2.5.49) ═════════════════════════════════════
    /** amount 0..1 (0 = apagado: bypass exacto y latencia 0). Ruta A en proceso + Ruta B vía daemon (SET_PUNCH). */
    external fun nativeSetPunch(amount: Float)
    /** Crossfeed de graves con ITD para audifonos/DAC (Ruta A). 0 = apagado (bypass exacto). */
    external fun nativeSetCrossfeed(strength: Float)
    /** ORBE — Binaural Esférico de Campo Cercano (v2.5.363). amount 0..1 (0 = apagado,
     *  bypass bit-exacto; **1.0 = ACTIVO por defecto**, directiva explícita 2026-11-08,
     *  puerta GREEN anulada), distancia 0.15..2.0 m, spread 0..1.5,
     *  proximidad 0..6 dB, difuso 0..1, head-tracking on/off. Corre EN SERIE antes
     *  de HOLOS (ORBE → HOLOS → IVANNA → Φ), no como alternativa. Escriben los
     *  atomics g_orbe_* (Ruta A) y viajan al daemon por SET_PERCEPTUAL_STATE (Ruta B). */
    external fun nativeSetOrbeParams(
        amount: Float, distanceM: Float, spread: Float,
        proximityDb: Float, diffuse: Float, headTracking: Boolean
    )
    /** Atajo: enabled=true pone amount=1, false pone amount=0 (conserva el resto de params). */
    external fun nativeSetOrbeEnabled(enabled: Boolean)
    /** Ψ (v2.5.388): radio de cabeza personalizado en metros (0.05..0.15,
     *  KEMAR 0.0875 por defecto) para ORBE — Ec. 2 del campo con la
     *  antropometría del oyente (HeadMeasureScreen). Escribe el atomic
     *  g_orbe_head_radius_m (Ruta A + FusionCore). Sin llamarla, KEMAR. */
    external fun nativeSetOrbeHeadRadiusM(radiusM: Float)
    /** IVANNA — Operador Unificado de Campo Espacial (v2.5.375). Fusión de
     *  separación por fuentes + HRTF personalizada por MAA + escena auto-supervisada
     *  + campo neural INAF. amount 0..1 (0 = apagado, bypass bit-exacto;
     *  **1.0 = ACTIVO por defecto**), scenePull 0..1, distancia 0.5..2.0 m,
     *  head-tracking on/off. Escriben los atomics g_omni_* (Ruta A). */
    external fun nativeSetOmniParams(
        amount: Float, scenePull: Float, distanceM: Float, headTracking: Boolean
    )
    /** Atajo: enabled=true pone amount=1, false pone amount=0. */
    external fun nativeSetOmniEnabled(enabled: Boolean)
    /** Yaw de cabeza en radianes (positivo = giro a la derecha). Sin llamador aún. */
    external fun nativeSetOmniHeadPose(yawRad: Float)
    /** Audiograma: pérdidas dB a 250/500/1k/2k/4k/8k Hz por oído (FloatArray de 6). */
    external fun nativeSetOmniAudiogram(lossL: FloatArray, lossR: FloatArray)
    /** HOLOS — Operador Único de Campo Acústico (v2.5.364). Los 13 parámetros
     *  de FUSION.md §6: amount 0..1 (0 = apagado, bypass bit-exacto;
     *  **0.5 = ACTIVO por defecto**, harmonics 0.0 retuneado, directiva explícita
     *  2026-11-08, puerta GREEN anulada), distancia 0.15..2.0 m, spread 0..1.5,
     *  proximidad 0..6 dB, difuso 0..1, armónicos 0..1, calidez 0..1, dinámica 0..1,
     *  ancho 0..1.5, gamma 0.5..1.0, sala RT60 0.18..1.8 s, head-tracking on/off y bandas
     *  ERB 12/24 (24 = default; el tier térmico puede forzar 12). Corre EN SERIE tras
     *  ORBE (ORBE → HOLOS → IVANNA → Φ), no en su lugar. Escriben
     *  los atomics g_holos_* (Ruta A) y viajan al daemon por
     *  SET_PERCEPTUAL_STATE (Ruta B, snapshot ABI v10). */
    external fun nativeSetHolosParams(
        amount: Float, distanceM: Float, spread: Float,
        proximityDb: Float, diffuse: Float, harmonics: Float,
        warmth: Float, dynamics: Float, width: Float, gamma: Float,
        roomRt60: Float, headTracking: Boolean, bandCount: Int
    )
    /** EQ estático de Γ (plano por defecto = no-op): graves/medios/agudos/
     *  presencia/tilt en dB (-12..12) y frecuencia de tilt en Hz. Sin UI en
     *  v1; existe para que Ruta A y Ruta B (snapshot ABI v10) coincidan. */
    external fun nativeSetHolosEq(
        low: Float, mid: Float, high: Float, presence: Float,
        tiltDb: Float, freq: Float
    )
    /** Recorte térmico de la Ruta A (paridad con la Ruta B): [holosTier] 0 Normal, 1 Warm
     *  (12 bandas, H3 off, difuso ×0.5), 2 Limited (difuso off), 3 Bypass (legacy bit-exacto);
     *  [spatialShed] apaga ORBE y NPE. Lo publica el ThermalGovernor en cada cambio de nivel. */
    external fun nativeSetHolosThermalTier(holosTier: Int, spatialShed: Boolean)

    // ── Bucle cerrado neuro-acústico (v2.5.365) ─────────────────────────────
    /** Activa/desactiva el bucle cerrado (Ruta A). Apagado por defecto.
     *  Sin frames reales el bucle nunca se activa (fail-closed). */
    external fun nativeSetNeuroLoopEnabled(enabled: Boolean)
    /** Ingiere UN frame real de sensor. kind: 2=EEG en auricular, 3=ABR,
     *  4=fNIRS (0/1 se rechazan en C++). timestampUs: CLOCK_MONOTONIC en
     *  microsegundos (SystemClock.uptimeMillis()*1000). response/coherence:
     *  FloatArray[8] en [0..1], bandas Greenwood. Devuelve true si el
     *  frame se aceptó y se aprendió. Solo lo llama NeuroSensorGateway,
     *  con frames que llegan de un driver de sensor real. */
    external fun nativeNeuroLoopIngest(
        kind: Int, timestampUs: Long,
        response: FloatArray, coherence: FloatArray, quality: Float,
        abrWaveVLatencyMs: Float, fnirsHbO: Float
    ): Boolean
    /** Telemetría: [0] active 0/1, [1] confidence 0..1, [2] framesLearned,
     *  [3] wet 0..1. */
    external fun nativeNeuroLoopStatus(): FloatArray

    // ── Sensor acústico real por micrófono (v2.5.369), Ruta A ───────────────
    /** Prepara el sensor para la frecuencia de muestreo del micrófono. */
    external fun nativeSensorInit(sampleRate: Int): Boolean
    /** Alimenta PCM del micrófono (float, mono, [-1,1]). Devuelve false si se rechaza. */
    external fun nativeSensorFeedMic(pcm: FloatArray, n: Int): Boolean
    /** Objetivo por banda (forma deseada, dB, plano = 0). FloatArray[24]. */
    external fun nativeSensorSetTarget(targetDb: FloatArray): Boolean
    /** Preset élite + control de graves para el sensor (modo MEDIDO).
     *  preset: 0 Harman 2018 (default), 1 Harman élite-lean, 2 campo
     *  difuso H&M, 3 altavoces en sala. bassTrimDb: ±3 dB. */
    external fun nativeSensorSetEliteTarget(preset: Int, bassTrimDb: Float): Boolean
    /** Control de graves ±3 dB sobre el preset vigente. */
    external fun nativeSensorSetBassTrimDb(bassTrimDb: Float): Boolean
    /** Preset vigente del sensor (0..3, ver nativeSensorSetEliteTarget). */
    external fun nativeSensorTargetKind(): Int
    /** Último frame: FloatArray[74] = mag[0..23], err[24..47], coh[48..71],
     *  level[72], valid[73] (1/0). */
    external fun nativeSensorLatestFrame(): FloatArray
    /** Contador de frames publicados por el sensor. */
    external fun nativeSensorFrameIndex(): Long
    /** Activa/desactiva el bucle cerrado de corrección de sala en vivo
     *  (Ruta A). Apagado por defecto. Al apagar, el amount vuelve a 0. */
    external fun nativeSensorRoomCorrectionSetEnabled(enabled: Boolean)
    /** Cantidad de corrección 0..1 (0 = sin corrección, 1 = corrección total). */
    external fun nativeSensorRoomCorrectionSetAmount(amount: Float)
    /** [0] enabled 0/1, [1] amount 0..1, [2] frame válido 0/1,
     *  [3] coherencia media 0..1. */
    external fun nativeSensorRoomCorrectionStatus(): FloatArray

    // ── Hiper-Operador de Estado Φ (v2.5.369), Ruta A ─────────────────────────
    /** Cantidad 0..1 (0 = apagado, identidad bit-exacta, por defecto),
     *  preset de ruta 0 teléfono / 1 sala Genezi (Aux) / 2 auto (Aux),
     *  XTC on/off. Escribe los atomics g_hyper_* (Ruta A y Ruta B). */
    external fun nativeSetHyperAmount(amount: Float, preset: Int, xtcEnabled: Boolean)
    /** Curva objetivo élite + control de graves. kind: 0 Harman 2018
     *  (default), 1 Harman élite-lean, 2 campo difuso H&M, 3 altavoces en
     *  sala. bassTrimDb: ±3 dB. Aplica LA MISMA curva al operador (modo
     *  APROXIMADO) y al sensor (modo MEDIDO). */
    external fun nativeSetHyperTarget(kind: Int, bassTrimDb: Float)
    /** Telemetría: [0] amount, [1] mode (0 off / 1 aprox / 2 medido),
     *  [2] preset, [3] neuroActive 0/1, [4] spikeRate, [5] framesReceived. */
    external fun nativeHyperStatus(): FloatArray
    /** [0..2] ganancia dB bajo/medio/alto · [3] actividad de ataque 0..1 · [4] guarda activa 0/1 · [5] latencia ms. */
    external fun nativeGetPunchTelemetry(): FloatArray
    external fun nativeSetSpscRingFactor(factor: Float)
    external fun nativeGetSpscRingFactor(): Float

    // ── Eje Supremo Neuroacústico: CochlearActiveInverseEngine (PINN/OHC) ──────
    /** Activa/desactiva la Inversión Biomecánica Coclear Activa (Eje Supremo PINN).
     *  Thread-safe: escribe a un std::atomic<bool>; el hot-path lo lee con relaxed order. */
    external fun nativeSetCochlearInverseEnabled(enabled: Boolean)
    /** Establece la intensidad de descompresión OHC [0.0 .. 1.0].
     *  0.0 = bypass completo; 1.0 = inversión máxima de prestina. Latencia de UI: 0 ms. */
    external fun nativeSetCochlearIntensity(intensity: Float)
    /** Devuelve true si el motor coclear está activo Y el pipeline inicializado. */
    external fun nativeIsCochlearActive(): Boolean
    external fun nativeGetCochlearIntensity(): Float

    /** Reinicia el acumulador de medición de IvannaLab. */
    external fun nativeLabReset()
    /** Alimenta [frames] frames estéreo intercalados [L0,R0,L1,R1,...]. */
    external fun nativeLabFeed(interleavedStereo: FloatArray, frames: Int)
    /** [0]=thd% [1]=imd% [2]=integratedLUFS [3]=luRange [4]=snrDB [5]=peakDBFS [6]=truepeakDBTP */
    external fun nativeLabMeasure(): FloatArray?
    /** Reporte de texto generado por IvannaLab a partir del estado acumulado. */
    external fun nativeLabReport(): String

    // ── λ_t / RT60 adaptativo (AdaptiveDecisionEngine) ─────────────────────
    external fun nativeGetAdaptiveLambdaT(): Float
    external fun nativeSetAdaptiveEnvironmentRT60(rt60: Float)

    
    // ═══ FASE 3: Upmixing Inteligente HOA -> Binaural ═══
    external fun nativeSetIntelligentUpmixingEnabled(enabled: Boolean)
    external fun nativeSetUpmixingImmersivity(immersivity: Float)

    // ═══ Wave Field Synthesis (renderer ya implementado/testeado en
    // spatial/WfsRenderer.cpp — faltaba el cableado UI→JNI→daemon) ═══
    external fun nativeSetWfsEnabled(enabled: Boolean)
    /** Modo AUX/altavoces: campo de presion coherente (graves en fase). crossover 60..200 Hz, coherencia 0..1. */
    external fun nativeSetAuxField(enabled: Boolean, crossoverHz: Float, coherence: Float)

    /** Ruta AUX Genezi / auto (v2.5.366): preset completo — geometría XTC del
     *  par de altavoces + campo AUX coherente. preset: 0 = Genezi en habitación,
     *  1 = habitáculo (asiento del conductor, LHD). No aplica kernel Volterra
     *  (solo IVK1 medido GENEZY01) ni HRTF binaural (en AUX el wet es 0). */
    external fun nativeSetAuxGeneziPreset(preset: Int)

    /** Corrección acústica de sala: 10 ganancias (dB, una por banda de octava 31.5 Hz…16 kHz); todo 0 = apagado. Ruta A + daemon. */
    external fun nativeSetRoomCorrection(gainsDb: FloatArray)

    /** Motor NPE neuromórfico en el MÓDULO (Ruta B, omega_effect): encendido, inhibición lateral, compresión OHC, master (dB). */
    /** Ruta A: NPE neuromorfico en la cadena nativa de la app (paridad con el modulo). */
    external fun nativeSetNpeLocal(enabled: Boolean, harmonic: Float, lateralInhib: Float, ohcCompression: Float, masterDb: Float)
    external fun nativeSetNpeModule(enabled: Boolean, lateralInhib: Float, ohcCompression: Float, masterDb: Float): Boolean

    /**
     * Prueba de NULL del NPE (señal sintética, mismo núcleo que corre en el módulo): [residuo dB, cambio de ganancia dB,
     * coherencia, bandas audibles, delta dB sub/cuerpo/definición/presencia/aire]. Mide cuánto AÑADE el motor.
     */
    external fun nativeNpeAbNull(harmonicGain: Float, lateralInhib: Float, ohcCompression: Float, masterDb: Float): FloatArray?

    /** Amplitud (lineal) de la componente a `freqHz` en samples[0..count) — detección síncrona nativa (RoomProbe). */
    external fun nativeRoomLockIn(samples: FloatArray, count: Int, sampleRate: Int, freqHz: Float): Float

    /** Diseña la corrección a partir de 27 niveles (dB) y 27 SNR (dB). Devuelve 10 ganancias, o vacío si no hay referencia fiable. */
    external fun nativeRoomDesign(levelsDb: FloatArray, snrDb: FloatArray, sampleRate: Int): FloatArray

    /** Renderiza la geometría de sala en PPM hasta 3840×2160 dentro del directorio privado de la app. */
    external fun nativeRenderRoomGeometry(privateDirectory: String, width: Int = 3840, height: Int = 2160): Boolean

    /** 4D: dinámica espacial temporal — el lateral se recoge en cada golpe y se abre en lo sostenido. amount 0..1 (techo); adaptive = escala con la densidad de golpes. */
    external fun nativeSetTemporalField(enabled: Boolean, amount: Float, adaptive: Boolean)
    external fun nativeSetWfsSpread(spread: Float)
    external fun nativeSetWfsSpeakerLayout(x: FloatArray, y: FloatArray, z: FloatArray)

    // ── FIX: Métodos JNI que DSPBridge/HybridDecisionEngine necesitan ──────
        // Estos fueron llamados en DSPBridge.applyCompressorAmount(), etc.,
        // pero nunca estaban declarados en IvannaNativeLib — falta de sincronización
        // entre .kt declarations y .cpp implementation.
        external fun nativeSetPerceptualGain(gain: Float)
        external fun nativeSetCompressorAmount(amount: Float)
        external fun nativeSetExciterReduction(amount: Float)
        external fun nativeSetSpatialWidth(width: Float)
        external fun nativeSetPerceptualEQ(lowDb: Float, midDb: Float, highDb: Float)

        external fun nativeSetFatigueProtection(
            iso226Compensation: Float,
            fatigueProtection: Float
        )

    
    external fun nativeSetSampleRate(rate: Int): Boolean
    external fun nativeSetBitDepth(depth: Int): Boolean
    external fun nativeGetConfiguredSampleRate(): Int
    external fun nativeGetConfiguredBitDepth(): Int

    // ═══ Eje Supremo Neuroacústico: Inversión Biomecánica Coclear (PINN) ═══
    // Símbolos sin prefijo → IvannaSpatialNative / NativeBridge (ivanna_spatial_jni.cpp)
    // Símbolos nativeSet*  → ivanna_omega_jni.cpp hot-path (g_cochlearEnabled / g_cochlearIntensity)
    external fun setCochlearInverseEnabled(enabled: Boolean)
    external fun setCochlearIntensity(intensity: Float)
    external fun isCochlearActive(): Boolean
    external fun getCochlearIntensity(): Float
}
