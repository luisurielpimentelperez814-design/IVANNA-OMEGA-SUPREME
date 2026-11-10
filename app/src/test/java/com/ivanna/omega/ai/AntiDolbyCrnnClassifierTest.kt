package com.ivanna.omega.ai

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Tests de lógica pura del clasificador CRNN Anti-Dolby (sin modelo TFLite
 * ni Android): heurística de respaldo y decisión de transitorio.
 *
 * Regresiones 2026-11-09 (music = 0.00 en vivo con música sonando):
 *  1. La heurística anterior formaba los logits con log-energías absolutas
 *     (siempre negativas) y daba music ≈ 0.00 para CUALQUIER entrada.
 *  2. El override por onset 0.15 se disparaba con casi cada ventana de rock
 *     y la EMA 0.85 lo sostenía segundos, starvando music hacia 0.10.
 * Estos tests reproducen ambas (fallan con la lógica vieja) y confirman el fix.
 *
 * NOTA: el bypass de EMA del frame transitorio no se puede probar aquí sin
 * instanciar el clasificador (necesita Context + TFLite nativo); lo verifica
 * la CI en dispositivo/emulador (logcat IVANNA_CMD: music debe recuperarse
 * ~2-3 s después de un golpe, no quedarse en 0.10).
 */
class AntiDolbyCrnnClassifierTest {

    /** Frame log-mel sintético: graves/medios/presencia/aire constantes por banda. */
    private fun frame(bass: Float, mid: Float, presence: Float, air: Float): FloatArray {
        val f = FloatArray(AntiDolbyCrnnClassifier.N_MELS)
        for (m in 0 until 8) f[m] = bass
        for (m in 8 until 20) f[m] = mid
        for (m in 20 until 32) f[m] = presence
        for (m in 32 until AntiDolbyCrnnClassifier.N_MELS) f[m] = air
        return f
    }

    @Test fun fallbackHeuristicDetectsSungRockAsMusic() {
        // Rock con canto: graves + presencia + medios, poco aire.
        val p = CrnnFallbackHeuristic.probs(frame(-4f, -5f, -4f, -7f))
        assertEquals(1f, p.sum(), 1e-5f)
        assertTrue(p.all { it.isFinite() && it in 0f..1f })
        assertTrue(p[AntiDolbyCrnnClassifier.IDX_MUSIC] > 0.5f)
        assertTrue(p[AntiDolbyCrnnClassifier.IDX_MUSIC] > p[AntiDolbyCrnnClassifier.IDX_VOICE])
    }

    @Test fun fallbackHeuristicDetectsElectronicMusic() {
        val p = CrnnFallbackHeuristic.probs(frame(-3f, -6f, -5f, -8f))
        assertTrue(p[AntiDolbyCrnnClassifier.IDX_MUSIC] > 0.9f)
        assertTrue(p[AntiDolbyCrnnClassifier.IDX_VOICE] < 0.05f)
    }

    @Test fun fallbackHeuristicDetectsSpokenVoice() {
        // Voz hablada: medios dominantes, sin graves ni aire.
        val p = CrnnFallbackHeuristic.probs(frame(-9f, -4f, -7f, -9f))
        assertTrue(p[AntiDolbyCrnnClassifier.IDX_VOICE] > 0.9f)
        assertTrue(p[AntiDolbyCrnnClassifier.IDX_MUSIC] < 0.01f)
    }

    @Test fun fallbackHeuristicIsLevelIndependent() {
        // Misma forma espectral a -2 dB y a -8 dB: misma clasificación.
        val loud = CrnnFallbackHeuristic.probs(frame(-2f, -3f, -2f, -5f))
        val quiet = CrnnFallbackHeuristic.probs(frame(-8f, -9f, -8f, -11f))
        for (i in loud.indices) assertEquals(loud[i], quiet[i], 1e-5f)
    }

    @Test fun transientOnsetRequiresSharpAttackAndEnoughSignal() {
        // Umbral nuevo 0.30: 0.15 (umbral viejo) ya no dispara.
        assertTrue(AntiDolbyCrnnClassifier.isTransientOnset(0.35f, 0.1f))
        assertTrue(AntiDolbyCrnnClassifier.isTransientOnset(0.31f, 0.01f))
        assertFalse(AntiDolbyCrnnClassifier.isTransientOnset(0.15f, 0.1f))
        assertFalse(AntiDolbyCrnnClassifier.isTransientOnset(0.29f, 0.1f))
        // Sin señal suficiente no es transitorio aunque el onset sea alto.
        assertFalse(AntiDolbyCrnnClassifier.isTransientOnset(0.5f, 0.0005f))
        assertFalse(AntiDolbyCrnnClassifier.isTransientOnset(0.0f, 0.1f))
    }
}
