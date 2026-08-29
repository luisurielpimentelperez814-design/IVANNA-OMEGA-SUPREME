package com.ivanna.omega.ai
import com.ivanna.omega.ai.SAFCore

import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.saf.SaFRoomBridge
// coerceIn is stdlib — no import needed

class PerceptualDecisionEngine {
    private var currentProfile: UserProfile = UserProfile()
    // FIX (fatiga temporal siempre 0): durationMin era constante 0f —
    // la componente temporal del fatigueWeight nunca contribuía.
    // Ahora se mide desde que se instanció el engine (inicio de sesión).
    private val sessionStartMs = System.currentTimeMillis()

    fun updateProfile(profile: UserProfile) {
        this.currentProfile = profile
    }

    fun evaluate(snapshot: PerceptualSnapshot): DSPDecision {
        val fatigueFactor = snapshot.fatigue.coerceIn(0f, 1f)
        val immersionFactor = snapshot.immersion.coerceIn(0f, 1f)
        val loudnessIso226 = snapshot.iso226LoudnessDb
        val maskingEff = snapshot.maskingEfficiency
        val dynRange = snapshot.dynamicRangeDb
        val confidence = snapshot.convNextConfidence.coerceIn(0f, 1f)
        val mood = snapshot.emotion.coerceIn(0f, 1f)
        val envNoise = snapshot.iso226LoudnessDb.coerceIn(0f, 100f)
        // Duración real de la sesión en minutos — contribuye a la fatiga temporal.
        // El modelo de fatiga ITU-R BS.1770 considera que la fatiga auditiva
        // crece con el tiempo de exposición continua a niveles altos.
        val durationMin = (System.currentTimeMillis() - sessionStartMs) / 60_000f

        val aggress = currentProfile.aggressiveness

        // ISO 226 & ITU-R BS.1770 Equal Loudness & Fatigue Mitigation Calculation
        val fatigueWeight = (fatigueFactor * 0.6f + (durationMin / 120f).coerceIn(0f, 1f) * 0.4f) * currentProfile.fatigueSensitivity
        val exciterRed = (fatigueWeight * 0.6f * aggress + (1.0f - confidence) * 0.2f).coerceIn(0f, 0.85f)
        val highCut = if (fatigueWeight > 0.4f) (20000f - fatigueWeight * 5000f * aggress).coerceIn(12000f, 20000f) else 20000f
        
        // Dynamic Range & Multiband Compressor target
        val compAmount = (0.25f + (1.0f - (dynRange / 24f).coerceIn(0f, 1f)) * 0.4f + fatigueWeight * 0.25f + aggress * 0.1f).coerceIn(0.1f, 0.95f)
        
        // HRTF Spatial Width
        val spatialWidth = (immersionFactor * currentProfile.spatialPreference * (1.0f - fatigueWeight * 0.25f)).coerceIn(0.5f, 2.0f)
        
        // Environment Noise & ISO 226 Loudness target (LUFS)
        val noiseBoost = (envNoise - 40f).coerceAtLeast(0f) * 0.1f
        val targetLoudness = (currentProfile.preferredLoudnessTarget + (loudnessIso226 - 84f) * 0.08f + maskingEff * 1.5f + noiseBoost).coerceIn(-24.0f, -8.0f)
        
        val moodColoration = (mood * 0.5f + (1f - fatigueWeight) * 0.5f).coerceIn(0f, 1f)
        val harmonicGain = ((1.0f - exciterRed) * (1.0f + currentProfile.treblePreferenceDb * 0.05f)).coerceIn(0.2f, 1.5f)
        val antiDolby = (compAmount * 0.8f + aggress * 0.2f).coerceIn(0.1f, 1.0f)

        val sMode = when {
            spatialWidth > 1.4f -> "3D_HOLOGRAM"
            spatialWidth > 0.9f -> "3D_SURROUND"
            else -> "STEREO_NEUTRAL"
        }

        return DSPDecision(
            compressorAmount = compAmount,
            exciterReduction = exciterRed,
            eqHighCut = highCut,
            spatialWidth = spatialWidth,
            loudnessTarget = targetLoudness,
            moodAdaptation = moodColoration,
            spatialMode = sMode,
            harmonicGain = harmonicGain,
            antiDolbyIntensity = antiDolby,
            confidence = (confidence * 0.7f + 0.3f).coerceIn(0f, 1f),
            executionLatencyMs = 0.8f + (1.0f - confidence) * 0.5f
        )
    }

    fun dispatchDecision(decision: DSPDecision, rt60: Float = 0.3f) {
        // FIX: usar SAFCore.stepRoom() — Φ_SAF-Room^∞ con M_t = G_t + λ_t·I.
        // dispatchDecision usaba SAFCore.update() directamente (sin acoplamiento
        // de sala). Ahora M_t se regula según rt60 (T60 de la sala), hMismatch
        // (exciterReduction proxea distorsión HRTF) y diffuseness (spatialWidth).
        val hMismatch   = decision.exciterReduction.coerceIn(0f, 1f)
        val diffuseness = ((decision.spatialWidth - 0.5f) / 1.5f).coerceIn(0f, 1f)

        val (safState, alphaRoom) = SAFCore.stepRoom(
            current = doubleArrayOf(
                decision.compressorAmount.toDouble(),
                decision.exciterReduction.toDouble(),
                decision.eqHighCut.toDouble(),
                decision.spatialWidth.toDouble()
            ),
            target = doubleArrayOf(0.5, 0.0, 16000.0, 1.0),
            metric = doubleArrayOf(1.0, 1.0, 1.0, 1.0),
            rt60        = rt60,
            hMismatch   = hMismatch,
            diffuseness = diffuseness
        )

        val safTelemetry = SAFCore.getState()

        OmegaEngineBridge.pushSAFState(
            safTelemetry[0].toFloat(),
            safTelemetry[1].toFloat(),
            safTelemetry[2].toFloat(),
            // gain modulado por α*(R,H,S) — conservador en sala incierta
            (safTelemetry[3] * alphaRoom.coerceIn(0f, 1f)).toFloat()
        )

        OmegaEngineBridge.sendPerceptualState(
            compressor     = safState[0].toFloat(),
            exciterRed     = safState[1].toFloat(),
            highCut        = safState[2].toFloat(),
            spatialWidth   = safState[3].toFloat(),
            loudnessTarget = decision.loudnessTarget,
            harmonicGain   = decision.harmonicGain,
            antiDolby      = decision.antiDolbyIntensity
        )

        // Actualizar SaFRoomBridge con el resultado de la decisión perceptual
        runCatching {
            SaFRoomBridge.setRoomState(rt60 = rt60, drr = 6.0f)
            SaFRoomBridge.setHrtfState(mismatchEnergy = hMismatch)
            SaFRoomBridge.setSoundFieldState(diffuseness = diffuseness)
        }
    }
}
