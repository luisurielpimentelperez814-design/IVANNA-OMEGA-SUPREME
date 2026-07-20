package com.ivanna.omega.adaptive

/**
 * Snapshot de telemetría del motor adaptativo.
 * Contiene los parámetros DSP analizados en tiempo real.
 */
data class AdaptiveTelemetrySnapshot(
    val drive: Float = 0f,
    val wet: Float = 0f,
    val mix: Float = 0f,
    val alpha: Float = 0f,
    val beta: Float = 0f,
    val gamma: Float = 0f,
    val freq: Float = 1000f,
    val resonance: Float = 0.7f,
    val low: Float = 0f,
    val mid: Float = 0f,
    val high: Float = 0f,
    val presence: Float = 0f,
    val master: Float = 0f,
    val mode: Int = 0,
    val bpm: Float = 120f,
    val spectralClass: Int = 0,
    val genomeFitness: Float = 0f
) {
    companion object {
        /**
         * Convierte un FloatArray crudo en un AdaptiveTelemetrySnapshot.
         * Usado por el backend JNI para pasar telemetría a la UI.
         */
        fun fromArray(arr: FloatArray?): AdaptiveTelemetrySnapshot {
            if (arr == null || arr.size < 16) return AdaptiveTelemetrySnapshot()
            return AdaptiveTelemetrySnapshot(
                drive = arr.getOrElse(0) { 0f },
                wet = arr.getOrElse(1) { 0f },
                mix = arr.getOrElse(2) { 0f },
                alpha = arr.getOrElse(3) { 0f },
                beta = arr.getOrElse(4) { 0f },
                gamma = arr.getOrElse(5) { 0f },
                freq = arr.getOrElse(6) { 1000f },
                resonance = arr.getOrElse(7) { 0.7f },
                low = arr.getOrElse(8) { 0f },
                mid = arr.getOrElse(9) { 0f },
                high = arr.getOrElse(10) { 0f },
                presence = arr.getOrElse(11) { 0f },
                master = arr.getOrElse(12) { 0f },
                mode = arr.getOrElse(13) { 0f }.toInt(),
                bpm = arr.getOrElse(14) { 120f },
                spectralClass = arr.getOrElse(15) { 0f }.toInt(),
                genomeFitness = arr.getOrElse(16) { 0f }
            )
        }

        fun toArray(snapshot: AdaptiveTelemetrySnapshot): FloatArray {
            return floatArrayOf(
                snapshot.drive,
                snapshot.wet,
                snapshot.mix,
                snapshot.alpha,
                snapshot.beta,
                snapshot.gamma,
                snapshot.freq,
                snapshot.resonance,
                snapshot.low,
                snapshot.mid,
                snapshot.high,
                snapshot.presence,
                snapshot.master,
                snapshot.mode.toFloat(),
                snapshot.bpm,
                snapshot.spectralClass.toFloat(),
                snapshot.genomeFitness
            )
        }
    }
}
