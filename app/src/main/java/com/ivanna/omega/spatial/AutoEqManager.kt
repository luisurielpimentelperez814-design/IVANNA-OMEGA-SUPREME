package com.ivanna.omega.spatial

import android.util.Log

/**
 * AutoEqManager — perfiles de compensación de auriculares derivados de
 * MEDICIONES REALES (HpIR SOFA de magisk_module/.../sofa/hpir_*.sofa).
 *
 * REFINAMIENTO (2026-08-29): antes eran 5 perfiles mock con valores
 * inventados ("+4.5dB sub-bass" en el HD600, sin ninguna medición detrás)
 * mientras 23 HpIR medidos de auriculares reales dormían sin uso en el
 * módulo. Ahora cada perfil se extrajo del HpIR correspondiente:
 *
 *   FFT del HpIR → respuesta |H(f)| promediada (M mediciones × 2 canales)
 *   → suavizado 1/12 octava → compensación = clip(target − medido, ±6 dB)
 *   con target perceptual tipo Harman over-ear (shelf de graves + presencia
 *   3 kHz + caída de agudos) — NO inversión cruda a plano (eso producía
 *   boosts de +8 dB en 23 Hz que solo excursionan el driver → distorsión).
 *
 * Método: ver tools/hpir/extract_hpir_profiles.py. Biquads peaking de
 * AutoEqFilter (nativo, hasta 10 bandas).
 */
object AutoEqManager {
    private const val TAG = "IVANNA.AutoEq"

    /** Una banda peaking: frecuencia central Hz, ganancia dB, Q. */
    data class Band(val freqHz: Float, val gainDb: Float, val q: Float)

    // Perfiles medidos — HpIR SOFA (headphone impulse response) de cada
    // modelo. La primera banda (~100 Hz) es el shelf de graves del target.
    private val PROFILES: Map<String, List<Band>> = mapOf(
        "Sennheiser HD650" to listOf(
            Band(100.0f, 6.0f, 0.71f),
            Band(6433.6f, 2.95f, 3.02f),
            Band(7646.5f, 6.0f, 1.77f),
            Band(12316.4f, -4.87f, 2.82f),
        ),
        "Beyerdynamic DT770 Pro" to listOf(
            Band(100.0f, 6.0f, 0.71f),
            Band(269.5f, 6.0f, 0.66f),
            Band(3222.7f, 6.0f, 1.79f),
            Band(8015.6f, 6.0f, 3.5f),
        ),
        "Beyerdynamic DT990 Pro" to listOf(
            Band(100.0f, 6.0f, 0.71f),
            Band(5103.5f, -5.78f, 2.99f),
            Band(7148.4f, 5.52f, 3.01f),
            Band(13523.4f, -6.0f, 1.97f),
        ),
        "AKG K271 MKII" to listOf(
            Band(100.0f, 6.0f, 0.71f),
            Band(269.5f, 6.0f, 1.81f),
            Band(4130.9f, 5.55f, 0.81f),
            Band(11818.4f, 6.0f, 1.45f),
        ),
        "AKG K272 HD" to listOf(
            Band(100.0f, 6.0f, 0.71f),
            Band(269.5f, 6.0f, 1.81f),
            Band(4130.9f, 5.55f, 0.81f),
            Band(11818.4f, 6.0f, 1.45f),
        ),
    )

    // Lista pública para la UI (Phase7Screen la itera). Derivada del mapa —
    // una sola fuente de verdad, sin lista paralela que se desincronice.
    val availableProfiles: List<String> = PROFILES.keys.toList()

    /**
     * Aplica el perfil medido del auricular al AutoEqFilter nativo.
     * Devuelve true si el perfil existía y se empujó; false si el handle es
     * nulo o el nombre no tiene medición — la UI debe mostrar la verdad.
     */
    fun applyProfile(handle: Long, profileName: String): Boolean {
        if (handle == 0L) return false
        val bands = PROFILES[profileName]
        if (bands == null) {
            Log.w(TAG, "Sin perfil medido para '$profileName' — AutoEQ no aplicado")
            return false
        }

        Log.i(TAG, "AutoEQ medido: $profileName (${bands.size} bandas)")
        return runCatching {
            IvannaSpatialNative.nativeObjectRendererSetAutoEqEnabled(handle, true)
            bands.forEachIndexed { i, b ->
                IvannaSpatialNative.nativeObjectRendererSetAutoEqBand(
                    handle, i, b.freqHz, b.gainDb, b.q
                )
            }
            true
        }.getOrDefault(false)
    }

    fun disable(handle: Long) {
        if (handle == 0L) return
        runCatching { IvannaSpatialNative.nativeObjectRendererSetAutoEqEnabled(handle, false) }
        Log.i(TAG, "AutoEQ disabled")
    }
}
