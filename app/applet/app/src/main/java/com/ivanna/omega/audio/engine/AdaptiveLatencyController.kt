package com.ivanna.omega.audio.engine

import android.media.AudioDeviceInfo
import android.media.AudioTimestamp
import android.media.AudioTrack
import android.os.Build
import android.util.Log
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * AdaptiveLatencyController — Sistema de Latencia Adaptativa en Tiempo Real.
 *
 * Misión:
 * - Supervisar y calcular la latencia física real (en ms) entre captura y DAC.
 * - Medir el jitter de los timestamps del hardware de audio (EMA).
 * - Supervisar los underruns reales del HAL (AudioTrack.getUnderrunCount en API 24+).
 * - Identificar la ruta física activa (Altavoz, Auriculares con cable, DAC USB, Bluetooth A2DP/BLE).
 * - Establecer dinámicamente el target de headroom (frames y ms) adaptado al entorno:
 *     * USB-DAC / Cable: target agresivo de mínima latencia (15 - 20 ms).
 *     * Altavoz interno: target estándar de baja latencia (18 - 25 ms).
 *     * Bluetooth A2DP / BLE: target tolerante a jitter de red inalámbrica (45 - 65 ms).
 * - Ajustar preventivamente el margen de seguridad si la carga de DSP o temperatura aumentan.
 * - Proporcionar un índice de "Buffer Health" (0% a 100%) para telemetría.
 *
 * Cero asignaciones en el hilo de audio. Lock-free.
 */
class AdaptiveLatencyController(
    val sampleRate: Int = 48000
) {
    enum class OutputRouteType {
        SPEAKER,
        WIRED,
        USB_DAC,
        BLUETOOTH,
        UNKNOWN
    }

    companion object {
        private const val TAG = "AdaptiveLatencyCtrl"

        // Headroom targets en milisegundos
        const val HEADROOM_USB_DAC_MS = 15.0f
        const val HEADROOM_WIRED_MS   = 16.0f
        const val HEADROOM_SPEAKER_MS = 20.0f
        const val HEADROOM_BT_MS      = 52.0f

        const val MIN_HEADROOM_MS     = 10.0f
        const val MAX_HEADROOM_MS     = 95.0f
    }

    private val audioTs = AudioTimestamp()

    @Volatile var currentRoute: OutputRouteType = OutputRouteType.SPEAKER
        private set

    @Volatile var routeName: String = "Speaker"
        private set

    @Volatile var measuredLatencyMs: Float = 0f
        private set

    @Volatile var peakLatencyMs: Float = 0f
        private set

    @Volatile var measuredJitterMs: Float = 0f
        private set

    val jitterMs: Float
        get() = measuredJitterMs

    @Volatile var activeCodec: String = "PCM_FLOAT"
        private set

    @Volatile var targetHeadroomMs: Float = HEADROOM_SPEAKER_MS
        private set

    @Volatile var targetHeadroomFrames: Long = (HEADROOM_SPEAKER_MS * sampleRate / 1000f).toLong()
        private set

    @Volatile var underrunCount: Int = 0
        private set

    @Volatile var bufferHealthPercent: Float = 100f
        private set

    private var lastHwTimestampNs: Long = 0L
    private var lastHwFramePos: Long = 0L
    private var lastRouteCheckNs: Long = 0L
    private var initialUnderrunBaseline: Int = -1

    /**
     * Evalúa la ruta de salida actual del AudioTrack de forma no bloqueante cada 500ms.
     */
    fun checkRoute(track: AudioTrack?, force: Boolean = false) {
        if (track == null) return
        val nowNs = System.nanoTime()
        if (!force && (nowNs - lastRouteCheckNs < 500_000_000L)) return
        lastRouteCheckNs = nowNs

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val device = try { track.routedDevice } catch (_: Throwable) { null }
            if (device != null) {
                when (device.type) {
                    AudioDeviceInfo.TYPE_USB_DEVICE,
                    AudioDeviceInfo.TYPE_USB_HEADSET -> {
                        currentRoute = OutputRouteType.USB_DAC
                        routeName = "USB-DAC"
                        activeCodec = "USB_HiRes"
                    }
                    AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
                    AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
                    AudioDeviceInfo.TYPE_BLE_HEADSET,
                    AudioDeviceInfo.TYPE_BLE_SPEAKER -> {
                        currentRoute = OutputRouteType.BLUETOOTH
                        routeName = "Bluetooth"
                        activeCodec = "LDAC/AAC"
                    }
                    AudioDeviceInfo.TYPE_WIRED_HEADSET,
                    AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> {
                        currentRoute = OutputRouteType.WIRED
                        routeName = "Headphone"
                        activeCodec = "PCM_32BIT"
                    }
                    AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> {
                        currentRoute = OutputRouteType.SPEAKER
                        routeName = "Speaker"
                        activeCodec = "PCM_FLOAT"
                    }
                    else -> {
                        currentRoute = OutputRouteType.SPEAKER
                        routeName = "Speaker"
                        activeCodec = "PCM_FLOAT"
                    }
                }
            }
        }
    }

    /**
     * Actualiza las mediciones en cada bloque escrito.
     * Cero allocs.
     *
     * @param track Instancia de AudioTrack
     * @param framesWrittenToTrack Total acumulado de frames estéreo escritos
     * @param dspLoadRatio Ratio de consumo de tiempo del DSP (0.0 .. 1.0)
     * @param predictiveExtraMarginFrames Margen adicional de frames sugerido por el PredictiveLoadGovernor
     */
    fun tick(
        track: AudioTrack?,
        framesWrittenToTrack: Long,
        dspLoadRatio: Float = 0f,
        predictiveExtraMarginFrames: Long = 0L
    ) {
        if (track == null) return
        checkRoute(track)

        // 1. Underruns de hardware reales (API 24+)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            try {
                val totalUnderruns = track.underrunCount
                if (initialUnderrunBaseline < 0) {
                    initialUnderrunBaseline = totalUnderruns
                }
                underrunCount = max(0, totalUnderruns - initialUnderrunBaseline)
            } catch (_: Throwable) {}
        }

        // 2. Medición de latencia y jitter mediante AudioTimestamp
        var hwPositionObtained = false
        var hwFramePos = 0L
        var hwTimestampNs = 0L

        try {
            if (track.getTimestamp(audioTs)) {
                hwFramePos = audioTs.framePosition
                hwTimestampNs = audioTs.nanoTime
                hwPositionObtained = true
            }
        } catch (_: Throwable) {}

        if (!hwPositionObtained) {
            // Fallback a playbackHeadPosition
            try {
                hwFramePos = track.playbackHeadPosition.toLong() and 0xFFFFFFFFL
                hwTimestampNs = System.nanoTime()
                hwPositionObtained = true
            } catch (_: Throwable) {}
        }

        if (hwPositionObtained) {
            val queuedFrames = max(0L, framesWrittenToTrack - hwFramePos)
            val queueMs = (queuedFrames * 1000.0f / sampleRate)
            measuredLatencyMs = queueMs
            if (measuredLatencyMs > peakLatencyMs) {
                peakLatencyMs = measuredLatencyMs
            }

            // Cálculo de jitter: variación en el intervalo de hardware
            if (lastHwTimestampNs > 0L && hwTimestampNs > lastHwTimestampNs) {
                val deltaHwNs = hwTimestampNs - lastHwTimestampNs
                val deltaFrames = max(0L, hwFramePos - lastHwFramePos)
                val expectedNs = (deltaFrames * 1_000_000_000.0 / sampleRate).toLong()
                val instantJitterMs = abs(deltaHwNs - expectedNs) / 1_000_000.0f

                // Filtro exponencial de jitter (EMA)
                measuredJitterMs = (0.92f * measuredJitterMs) + (0.08f * instantJitterMs)
            }

            lastHwTimestampNs = hwTimestampNs
            lastHwFramePos = hwFramePos

            // 3. Adaptación del target de headroom
            val baseHeadroomMs = when (currentRoute) {
                OutputRouteType.USB_DAC   -> HEADROOM_USB_DAC_MS
                OutputRouteType.WIRED     -> HEADROOM_WIRED_MS
                OutputRouteType.SPEAKER   -> HEADROOM_SPEAKER_MS
                OutputRouteType.BLUETOOTH -> HEADROOM_BT_MS
                OutputRouteType.UNKNOWN   -> HEADROOM_SPEAKER_MS
            }

            // Compensación por jitter y carga de DSP
            val jitterPenaltyMs = if (measuredJitterMs > 3.0f) (measuredJitterMs * 1.2f) else 0f
            val loadPenaltyMs = if (dspLoadRatio > 0.70f) 8.0f else 0f

            val totalTargetMs = (baseHeadroomMs + jitterPenaltyMs + loadPenaltyMs).coerceIn(MIN_HEADROOM_MS, MAX_HEADROOM_MS)
            targetHeadroomMs = totalTargetMs

            val baseTargetFrames = (totalTargetMs * sampleRate / 1000f).toLong()
            targetHeadroomFrames = baseTargetFrames + predictiveExtraMarginFrames

            // 4. Cálculo de salud del buffer (Buffer Health %)
            val target = targetHeadroomFrames.toFloat().coerceAtLeast(1f)
            val diff = abs(queuedFrames - target)
            val health = (1.0f - (diff / (target * 2.0f))).coerceIn(0f, 1f) * 100f
            bufferHealthPercent = health
        }
    }

    fun configure(rate: Int = sampleRate, targetTrackBuf: Int = 0) {
        reset()
    }

    fun reset() {
        measuredLatencyMs = 0f
        peakLatencyMs = 0f
        measuredJitterMs = 0f
        lastHwTimestampNs = 0L
        lastHwFramePos = 0L
        initialUnderrunBaseline = -1
        underrunCount = 0
        bufferHealthPercent = 100f
    }
}
