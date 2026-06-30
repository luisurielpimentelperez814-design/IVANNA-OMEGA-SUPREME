package com.ivanna.omega.core

import android.media.AudioFormat
import android.util.Log

/**
 * AntiDolbyPreset — Perfil optimizado para superar a Dolby Atmos.
 *
 * Características v1.5:
 * - Compresor 2:1 lento (attack 30ms, release 300ms)
 * - Exciter armónicos pares (solo si no es voz)
 * - Widener reducido al 40%
 * - Reverb solo en cola (post-transitorio)
 * - Activa automáticamente con sampleRate=48000 y decoder EAC3
 */
class AntiDolbyPreset {
    companion object {
        private const val TAG = "AntiDolbyPreset"

        // Parámetros del compresor
        const val COMP_ATTACK_MS = 30f
        const val COMP_RELEASE_MS = 300f
        const val COMP_RATIO = 2.0f
        const val COMP_THRESHOLD_DB = -18.0f

        // Parámetros del exciter
        const val EXCITER_AMOUNT = 0.4f
        const val EXCITER_HARMONICS = 2  // armónicos pares

        // Parámetros del widener
        const val WIDENER_AMOUNT = 0.4f  // 40% (vs 100% normal)

        // Curva inversa Dolby para recuperar respuesta plana
        // Frecuencias: 32, 64, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz
        val EQ_INVERSE_DOLBY = floatArrayOf(
            0.0f, 0.0f, -1.0f, -2.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f, 1.5f
        )

        /**
         * Detecta si el formato de audio proviene de decoder EAC3/JOC.
         * EAC3 típicamente usa sampleRate=48000 con encoding específico.
         */
        fun isEac3Format(sampleRate: Int, encoding: Int): Boolean {
            val is48k = sampleRate == 48000
            val isEac3 = encoding == AudioFormat.ENCODING_E_AC3 || 
                        encoding == AudioFormat.ENCODING_E_AC3_JOC ||
                        encoding == 18  // ENCODING_E_AC3 en algunos OEMs

            Log.i(TAG, "Detección EAC3: sampleRate=$sampleRate, encoding=$encoding, isEac3=$isEac3")
            return is48k && isEac3
        }

        /**
         * Verifica si el dispositivo tiene Dolby Atmos activo.
         */
        fun isDolbyPresent(): Boolean {
            // Verificar propiedades del sistema
            val dolbyProps = listOf(
                "ro.vendor.dolby.dax.version",
                "persist.vendor.audio.dolby.ds2.enabled",
                "ro.audio.monitor.rotation"
            )
            for (prop in dolbyProps) {
                val value = try {
                    Class.forName("android.os.SystemProperties")
                        .getMethod("get", String::class.java)
                        .invoke(null, prop) as String
                } catch (e: Exception) { "" }
                if (value.isNotEmpty() && value != "false" && value != "0") {
                    Log.i(TAG, "Dolby detectado via prop: $prop = $value")
                    return true
                }
            }
            return false
        }
    }

    data class DspParams(
        val compressorAttackMs: Float = COMP_ATTACK_MS,
        val compressorReleaseMs: Float = COMP_RELEASE_MS,
        val compressorRatio: Float = COMP_RATIO,
        val compressorThresholdDb: Float = COMP_THRESHOLD_DB,
        val exciterAmount: Float = EXCITER_AMOUNT,
        val exciterHarmonics: Int = EXCITER_HARMONICS,
        val widenerAmount: Float = WIDENER_AMOUNT,
        val reverbTailOnly: Boolean = true,
        val eqCurve: FloatArray = EQ_INVERSE_DOLBY.clone()
    )

    fun getDefaultParams(): DspParams = DspParams()

    /**
     * Activa el preset Anti-Dolby con parámetros óptimos para EAC3.
     */
    fun getEac3Preset(): DspParams = DspParams(
        compressorAttackMs = 30f,
        compressorReleaseMs = 300f,
        compressorRatio = 2.0f,
        compressorThresholdDb = -18f,
        exciterAmount = 0.35f,      // Ligeramente menor para EAC3
        exciterHarmonics = 2,
        widenerAmount = 0.35f,        // Más conservador para contenido multicanal
        reverbTailOnly = true,
        eqCurve = floatArrayOf(0.0f, 0.5f, -0.5f, -1.5f, 0.0f, 0.5f, 1.5f, 2.5f, 3.5f, 2.0f)
    )
}
