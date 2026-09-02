package com.ivanna.omega.ai

import kotlin.math.*

enum class EmotionalState {
    CALM,
    EUPHORIA,
    FOCUS,
    ENERGETIC,
    NEUTRAL,
    INTIMATE
}

data class PsychoacousticAnalysis(
    val loudnessLUFS: Float,
    val iso226CompensationCurve: FloatArray, // 24 Bark bands
    val barkSpectrum: FloatArray,            // 24 Bark band energies
    val maskingThresholds: FloatArray,       // Spectral masking in dB
    val dynamicRangeDb: Float,
    val spectralTilt: Float
)

data class HearingFatigueState(
    val shortTermExposureDoseDbHr: Float,
    val cumulativeSessionFatigueScore: Float, // 0.0 (fresh) to 1.0 (exhausted)
    val hfProtectionAttenuationDb: Float
)

class PsychoacousticAnalyzer {

    // Center frequencies for 24 Bark Bands
    private val barkCenterFreqs = floatArrayOf(
        50f, 150f, 250f, 350f, 450f, 570f, 700f, 840f, 1000f, 1170f,
        1370f, 1600f, 1850f, 2150f, 2500f, 2900f, 3400f, 4000f, 4800f, 5800f,
        7000f, 8500f, 10500f, 13500f
    )

    // ── Espectro real (2026-08-29) ──────────────────────────────────────────
    // Antes el "espectro Bark" se FABRICABA: meanEnergy × (1 + 0.3·sin(b·0.5))
    // — una ondulación sinusoidal fija, idéntica para cualquier audio. El
    // enmascaramiento, el brillo, el tilt y en cascada la emoción, la fatiga
    // y las decisiones DSP operaban sobre un patrón decorativo, no sobre la
    // música. Ahora: FFT radix-2 de 1024 puntos sobre el bloque real y
    // agregación de bins por banda crítica (bordes = punto medio geométrico
    // entre centros Bark). Normalización por Parseval: la energía de banda
    // queda en la misma escala que la energía media temporal (un seno full
    // scale centrado en banda da -3.01 dB, igual que antes).
    private val fftN = 1024
    private val fftRe = FloatArray(fftN)
    private val fftIm = FloatArray(fftN)
    private val hann = FloatArray(fftN) { i ->
        (0.5 - 0.5 * cos(2.0 * PI * i / (fftN - 1))).toFloat()
    }
    // Bordes de banda (25 bordes para 24 bandas), punto medio geométrico
    private val barkEdges = FloatArray(25).also { e ->
        e[0] = 20f
        for (b in 1 until 24) e[b] = sqrt(barkCenterFreqs[b - 1] * barkCenterFreqs[b])
        e[24] = 18000f
    }

    // K-weighting aproximado para LUFS (BS.1770): pre-filtro high-shelf
    // (+4 dB sobre ~1.68 kHz) + high-pass RLB (~38 Hz). Primer orden — no es
    // el biquad exacto de la norma (ese vive en el LoudnessMeter C++), pero
    // captura el énfasis de agudos que la energía cruda ignoraba: dos
    // señales con igual RMS pero distinto contenido espectral ya no miden
    // el mismo loudness. Estado persistente entre llamadas (filtro IIR).
    private var kHpX1 = 0f; private var kHpY1 = 0f     // RLB high-pass 38 Hz
    private var kHsX1 = 0f; private var kHsY1 = 0f     // high-shelf: extracción de agudos 1.68 kHz
    private var kFiltersSr = 0
    private var kHpA = 0f; private var kHsA = 0f

    private fun ensureKWeighting(sr: Int) {
        if (sr == kFiltersSr) return
        kFiltersSr = sr
        val dt = 1.0 / sr
        val rcHp = 1.0 / (2.0 * PI * 38.0)     // RLB high-pass
        kHpA = (rcHp / (rcHp + dt)).toFloat()
        val rcHs = 1.0 / (2.0 * PI * 1680.0)   // corte del shelf
        kHsA = (rcHs / (rcHs + dt)).toFloat()
        kHpX1 = 0f; kHpY1 = 0f; kHsX1 = 0f; kHsY1 = 0f
    }

    // Filtro K-weighting por muestra: HP 38 Hz, luego shelf +4 dB de agudos.
    private fun kWeight(x: Float): Float {
        val hp = kHpA * (kHpY1 + x - kHpX1)
        kHpX1 = x; kHpY1 = hp
        val high = kHsA * (kHsY1 + hp - kHsX1)
        kHsX1 = hp; kHsY1 = high
        return hp + 0.585f * high   // +4 dB shelf ≈ factor (10^(4/20)-1)
    }

    private fun fftInPlace(re: FloatArray, im: FloatArray) {
        val n = re.size
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
            j = j xor bit
            if (i < j) {
                val tr = re[i]; re[i] = re[j]; re[j] = tr
                val ti = im[i]; im[i] = im[j]; im[j] = ti
            }
        }
        var len = 2
        while (len <= n) {
            val ang = -2.0 * PI / len
            val wRe = cos(ang).toFloat(); val wIm = sin(ang).toFloat()
            var i = 0
            while (i < n) {
                var cRe = 1f; var cIm = 0f
                val half = len / 2
                for (k in 0 until half) {
                    val uRe = re[i + k]; val uIm = im[i + k]
                    val vRe = re[i + k + half] * cRe - im[i + k + half] * cIm
                    val vIm = re[i + k + half] * cIm + im[i + k + half] * cRe
                    re[i + k] = uRe + vRe; im[i + k] = uIm + vIm
                    re[i + k + half] = uRe - vRe; im[i + k + half] = uIm - vIm
                    val nr = cRe * wRe - cIm * wIm; cIm = cRe * wIm + cIm * wRe; cRe = nr
                }
                i += len
            }
            len = len shl 1
        }
    }

    fun analyze(pcmBuffer: FloatArray, sampleRate: Int): PsychoacousticAnalysis {
        val numSamples = pcmBuffer.size
        if (numSamples == 0) {
            return PsychoacousticAnalysis(-70f, FloatArray(24), FloatArray(24), FloatArray(24), 0f, 0f)
        }
        ensureKWeighting(sampleRate)

        // 1. ITU-R BS.1770 K-weighted energy (filtro K-weighting + energía)
        var sumEnergy = 0.0
        for (i in 0 until numSamples) {
            val s = kWeight(pcmBuffer[i])
            sumEnergy += (s * s).toDouble()
        }
        val meanEnergy = (sumEnergy / numSamples).coerceAtLeast(1e-12)
        val lufs = (-0.691 + 10.0 * log10(meanEnergy)).toFloat().coerceIn(-80.0f, 0.0f)

        // 2. Espectro real → 24 bandas críticas Bark
        //    Ventana Hann sobre los primeros fftN samples (zero-pad si el
        //    bloque es más corto); la energía por banda usa Parseval con el
        //    espectro single-sided (×2 los bins interiores).
        for (i in 0 until fftN) {
            fftRe[i] = (if (i < numSamples) pcmBuffer[i] else 0f) * hann[i]
            fftIm[i] = 0f
        }
        fftInPlace(fftRe, fftIm)

        val barkEnergies = FloatArray(24)
        val iso226Curve = FloatArray(24)
        val binHz = sampleRate.toFloat() / fftN
        val invN2 = 1.0 / (fftN.toDouble() * fftN.toDouble())
        // Compensación de la ventana Hann (potencia ×2.63 coherente-gain)
        val winComp = 2.63
        var k = 1  // bin 0 (DC) excluido del reparto por bandas
        for (b in 0 until 24) {
            val fHi = min(barkEdges[b + 1], sampleRate * 0.5f)
            var power = 0.0
            while (k <= fftN / 2 - 1 && k * binHz < fHi) {
                val p = (fftRe[k] * fftRe[k] + fftIm[k] * fftIm[k]).toDouble()
                power += 2.0 * p   // single-sided
                k++
            }
            if (k >= fftN / 2) k = fftN / 2 - 1  // no desbordar en SR bajas
            val meanSqBand = power * invN2 * winComp
            barkEnergies[b] = (10.0 * log10(max(meanSqBand, 1e-12))).toFloat()

            val f = barkCenterFreqs[b]
            val isoDb = 3.64 * (f / 1000.0).pow(-0.8) - 6.5 * exp(-0.6 * (f / 1000.0 - 3.3).pow(2.0)) + 10.0.pow(-3.0) * (f / 1000.0).pow(4.0)
            iso226Curve[b] = isoDb.toFloat()
        }

        // 3. Umbrales de enmascaramiento reales: función de dispersión
        //    (spreading function) entre bandas Bark — una banda fuerte
        //    enmascara a sus vecinas con atenuación creciente por distancia
        //    (25 dB/Bark hacia abajo, 15 dB/Bark hacia arriba, aprox. de
        //    Schroeder). Antes: energía propia − (15 + 0.5·b), que ignoraba
        //    por completo a las bandas vecinas.
        val maskingThresholds = FloatArray(24)
        for (b in 0 until 24) {
            var th = -80f  // piso absoluto
            for (bp in 0 until 24) {
                val dz = b - bp
                val atten = if (dz >= 0) 25f * dz else 15f * (-dz)
                val cand = barkEnergies[bp] - atten
                if (cand > th) th = cand
            }
            maskingThresholds[b] = th - 3f  // offset conservador de excitación→umbral
        }

        // 4. Dynamic Range & Spectral Tilt (sobre el espectro real)
        val peak = pcmBuffer.maxOf { abs(it) }.coerceAtLeast(1e-6f)
        var sumRaw = 0.0
        for (i in 0 until numSamples) { val s = pcmBuffer[i]; sumRaw += (s * s).toDouble() }
        val rms = sqrt((sumRaw / numSamples).coerceAtLeast(1e-12)).toFloat()
        val dynamicRange = 20.0f * log10(peak / rms)
        val spectralTilt = barkEnergies[23] - barkEnergies[0]

        return PsychoacousticAnalysis(
            loudnessLUFS = lufs,
            iso226CompensationCurve = iso226Curve,
            barkSpectrum = barkEnergies,
            maskingThresholds = maskingThresholds,
            dynamicRangeDb = dynamicRange,
            spectralTilt = spectralTilt
        )
    }
}

class EmotionInferer {

    // Lightweight 3-Layer Dense Neural Network for TinyML Emotion Classification
    // Input: [loudnessNorm, dynamicRangeNorm, spectralTiltNorm, manualAdjustFreq, sessionDurationMinNorm]
    fun inferEmotion(
        analysis: PsychoacousticAnalysis,
        manualInteractionsLastMin: Int,
        sessionDurationMinutes: Float
    ): EmotionalState {
        val x0 = (analysis.loudnessLUFS + 80.0f) / 80.0f
        val x1 = (analysis.dynamicRangeDb / 40.0f).coerceIn(0f, 1f)
        val x2 = ((analysis.spectralTilt + 30.0f) / 60.0f).coerceIn(0f, 1f)
        val x3 = (manualInteractionsLastMin / 10.0f).coerceIn(0f, 1f)
        val x4 = (sessionDurationMinutes / 120.0f).coerceIn(0f, 1f)

        // Hidden Layer 1 (ReLU)
        val h1_0 = max(0f, 0.4f * x0 + 0.8f * x1 - 0.2f * x2 + 0.1f * x3 + 0.0f)
        val h1_1 = max(0f, -0.5f * x0 + 0.3f * x1 + 0.9f * x2 - 0.4f * x3 + 0.2f * x4)
        val h1_2 = max(0f, 0.2f * x0 - 0.6f * x1 + 0.1f * x2 + 0.8f * x3 + 0.5f * x4)

        // Output logits
        val scoreCalm = h1_0 * 1.2f - h1_2 * 0.8f
        val scoreEnergetic = h1_1 * 1.5f + h1_2 * 0.4f
        val scoreFocus = h1_0 * 0.9f + h1_1 * 0.6f - h1_2 * 0.5f
        val scoreEuphoria = h1_1 * 1.1f + h1_2 * 0.9f

        val scores = mapOf(
            EmotionalState.CALM to scoreCalm,
            EmotionalState.ENERGETIC to scoreEnergetic,
            EmotionalState.FOCUS to scoreFocus,
            EmotionalState.EUPHORIA to scoreEuphoria,
            EmotionalState.NEUTRAL to 0.5f,
            EmotionalState.INTIMATE to (scoreCalm * 0.7f + 0.3f)
        )

        return scores.maxByOrNull { it.value }?.key ?: EmotionalState.NEUTRAL
    }
}

class FatigueTracker {

    private var cumulativeDoseDbHr: Float = 0.0f

    fun updateFatigue(analysis: PsychoacousticAnalysis, deltaSeconds: Float): HearingFatigueState {
        // WHO/ITU auditory exposure model: Reference threshold 80 LUFS
        val excessLoudness = (analysis.loudnessLUFS - (-18.0f)).coerceAtLeast(0.0f)
        val doseIncrement = (excessLoudness / 10.0f).pow(2.0f) * (deltaSeconds / 3600.0f)
        cumulativeDoseDbHr += doseIncrement

        val fatigueScore = (cumulativeDoseDbHr / 10.0f).coerceIn(0.0f, 1.0f)
        val hfProtectionAttenuation = if (fatigueScore > 0.6f) {
            -1.0f * (fatigueScore - 0.6f) * 15.0f // Up to -6 dB attenuation above 4kHz
        } else {
            0.0f
        }

        return HearingFatigueState(
            shortTermExposureDoseDbHr = cumulativeDoseDbHr,
            cumulativeSessionFatigueScore = fatigueScore,
            hfProtectionAttenuationDb = hfProtectionAttenuation
        )
    }

    fun resetSession() {
        cumulativeDoseDbHr = 0.0f
    }
}
