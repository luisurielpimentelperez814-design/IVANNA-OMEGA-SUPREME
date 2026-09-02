package com.ivanna.omega.ai

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

// λ_t adaptativo: API nativa retirada (ver IvannaNativeLib). Sentinela negativa
// = "no disponible", exactamente el contrato que devolvía el external fun.
private const val ADAPTIVE_LAMBDA_T_UNAVAILABLE = -1f

/**
 * PerceptualCortex
 *
 * Orquestador superior del cerebro perceptual IVANNA.
 *
 * Flujo:
 *
 * Audio Telemetry
 *        |
 *        v
 * PsychoacousticAnalyzer
 *        |
 *        +--> EmotionInferer
 *        |
 *        +--> FatigueTracker
 *        |
 *        v
 * Adaptive Decision Layer
 *        |
 *        v
 * SAF Optimizer
 *        |
 *        v
 * DSP Runtime Parameters
 */
class PerceptualCortex {

    private val decisionEngine = PerceptualDecisionEngine()
    private val psychoacousticAnalyzer =
        PsychoacousticAnalyzer()

    private val emotionInferer =
        EmotionInferer()

    private val fatigueTracker =
        FatigueTracker()

    // ── AUDIT FIX PR 1: Listeners para notificar cambios de estado ──────────
    private val listeners = mutableListOf<PerceptualStateListener>()
    // FIX (auditoría 2026-08-24): elapsedRealtimeNanos es monotónico —
    // currentTimeMillis saltaba (NTP/cambio de hora) y podía dar deltaMs
    // negativo a los listeners de fatiga/timing.
    private var lastTimestampMs = android.os.SystemClock.elapsedRealtimeNanos() / 1_000_000

    private var lastAnalysis:
            PsychoacousticAnalysis? = null

    private var lastEmotion:
            EmotionalState = EmotionalState.NEUTRAL

    private var lastFatigue:
            HearingFatigueState? = null


    fun process(
        pcm: FloatArray,
        sampleRate: Int,
        manualInteractions: Int = 0,
        sessionMinutes: Float = 0f,
        deltaSeconds: Float = 0.02f
    ): PerceptualState {


        val analysis =
            psychoacousticAnalyzer.analyze(
                pcm,
                sampleRate
            )


        val emotion =
            emotionInferer.inferEmotion(
                analysis,
                manualInteractions,
                sessionMinutes
            )


        val fatigue =
            fatigueTracker.updateFatigue(
                analysis,
                deltaSeconds
            )

        // ── AUDIT FIX PR 7: Aplicar λ_t adaptativo a la fatiga ────────────────
        val adjustedFatigue = applyAdaptiveLambdaT(fatigue)

        lastAnalysis = analysis
        lastEmotion = emotion
        lastFatigue = adjustedFatigue


        val dsp =
            barkToDSP(
                analysis,
                adjustedFatigue
            )

        // ── AUDIT FIX PR 1: Crear estado y notificar listeners ──────────────
        val state = PerceptualState(
            analysis,
            emotion,
            fatigue,
            dsp
        )

        // Calcular delta desde última llamada
        val nowMs = android.os.SystemClock.elapsedRealtimeNanos() / 1_000_000
        val deltaMs = nowMs - lastTimestampMs
        lastTimestampMs = nowMs

        // Notificar a todos los listeners (IvannaBridgePlayer, DSPBridge, etc.)
        notifyListeners(state, deltaMs)

        return state
    }



    /**
     * Bark psychoacoustic domain
     * ->
     * DSP runtime domain
     *
     * Valores normalizados:
     *
     * gain      0..1
     * compressor 0..1
     * exciter   0..1
     * spatial   0..1
     */
    private fun barkToDSP(
        analysis: PsychoacousticAnalysis,
        fatigue: HearingFatigueState
    ): DSPPerceptualControl {


        var low = 0f
        var mid = 0f
        var high = 0f


        for(i in analysis.barkSpectrum.indices){

            when(i){

                in 0..7 ->
                    low += analysis.barkSpectrum[i]

                in 8..15 ->
                    mid += analysis.barkSpectrum[i]

                else ->
                    high += analysis.barkSpectrum[i]
            }
        }


        low /= 8f
        mid /= 8f
        high /= 8f


        val brightness =
            ((high - low) / 40f)
                .coerceIn(-1f,1f)


        val protection =
            (-fatigue.hfProtectionAttenuationDb / 15f)
                .coerceIn(0f,1f)



        // ── ISO226 + Fatigue perceptual EQ ─────────────────────────────
        val isoComp =
            ((analysis.loudnessLUFS + 60f) / 60f)
                .coerceIn(0f, 1f)

        val fatigueEqCut =
            protection * -6f

        val lowComp =
            ((1f - isoComp) * 6f)
                .coerceIn(0f, 6f)

        val midComp =
            (fatigueEqCut * 0.5f)
                .coerceIn(-6f, 0f)

        val highComp =
            fatigueEqCut
                .coerceIn(-12f, 0f)


        val ctrl = DSPPerceptualControl(

            gain =
                (1f - protection * 0.2f)
                    .coerceIn(0.5f,1f),

            compressor =
                (analysis.dynamicRangeDb / 40f)
                    .coerceIn(0f,1f),

            exciter =
                (0.5f + brightness * 0.25f)
                    .coerceIn(0f,1f),

            spatial =
                (0.5f + abs(brightness) * 0.2f)
                    .coerceIn(0f,1f),

            lowEqDb = lowComp,

            midEqDb = midComp,

            highEqDb = highComp,

            iso226Compensation = isoComp,

            fatigueProtection = protection
        )

        // FIX: PerceptualCortex calculaba DSPPerceptualControl pero nunca
        // lo entregaba a PerceptualDecisionEngine. El loop perceptual estaba
        // abierto — PDE.evaluate() y PDE.dispatchDecision() nunca corrían
        // desde el proceso de audio, solo desde PerceptualBrainEngine (10Hz).
        //
        // Ahora PerceptualCortex cierra el loop: convierte DSPPerceptualControl
        // en un PerceptualSnapshot y lo entrega a PDE en el mismo bloque PCM.
        // Esto da latencia de decisión < 10ms (un bloque de audio) en vez de
        // 100ms (el timer de PerceptualBrainEngine).
        runCatching {
            // FIX (build, log CI 84498918857): el bloque original referenciaba
            // miembros inexistentes — verificado contra las declaraciones reales:
            //   lastFatigue.fatigueScore → HearingFatigueState solo expone
            //     cumulativeSessionFatigueScore (PerceptualBrainCortex.kt:23)
            //   analysis.loudnessDb → el campo real es loudnessLUFS
            //   analysis.maskingEfficiency / analysis.confidence → NO existen en
            //     PsychoacousticAnalysis (PerceptualBrainCortex.kt:14); la
            //     eficiencia de enmascaramiento se deriva honestamente como la
            //     fracción de bandas Bark cuya energía supera su umbral de
            //     enmascaramiento, y convNextConfidence queda en 0f (este path
            //     no corre el clasificador TinyML — 0 = "sin dato", no mentira)
            //   EmotionalState.EXCITED/TENSE → el enum real es
            //     CALM/EUPHORIA/FOCUS/ENERGETIC/NEUTRAL/INTIMATE
            //   Además la data class local PerceptualSnapshot (línea ~255) se
            //   elimina: duplicaba la de PerceptualBrainEngine.kt:64 (mismo
            //   paquete). La rica tiene defaults en todos los campos, así que
            //   basta pasar los named args que este path realmente conoce.
            val maskedBands = run {
                var above = 0
                val n = minOf(analysis.barkSpectrum.size, analysis.maskingThresholds.size)
                for (b in 0 until n) {
                    if (analysis.barkSpectrum[b] > analysis.maskingThresholds[b]) above++
                }
                if (n > 0) above.toFloat() / n.toFloat() else 0f
            }
            val snapshot = PerceptualSnapshot(
                fatigue              = lastFatigue?.cumulativeSessionFatigueScore ?: 0f,
                immersion            = ctrl.spatial,
                iso226LoudnessDb     = analysis.loudnessLUFS,
                maskingEfficiency    = maskedBands,
                dynamicRangeDb       = analysis.dynamicRangeDb,
                convNextConfidence   = 0f,
                emotion              = when (lastEmotion) {
                    EmotionalState.EUPHORIA  -> 1.0f
                    EmotionalState.ENERGETIC -> 0.85f
                    EmotionalState.FOCUS     -> 0.7f
                    EmotionalState.NEUTRAL   -> 0.5f
                    EmotionalState.INTIMATE  -> 0.4f
                    EmotionalState.CALM      -> 0.3f
                }
            )
            val decision = decisionEngine.evaluate(snapshot)
            decisionEngine.dispatchDecision(decision)
        }.onFailure { Log.w("PerceptualCortex", "dispatch error: ${it.message}") }

        return ctrl
    }

    // ── AUDIT FIX PR 1: Listener management methods ─────────────────────────
    /**
     * Agregar un listener para recibir notificaciones de cambios de estado.
     * Se usa para conectar componentes como IvannaBridgePlayer, DSPBridge, etc.
     *
     * @param listener que implementa PerceptualStateListener
     */
    fun addStateListener(listener: PerceptualStateListener) {
        if (!listeners.contains(listener)) {
            listeners.add(listener)
            Log.d("PerceptualCortex", "Listener agregado: ${listener.javaClass.simpleName}")
        }
    }

    /**
     * Remover un listener.
     *
     * @param listener a remover
     */
    fun removeStateListener(listener: PerceptualStateListener) {
        listeners.remove(listener)
        Log.d("PerceptualCortex", "Listener removido: ${listener.javaClass.simpleName}")
    }

    /**
     * Notificar a todos los listeners de un cambio de estado.
     * Llamado automáticamente dentro de process().
     *
     * @param state nuevo PerceptualState calculado
     * @param deltaMs tiempo desde última llamada
     */
    // ── AUDIT FIX PR 7: Aplicar λ_t adaptativo a la fatiga ───────────────────
    /**
     * Ajustar fatiga basándose en λ_t calculado por AdaptiveDecisionEngine.
     * λ_t modula cuán rápido se acumula/recupera la fatiga.
     * Rango: 0.0 (no hay adaptación) a 1.0 (máxima adaptación).
     *
     * @param baseFatigue fatiga base calculada por FatigueTracker
     * @return fatiga ajustada por λ_t
     */
    private fun applyAdaptiveLambdaT(baseFatigue: HearingFatigueState): HearingFatigueState {
        try {
            if (!IvannaNativeLib.isLoaded) return baseFatigue

            // API retirada (ver IvannaNativeLib): el motor nativo no expone λ_t.
            // Mismo comportamiento que el camino "λ_t no disponible" de antes.
            val lambdaT = ADAPTIVE_LAMBDA_T_UNAVAILABLE
            if (lambdaT < 0) return baseFatigue  // No disponible

            Log.d("PerceptualCortex", "applyAdaptiveLambdaT: λ_t=$lambdaT")

            // Aplicar λ_t como factor de modulación
            // Valores altos de λ_t reducen la fatiga, valores bajos la aumentan
            val adjustedLevel = (baseFatigue.cumulativeSessionFatigueScore * (1f - lambdaT * 0.3f))
                .coerceIn(0f, 1f)

            return baseFatigue.copy(
                cumulativeSessionFatigueScore = adjustedLevel
            )
        } catch (e: Exception) {
            Log.e("PerceptualCortex", "Error aplicando λ_t: ${e.message}")
            return baseFatigue
        }
    }

    private fun notifyListeners(state: PerceptualState, deltaMs: Long) {
        listeners.forEach { listener ->
            try {
                listener.onPerceptualStateChanged(state, deltaMs)
            } catch (e: Exception) {
                Log.e("PerceptualCortex", "Error notificando listener: ${e.message}")
            }
        }
    }

    fun reset(){
        fatigueTracker.resetSession()
        lastAnalysis = null
        lastEmotion = EmotionalState.NEUTRAL
        lastFatigue = null
    }
}



data class DSPPerceptualControl(

    val gain: Float,

    val compressor: Float,

    val exciter: Float,

    val spatial: Float,

    // Canales adaptativos perceptuales
    // ISO226 + protección por fatiga auditiva
    val lowEqDb: Float = 0f,

    val midEqDb: Float = 0f,

    val highEqDb: Float = 0f,

    val iso226Compensation: Float = 0f,

    val fatigueProtection: Float = 0f
)



data class PerceptualState(

    val psychoacoustic:
        PsychoacousticAnalysis,

    val emotion:
        EmotionalState,

    val fatigue:
        HearingFatigueState,

    val dsp:
        DSPPerceptualControl
)


// (PerceptualSnapshot local eliminada — duplicaba la de PerceptualBrainEngine.kt:64;
//  mismo paquete com.ivanna.omega.ai → Redeclaration. Ver fix arriba.)
