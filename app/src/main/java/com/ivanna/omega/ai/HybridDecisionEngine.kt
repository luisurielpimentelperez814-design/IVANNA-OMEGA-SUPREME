package com.ivanna.omega.ai

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

// No duplicate DSPDecision here — use the single DSPDecision in DSPDecision.kt

enum class SpatialMode {
    STEREO,
    SURROUND_5_1,
    BINAURAL_3D,
    ATMOS_OBJECTS
}

class QLearningAgent {

    // FIX (2026-08-29, agente roto):
    //  a) El bootstrap usaba el Q ACTUAL como si fuera max Q(s',·) —
    //     Q+α(r+γQ−Q) no es Q-learning, es un filtro con auto-decaimiento.
    //     Los callers no proveen estado siguiente, así que el modelo honesto
    //     es bandido contextual: Q(s,a) += α·(r − Q(s,a)).
    //  b) fatigueBucket se calculaba en el engine pero NUNCA indexaba la
    //     tabla — la mitad del estado se descartaba. Ahora el estado es
    //     (emoción × fatiga) = 6×5 = 30 contextos.
    //  c) Argmax puro con tabla a ceros = acción 0 para siempre; jamás se
    //     exploraban las acciones 1-4. Ahora ε-greedy (ε = 10%).
    private val numEmotions = 6
    private val numFatigueBuckets = 5
    private val numActions = 5
    private val qTable = Array(numEmotions * numFatigueBuckets) { FloatArray(numActions) }
    private val learningRate = 0.1f
    private val epsilon = 0.10f

    private fun stateIdx(emotion: EmotionalState, fatigueBucket: Int): Int {
        val fb = fatigueBucket.coerceIn(0, numFatigueBuckets - 1)
        return emotion.ordinal * numFatigueBuckets + fb
    }

    fun selectAction(emotion: EmotionalState, fatigueBucket: Int): Int {
        val actions = qTable[stateIdx(emotion, fatigueBucket)]

        // ε-greedy: explorar una acción uniforme al azar con prob. ε
        if (kotlin.random.Random.nextFloat() < epsilon) {
            return kotlin.random.Random.nextInt(numActions)
        }

        var maxAction = 0
        var maxQ = actions[0]
        for (i in 1 until actions.size) {
            if (actions[i] > maxQ) {
                maxQ = actions[i]
                maxAction = i
            }
        }
        return maxAction
    }

    fun updateQ(
        emotion: EmotionalState,
        fatigueBucket: Int,
        action: Int,
        reward: Float
    ) {
        if (action < 0 || action >= numActions) return
        val s = stateIdx(emotion, fatigueBucket)
        val currentQ = qTable[s][action]
        // Bandido contextual: recompensa inmediata como target (sin bootstrap
        // falso). Converge al valor esperado de la recompensa del usuario
        // para esa acción en ese contexto (emoción, fatiga).
        qTable[s][action] = currentQ + learningRate * (reward - currentQ)
    }
}

class HybridDecisionEngine {

    private val psychoacousticAnalyzer = PsychoacousticAnalyzer()
    private val emotionInferer = EmotionInferer()
    private val fatigueTracker = FatigueTracker()
    private val qAgent = QLearningAgent()

    fun computeDecision(
        pcmBuffer: FloatArray,
        sampleRate: Int,
        manualInteractions: Int,
        sessionDurationMin: Float,
        userBassPreference: Float,
        userTreblePreference: Float
    ): DSPDecision {

        val psychoAnalysis =
            psychoacousticAnalyzer.analyze(pcmBuffer, sampleRate)

        val emotion =
            emotionInferer.inferEmotion(
                psychoAnalysis,
                manualInteractions,
                sessionDurationMin
            )

        val fatigue =
            fatigueTracker.updateFatigue(
                psychoAnalysis,
                1.0f
            )

        val fatigueBucket =
            (fatigue.cumulativeSessionFatigueScore * 4)
                .toInt()
                .coerceIn(0, 4)

        val qAction =
            qAgent.selectAction(emotion, fatigueBucket)

        val lufsDeficit =
            (-14.0f - psychoAnalysis.loudnessLUFS)
                .coerceIn(-12.0f, 12.0f)

        var eqLow =
            lufsDeficit * 0.4f + (userBassPreference * 4.0f)

        var eqMid = 0.0f

        var eqHigh =
            lufsDeficit * 0.2f + (userTreblePreference * 4.0f)

        when (qAction) {
            1 -> {
                eqLow += 1.5f
                eqHigh += 1.0f
            }

            2 -> {
                eqMid += 2.0f
            }

            3 -> {
                eqHigh += 2.5f
            }

            4 -> {
                eqLow -= 2.0f
                eqHigh -= 2.0f
            }
        }

        eqHigh += fatigue.hfProtectionAttenuationDb

        val compression =
            if (psychoAnalysis.dynamicRangeDb > 20.0f) {
                ((psychoAnalysis.dynamicRangeDb - 20.0f) / 20.0f)
                    .coerceIn(0.1f, 0.8f)
            } else {
                0.05f
            }

        val (spatialWidth, mode, room) =
            when (emotion) {

                EmotionalState.EUPHORIA ->
                    Triple(1.6f, SpatialMode.ATMOS_OBJECTS, 0.7f)

                EmotionalState.CALM ->
                    Triple(1.1f, SpatialMode.BINAURAL_3D, 0.4f)

                EmotionalState.FOCUS ->
                    Triple(0.9f, SpatialMode.STEREO, 0.1f)

                EmotionalState.ENERGETIC ->
                    Triple(1.4f, SpatialMode.SURROUND_5_1, 0.5f)

                EmotionalState.INTIMATE ->
                    Triple(1.2f, SpatialMode.BINAURAL_3D, 0.3f)

                EmotionalState.NEUTRAL ->
                    Triple(1.0f, SpatialMode.STEREO, 0.2f)
            }

        val confidence =
            (0.85f + 0.10f *
                    (1.0f - fatigue.cumulativeSessionFatigueScore))
                .coerceIn(0.5f, 0.99f)

        val eqLowDb = eqLow.coerceIn(-12f, 12f)
        val eqMidDb = eqMid.coerceIn(-12f, 12f)
        val eqHighDb = eqHigh.coerceIn(-12f, 12f)

        // ── AUDIT FIX PR 5: Enviar parámetros EQ al nativo ──────────────────
        applyEQToNative(eqLowDb, eqMidDb, eqHighDb)

        return DSPDecision(
            compressorAmount = compression,
            exciterReduction =
                (fatigue.cumulativeSessionFatigueScore * 0.5f)
                    .coerceIn(0.0f, 0.8f),

            eqLowDb = eqLowDb,
            eqMidDb = eqMidDb,
            eqHighDb = eqHighDb,

            spatialWidth = spatialWidth,
            loudnessTargetLUFS = -14.0f,
            fatigueProtectionDb = fatigue.hfProtectionAttenuationDb,

            moodAdaptation = 0.2f * emotion.ordinal,

            spatialMode = mode.name,

            roomSize = room,
            headTrackingEnabled =
                mode == SpatialMode.BINAURAL_3D ||
                mode == SpatialMode.ATMOS_OBJECTS,

            confidenceScore = confidence
        )
    }

    fun provideUserFeedbackReward(
        emotion: EmotionalState,
        fatigueBucket: Int,
        action: Int,
        positiveReward: Boolean
    ) {
        val reward =
            if (positiveReward) 1.0f else -1.5f

        qAgent.updateQ(
            emotion,
            fatigueBucket,
            action,
            reward
        )
    }

    // ── AUDIT FIX PR 5: Método para aplicar EQ calculado a la librería nativa ──
    /**
     * Enviar parámetros de EQ calculados por HybridDecisionEngine al nativo.
     * Estos parámetros se aplican dinámicamente durante el procesamiento DSP.
     *
     * @param lowDb ganancia en bajos (-12..+12 dB)
     * @param midDb ganancia en medios (-12..+12 dB)
     * @param highDb ganancia en altos (-12..+12 dB)
     */
    private fun applyEQToNative(lowDb: Float, midDb: Float, highDb: Float) {
        try {
            if (IvannaNativeLib.isLoaded) {
                Log.d("HybridDecisionEngine", "applyEQToNative: low=$lowDb, mid=$midDb, high=$highDb")
                IvannaNativeLib.nativeSetEQParams(
                    lowDb.coerceIn(-12f, 12f),
                    midDb.coerceIn(-12f, 12f),
                    highDb.coerceIn(-12f, 12f),
                    master = 0f
                )
            }
        } catch (e: Exception) {
            Log.e("HybridDecisionEngine", "Error aplicando EQ al nativo: ${e.message}")
        }
    }
}
