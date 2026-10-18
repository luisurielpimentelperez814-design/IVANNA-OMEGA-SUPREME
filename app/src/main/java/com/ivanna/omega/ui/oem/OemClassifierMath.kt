package com.ivanna.omega.ui.oem

/**
 * Lectura del vector de `IvannaNativeLib.nativeGetAudioCharacteristics()` para el panel
 * «CLASIFICADOR IA · CARACTERÍSTICAS». Lógica pura (sin Android) para poder probarla en JVM.
 *
 * CONTRATO NATIVO (ivanna_omega_jni.cpp, 8 floats, orden exacto):
 *   [0] rms  [1] peak  [2] percussiveness 0..1  [3] tonality 0..1
 *   [4] reverb proxy 0..1  [5] dynamic range proxy 0..1  [6] centroide Hz  [7] spread Hz
 *
 * Bug que esto corrige: `OemViewModel` leía [0] como percusividad, [1] como tonalidad y [2] como
 * centroide, es decir RMS, pico y percusividad. Resultado visible: «Centroide espect. 0 Hz» con RMS 0.22
 * y probabilidades de clase sin sentido.
 *
 * Las probabilidades NO son la red neuronal: son una derivación heurística de esos rasgos nativos
 * (voz ≈ ni tonal ni percusiva, música ≈ tonal, bajos ≈ centroide bajo y percusivo). El clasificador
 * CRNN real vive en la captura Ruta A (`AudioPipeline.sharedYamnetResult`).
 */
data class OemClassifierReading(
    val percussiveness: Float,
    val tonality: Float,
    val centroidHz: Float,
    val probVoice: Float,
    val probMusic: Float,
    val probBass: Float,
    val probSilence: Float,
    /** 0 voz, 1 música, 2 bajos, 3 silencio (mismo código que `OemState.dominantClass`). */
    val dominantClass: Int,
    /** false si el vector nativo no trae los 8 valores (motor sin cargar / aún sin datos). */
    val hasCharacteristics: Boolean,
)

object OemClassifierMath {
    const val IDX_PERCUSSIVENESS = 2
    const val IDX_TONALITY = 3
    const val IDX_CENTROID = 6
    const val EXPECTED_SIZE = 8
    private const val SIGNAL_RMS = 1e-4f

    private fun clean(v: Float, lo: Float, hi: Float): Float = if (v.isFinite()) v.coerceIn(lo, hi) else 0f

    /** [rms] viene del estado del pipeline unificado; [chars] es el vector nativo (puede estar vacío). */
    fun read(chars: FloatArray?, rms: Float): OemClassifierReading {
        val ok = chars != null && chars.size >= EXPECTED_SIZE
        val percussive = if (ok) clean(chars!![IDX_PERCUSSIVENESS], 0f, 1f) else 0f
        val tonal = if (ok) clean(chars!![IDX_TONALITY], 0f, 1f) else 0f
        val centroid = if (ok) clean(chars!![IDX_CENTROID], 0f, 22050f) else 0f
        val hasSignal = rms.isFinite() && rms > SIGNAL_RMS

        // Sin vector nativo no se inventa una clase: se informa silencio/sin datos.
        if (!hasSignal || !ok) {
            return OemClassifierReading(percussive, tonal, centroid, 0f, 0f, 0f, if (hasSignal) 0f else 1f,
                if (hasSignal) -1 else 3, ok)
        }
        val pBass = ((1f - (centroid / 4000f).coerceIn(0f, 1f)) * percussive).coerceIn(0f, 1f)
        val pMusic = (tonal * (1f - pBass * 0.5f)).coerceIn(0f, 1f)
        val pVoice = ((1f - tonal) * (1f - percussive) * (1f - pBass)).coerceIn(0f, 1f)
        val dom = when {
            pVoice >= pMusic && pVoice >= pBass -> 0
            pMusic >= pBass -> 1
            else -> 2
        }
        return OemClassifierReading(percussive, tonal, centroid, pVoice, pMusic, pBass, 0f, dom, true)
    }
}
