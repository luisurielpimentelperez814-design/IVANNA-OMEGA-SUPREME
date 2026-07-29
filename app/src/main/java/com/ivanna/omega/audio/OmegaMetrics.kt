package com.ivanna.omega.audio

data class OmegaMetrics(
    var rmsLevel: Float = 0f,
    var peakLevel: Float = 0f,
    var clipCount: Int = 0,
    var cpuPercent: Float = 0f,
    var latencyMs: Float = 2.8f,
    // FIX (UI mostraba 96 kHz aún con hardware a 48 kHz): el default estaba
    // hardcodeado a 96000 y OmegaMetrics sólo se actualiza dentro de
    // IvannaBridgePlayer.pollOmegaMetrics(), que sólo corre reproduciendo por
    // el bridge. En captura del sistema o standby nadie sobreescribe el
    // default → EngineStatusCard imprime "${sampleRate / 1000}kHz" = 96kHz
    // falso. El SR real del dispositivo se obtiene en MainActivity.onCreate
    // (PROPERTY_OUTPUT_SAMPLE_RATE con fallback 48000). Bajar a 48000 alinea
    // el default con el fallback real; MainActivity actualiza el valor
    // efectivo si el hardware reporta otro.
    var sampleRate: Int = 48000,
    var yamnetCategory: String = "—",
    var yamnetConfidence: Float = 0f,
    var dspActive: Boolean = false,
    var hrtfActive: Boolean = false,
    var spatialWidth: Float = 0f,
    var audioRoute: String = "—"  // 4D: ruta de salida detectada por AudioRoutingManager
)
