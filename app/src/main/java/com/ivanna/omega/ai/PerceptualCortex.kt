package com.ivanna.omega.ai

import android.util.Log

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

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


        lastAnalysis = analysis
        lastEmotion = emotion
        lastFatigue = fatigue


        val dsp =
            barkToDSP(
                analysis,
                fatigue
            )


        return PerceptualState(
            analysis,
            emotion,
            fatigue,
            dsp
        )
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
                    .coerceIn(0f,1f)
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
            val snapshot = PerceptualSnapshot(
                fatigue              = lastFatigue?.fatigueScore   ?: 0f,
                immersion            = ctrl.spatial,
                iso226LoudnessDb     = analysis.loudnessDb,
                maskingEfficiency    = analysis.maskingEfficiency,
                dynamicRangeDb       = analysis.dynamicRangeDb,
                convNextConfidence   = analysis.confidence,
                emotion              = when (lastEmotion) {
                    EmotionalState.EXCITED -> 1.0f
                    EmotionalState.CALM    -> 0.3f
                    EmotionalState.TENSE   -> 0.7f
                    else                   -> 0.5f
                }
            )
            val decision = decisionEngine.evaluate(snapshot)
            decisionEngine.dispatchDecision(decision)
        }.onFailure { Log.w("PerceptualCortex", "dispatch error: ${it.message}") }

        return ctrl
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

    val spatial: Float
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


data class PerceptualSnapshot(
    val fatigue:            Float,
    val immersion:          Float,
    val iso226LoudnessDb:   Float,
    val maskingEfficiency:  Float,
    val dynamicRangeDb:     Float,
    val convNextConfidence: Float,
    val emotion:            Float
)
