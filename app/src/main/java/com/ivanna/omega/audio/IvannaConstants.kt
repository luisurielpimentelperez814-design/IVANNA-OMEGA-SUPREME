package com.ivanna.omega.audio

/**
 * Constantes centralizadas de rangos DSP — IVANNA ULTRA
 * Toda la lógica de clamp en Kotlin y C++ debe referenciar estos valores
 * para mantener coherencia con la UI (±24 dB / ±2400 mB).
 */
object IvannaConstants {
    // EQ — decibeles (capa JS/Kotlin)
    const val EQ_MIN_DB  = -24f
    const val EQ_MAX_DB  =  24f

    // EQ — milibeles (capa Android Equalizer API)
    const val EQ_MIN_MB  = -2400
    const val EQ_MAX_MB  =  2400

    // Bass Boost / Virtualizer strength (0–1000)
    const val BASS_MIN   = 0
    const val BASS_MAX   = 1000
    const val VIRT_MIN   = 0
    const val VIRT_MAX   = 1000

    // Loudness gain mB
    const val LOUD_MIN   = 0
    const val LOUD_MAX   = 1000

    // Compressor
    const val COMP_RATIO_MIN = 1.5f
    const val COMP_RATIO_MAX = 6f
    // Output gain range (-24..+18 dB hardware limit)
    const val OUTPUT_GAIN_MIN = -24f
    const val OUTPUT_GAIN_MAX =  18f

    // Input trim range
    const val INPUT_TRIM_MIN  = -24f
    const val INPUT_TRIM_MAX  =  24f

}
