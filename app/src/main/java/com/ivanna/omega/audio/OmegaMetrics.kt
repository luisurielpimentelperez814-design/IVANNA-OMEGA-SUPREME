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
) {
    companion object {
        // FIX (audit): puente global observable.
        // Antes OmegaMetrics vivía en dos sitios:
        //   - IvannaBridgePlayer._omegaMetrics (solo se rellena reproduciendo)
        //   - default hardcoded (96 kHz falso, telemetría vacía)
        // Ahora hay un StateFlow compartido que MainActivity / AudioPipeline /
        // NpeEngine actualizan aunque no haya bridge activo. IvannaBridgePlayer
        // sigue publicando su propio flow local para no romper el resto.
        private val _shared = kotlinx.coroutines.flow.MutableStateFlow(OmegaMetrics())
        val shared: kotlinx.coroutines.flow.StateFlow<OmegaMetrics> = _shared

        fun updateSampleRate(sr: Int) {
            if (sr <= 0) return
            _shared.value = _shared.value.copy(sampleRate = sr)
        }

        fun updateSharedYamnet(category: String, confidence: Float) {
            _shared.value = _shared.value.copy(
                yamnetCategory = category,
                yamnetConfidence = confidence.coerceIn(0f, 1f)
            )
        }

        // FIX (Ruta A telemetria muerta): antes solo IvannaBridgePlayer
        // (reproduccion local) publicaba niveles vivos. Con Spotify/YouTube
        // el usuario usa MediaProjection/PlaybackCapture (Ruta A) y nadie
        // rellenaba estos campos -> EngineStatusCard mostraba STANDBY /
        // RMS -60 dB / peak 0 / clips 0 aunque el DSP procesaba audio.
        //
        // updateSharedLevels() da un punto de entrada unico para publicar
        // niveles desde la ruta de captura y desde cualquier otra ruta A
        // que se cablee en el futuro, sin duplicar copy().
        // Todos los parametros son opcionales para permitir updates parciales
        // (p.ej. solo RMS/peak cada N bloques y clips acumulados).
        fun updateSharedLevels(
            rms: Float? = null,
            peak: Float? = null,
            clips: Int? = null,
            hrtfActive: Boolean? = null,
            spatialWidth: Float? = null,
            dspActive: Boolean? = null,
        ) {
            val cur = _shared.value
            _shared.value = cur.copy(
                rmsLevel     = rms          ?: cur.rmsLevel,
                peakLevel    = peak         ?: cur.peakLevel,
                clipCount    = clips        ?: cur.clipCount,
                hrtfActive   = hrtfActive   ?: cur.hrtfActive,
                spatialWidth = spatialWidth ?: cur.spatialWidth,
                dspActive    = dspActive    ?: cur.dspActive,
            )
        }
    }
}
