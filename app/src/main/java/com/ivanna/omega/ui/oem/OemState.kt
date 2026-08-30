package com.ivanna.omega.ui.oem

data class OemState(
    val engineState      : EngineState = EngineState.UNKNOWN,
    val backend          : AudioBackend = AudioBackend.UNKNOWN,
    val nativeLoaded     : Boolean = false,
    val daemonAlive      : Boolean = false,
    val adaptiveRunning  : Boolean = false,
    val activeRoute      : Float = 0f,
    val rms              : Float = 0f,
    val peak             : Float = 0f,
    val voiceProtect     : Float = 0f,
    val compAmount       : Float = 0f,
    val exciterRed       : Float = 0f,
    val spatialWidth     : Float = 1f,
    val adaptiveActive   : Float = 0f,
    val grDb             : Float = 0f,
    val targetGain       : Float = 1f,
    val safetyMargin     : Float = 0f,
    val applied          : Float = 0f,
    val percussiveness   : Float = 0f,
    val tonality         : Float = 0f,
    val reverbLevel      : Float = 0f,
    val dynRange         : Float = 0f,
    val spectralCentroid : Float = 2500f,
    val latencyUs        : Long = 0L,
    val clipCount        : Int = 0,
    val thermalLoad      : Float = 0f,
    val thermalApiOk     : Boolean = false,
    val probVoice        : Float = 0f,
    val probMusic        : Float = 0f,
    val probBass         : Float = 0f,
    val probSilence      : Float = 0f,
    val dominantClass    : Int = -1,
    val hrtfReady        : Boolean = false,
    val hrtfSubject      : String = "—",
    val usbStreaming     : Boolean = false,
    val evoBestFitness   : Float = 0f,
    val evoGeneration    : Int = 0,
    // Campos añadidos en OemViewModel refactored (v2.3.1)
    val tempC          : Float = 0f,
    val latencyMs      : Float = 0f,
    val phaseState     : Float = 0f,
    val hrtfLoaded     : Boolean = false,
    val safConverged   : Boolean = false,
    val safError       : Float = 0f,
    val safIteration   : Int = 0,
    val safDiag        : FloatArray = FloatArray(0),
    val daemonStatus   : String = "",
) {
    enum class EngineState { UNKNOWN, ACTIVE, SUSPENDED, POWER_SAVE, RECOVERY }
    enum class AudioBackend { UNKNOWN, HEXAGON_DSP, NEON_ARM64, CPU_FALLBACK }
    enum class ThermalLevel { NORMAL, LIGHT, MODERATE, SEVERE }

    val thermalLevel: ThermalLevel get() = when {
        thermalLoad >= 0.8f -> ThermalLevel.SEVERE
        thermalLoad >= 0.6f -> ThermalLevel.MODERATE
        thermalLoad >= 0.3f -> ThermalLevel.LIGHT
        else                -> ThermalLevel.NORMAL
    }
    val dominantLabel: String get() = when (dominantClass) {
        0 -> "VOZ"; 1 -> "MÚSICA"; 2 -> "BAJOS"; 3 -> "SILENCIO"; else -> "—"
    }
    val signalActive: Boolean get() = rms > 1e-4f
}
