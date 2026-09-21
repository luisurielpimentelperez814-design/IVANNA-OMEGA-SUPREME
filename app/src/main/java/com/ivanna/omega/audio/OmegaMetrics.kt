package com.ivanna.omega.audio

data class OmegaMetrics(
    var rmsLevel: Float = 0f,
    var peakLevel: Float = 0f,
    var clipCount: Int = 0,
    var cpuPercent: Float = 0f,
    var latencyMs: Float = 0f,  // 0 = sin medición; el bus mmap / updateSharedLevels publica el valor real por bloque
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
    var audioRoute: String = "—",  // 4D: ruta de salida detectada por AudioRoutingManager
    var jitterMs: Float = 0f,
    var underrunCount: Int = 0,
    var dspLoadPercent: Float = 0f,
    var bufferHealthPercent: Float = 100f,
    var activeCodec: String = "—",
    var budgetBypasses: Int = 0,
    var antiPopEvents: Int = 0,
    var resyncCount: Int = 0
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

        var peakQueueMsShared: Float = 0f
        var resyncCountShared: Int = 0
        fun updateSharedLatency(queueMs: Float, peakMs: Float, resyncs: Int) {
            peakQueueMsShared = peakMs
            resyncCountShared = resyncs
            _shared.value = _shared.value.copy(
                latencyMs = queueMs.coerceAtLeast(0f),
                resyncCount = resyncs
            )
        }

        fun updateRefinedTelemetry(
            latencyMs: Float? = null,
            peakLatencyMs: Float? = null,
            jitterMs: Float? = null,
            underrunCount: Int? = null,
            dspLoadPercent: Float? = null,
            bufferHealthPercent: Float? = null,
            activeCodec: String? = null,
            audioRoute: String? = null,
            budgetBypasses: Int? = null,
            antiPopEvents: Int? = null,
            resyncCount: Int? = null
        ) {
            if (peakLatencyMs != null) peakQueueMsShared = peakLatencyMs
            if (resyncCount != null) resyncCountShared = resyncCount
            val cur = _shared.value
            _shared.value = cur.copy(
                latencyMs           = latencyMs           ?: cur.latencyMs,
                jitterMs            = jitterMs            ?: cur.jitterMs,
                underrunCount       = underrunCount       ?: cur.underrunCount,
                dspLoadPercent      = dspLoadPercent      ?: cur.dspLoadPercent,
                bufferHealthPercent = bufferHealthPercent ?: cur.bufferHealthPercent,
                activeCodec         = activeCodec         ?: cur.activeCodec,
                audioRoute          = audioRoute          ?: cur.audioRoute,
                budgetBypasses      = budgetBypasses      ?: cur.budgetBypasses,
                antiPopEvents       = antiPopEvents       ?: cur.antiPopEvents,
                resyncCount         = resyncCount         ?: cur.resyncCount
            )
        }

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
