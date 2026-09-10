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
 *   → suavizado 1/12 octava → compensación = clip(target − medido, ±8 dB)
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

    // Perfiles medidos — HpIR SOFA de cada modelo. RECONCILIADO 2026-09-10
    // (flanco HRTF-tools) contra tools/hpir/hpir_profiles_measured.json
    // (regenerado en 7c43a509, 2026-09-09): el Kotlin se había quedado en la
    // versión 2026-08-29 — 4 bandas y clamp ±6 dB — mientras el extractor
    // actual produce 5 bandas y clamp ±8 dB. La primera banda es el shelf de
    // graves del target (~105 Hz). Única edición manual permitida: NINGUNA —
    // regenerar con el extractor, no editar a mano (drift datos↔consumidor).
    private val PROFILES: Map<String, List<Band>> = mapOf(
        "Sennheiser HD650" to listOf(
            Band(105.0f, 6.08f, 0.71f),
            Band(4728.5f, -3.26f, 1.15f),
            Band(6498.0f, 2.73f, 2.98f),
            Band(7810.5f, 8.0f, 1.83f),
            Band(17232.4f, 6.01f, 4.0f),
        ),
        "Beyerdynamic DT770 Pro" to listOf(
            Band(105.0f, 8.0f, 0.71f),
            Band(123.0f, 8.0f, 0.71f),
            Band(8121.1f, 8.0f, 3.97f),
            Band(13148.4f, -7.79f, 3.17f),
            Band(16669.9f, 8.0f, 4.0f),
        ),
        "Beyerdynamic DT990 Pro" to listOf(
            Band(105.0f, 6.52f, 0.71f),
            Band(673.8f, 3.92f, 0.4f),
            Band(5074.2f, -6.57f, 2.76f),
            Band(7154.3f, 5.41f, 2.93f),
            Band(10201.2f, 3.64f, 4.0f),
        ),
        "AKG K271 MKII" to listOf(
            Band(105.0f, 7.58f, 0.71f),
            Band(199.2f, 6.48f, 0.7f),
            Band(4212.9f, 3.95f, 1.64f),
            Band(7617.2f, -5.53f, 1.65f),
            Band(11759.8f, 8.0f, 1.41f),
        ),
        "AKG K272 HD" to listOf(
            Band(105.0f, 7.58f, 0.71f),
            Band(199.2f, 6.48f, 0.7f),
            Band(4212.9f, 3.95f, 1.64f),
            Band(7617.2f, -5.53f, 1.65f),
            Band(11759.8f, 8.0f, 1.41f),
        )
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
