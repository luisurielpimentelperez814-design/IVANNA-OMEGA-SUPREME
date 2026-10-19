package com.ivanna.omega.sensing

/**
 * Safe bridge between radio-environment cues and acoustic calibration.
 *
 * Wi-Fi sensing may request a new direct acoustic measurement; it cannot directly
 * authorize EQ, gain, RT60 or spatial-renderer changes. The measured audio path
 * remains the authority for DSP decisions.
 */
object WifiAcousticAdaptationPolicy {
    enum class Action {
        NO_ACTION,
        WAIT_FOR_MORE_RADIO_DATA,
        REQUEST_ACOUSTIC_REMEASUREMENT
    }

    data class Input(
        val scene: UniversalWifiSensingAdapter.SceneFeatures,
        val acousticCalibrationAvailable: Boolean,
        val acousticCalibrationAgeMs: Long?,
        val acousticCalibrationMaxAgeMs: Long = 30 * 60 * 1000L,
        val minimumRadioConfidence: Double = 0.25,
        val minimumMovementIndicator: Double = 0.25
    )

    data class Decision(
        val action: Action,
        val reason: String,
        val dspMayChange: Boolean = false
    )

    fun evaluate(input: Input): Decision {
        val scene = input.scene
        if (scene.level == UniversalWifiSensingAdapter.DataLevel.NO_WIFI) {
            return Decision(Action.NO_ACTION, "No hay observaciones Wi-Fi; conservar el estado de audio.")
        }
        val movement = scene.movementIndicator
        if (movement == null || scene.occupancy == UniversalWifiSensingAdapter.OccupancyHint.UNKNOWN) {
            return Decision(Action.WAIT_FOR_MORE_RADIO_DATA, "Falta una referencia temporal Wi-Fi comparable.")
        }
        if (scene.confidence < input.minimumRadioConfidence || movement < input.minimumMovementIndicator) {
            return Decision(Action.NO_ACTION, "No se ha superado el umbral de cambio ambiental.")
        }
        val stale = input.acousticCalibrationAgeMs == null ||
            input.acousticCalibrationAgeMs < 0L ||
            input.acousticCalibrationAgeMs > input.acousticCalibrationMaxAgeMs
        val reason = when {
            !input.acousticCalibrationAvailable -> "Cambio de señal: solicitar medición acústica inicial."
            stale -> "Cambio de señal: la calibración acústica está ausente o vencida."
            else -> "Cambio de señal: confirmar con RoomProbe antes de considerar cambios DSP."
        }
        return Decision(Action.REQUEST_ACOUSTIC_REMEASUREMENT, reason, dspMayChange = false)
    }
}
