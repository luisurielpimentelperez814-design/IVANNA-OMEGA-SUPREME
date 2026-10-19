package com.ivanna.omega.sensing

import android.content.Context
import android.os.Build
import android.os.SystemClock
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * Cross-device sensing contract with verified hardware providers and an honest fallback.
 * CSI is never synthesized from RSSI; a 3D scene requires calibrated multi-link measurements.
 */
object UniversalWifiSensingAdapter {
    enum class DataLevel { NO_WIFI, WIFI_METADATA_ONLY, CSI_VERIFIED }

    data class DeviceDescriptor(
        val manufacturer: String,
        val model: String,
        val androidApi: Int,
        val chipsetHint: String? = null
    )

    data class CsiCapabilities(
        val providerId: String,
        val chipsetFamilies: Set<String>,
        val kernelFamilies: Set<String>,
        val sampleFormat: String,
        val supportsTimestamps: Boolean,
        val verifiedOnDevice: Boolean
    )

    data class CsiSample(
        val timestampNanos: Long,
        val subcarrierAmplitudes: FloatArray,
        val subcarrierPhases: FloatArray? = null,
        val antennaCount: Int,
        val sampleRateHz: Int
    )

    interface CsiProvider {
        val capabilities: CsiCapabilities
        fun isSupported(context: Context, device: DeviceDescriptor): Boolean
        fun start(onSample: (CsiSample) -> Unit, onError: (String) -> Unit): Boolean
        fun stop()
    }

    data class SceneFeatures(
        val level: DataLevel,
        val observationCount: Int,
        val medianRssiDbm: Int?,
        val signalVariabilityDb: Double?,
        val movementIndicator: Double?,
        val occupancy: OccupancyHint,
        val confidence: Double,
        val source: String,
        val timestampMs: Long,
        val limitations: List<String>
    )

    enum class OccupancyHint { UNKNOWN, SIGNAL_CHANGED, NO_SIGNIFICANT_CHANGE }

    private val providers = CopyOnWriteArrayList<CsiProvider>()
    private val history = ArrayDeque<List<WifiObservation>>()
    private const val HISTORY_LIMIT = 8
    @Volatile private var activeProvider: CsiProvider? = null

    fun registerProvider(provider: CsiProvider) {
        // Providers are opt-in code integrations, not arbitrary shell commands or guessed plugins.
        if (provider.capabilities.verifiedOnDevice &&
            provider.capabilities.providerId.isNotBlank() &&
            providers.none { it.capabilities.providerId == provider.capabilities.providerId }) {
            providers += provider
        }
    }

    fun availableCsiProviders(context: Context): List<CsiCapabilities> {
        val device = currentDevice()
        return providers.filter { provider ->
            runCatching { provider.capabilities.verifiedOnDevice && provider.isSupported(context, device) }
                .getOrDefault(false)
        }.map { it.capabilities }
    }

    /**
     * Starts the first verified provider for this actual device and streams normalized CSI
     * features. Returns false when no registered provider is compatible or start fails.
     * No built-in provider is claimed until a chipset/driver combination has been tested.
     */
    @Synchronized
    fun startCsiSession(
        context: Context,
        onFeatures: (CsiFeatureExtractor.Features, CsiCapabilities) -> Unit,
        onError: (String) -> Unit
    ): Boolean {
        stopCsiSession()
        val device = currentDevice()
        val provider = providers.firstOrNull {
            runCatching { it.capabilities.verifiedOnDevice && it.isSupported(context, device) }
                .getOrDefault(false)
        } ?: run {
            onError("No hay un proveedor CSI verificado para ${device.manufacturer} ${device.model}.")
            return false
        }
        CsiFeatureExtractor.reset()
        val started = runCatching {
            provider.start(
                onSample = { sample ->
                    val features = CsiFeatureExtractor.analyze(sample)
                    onFeatures(features, provider.capabilities)
                },
                onError = { message ->
                    if (activeProvider === provider) onError(message)
                }
            )
        }.getOrElse {
            onError("El proveedor CSI falló al iniciar: ${it.javaClass.simpleName}")
            false
        }
        if (!started) {
            runCatching { provider.stop() }
            onError("El proveedor CSI no pudo iniciar la captura.")
            return false
        }
        activeProvider = provider
        return true
    }

    @Synchronized
    fun stopCsiSession() {
        val provider = activeProvider
        activeProvider = null
        runCatching { provider?.stop() }
        CsiFeatureExtractor.reset()
    }

    private fun currentDevice() = DeviceDescriptor(
        manufacturer = Build.MANUFACTURER.orEmpty(),
        model = Build.MODEL.orEmpty(),
        androidApi = Build.VERSION.SDK_INT
    )

    /** RSSI fallback: a qualitative signal-change cue, never person count or geometry. */
    @Synchronized
    fun analyzeMetadata(observations: List<WifiObservation>): SceneFeatures {
        val now = SystemClock.elapsedRealtime()
        if (observations.isEmpty()) {
            return SceneFeatures(
                DataLevel.NO_WIFI, 0, null, null, null, OccupancyHint.UNKNOWN, 0.0,
                "none", now,
                listOf("No hay observaciones Wi-Fi nuevas.", "No se estima ocupación ni anatomía.")
            )
        }

        val rssi = observations.map { it.rssiDbm }.sorted()
        val median = if (rssi.size % 2 == 0) (rssi[rssi.size / 2 - 1] + rssi[rssi.size / 2]) / 2 else rssi[rssi.size / 2]
        history.addLast(observations.toList())
        while (history.size > HISTORY_LIMIT) history.removeFirst()

        val prior = history.dropLast(1).lastOrNull()
        val priorByKey = prior.orEmpty().associateBy { it.ssid to it.frequencyMhz }
        val deltas = observations.mapNotNull { current ->
            priorByKey[current.ssid to current.frequencyMhz]?.let { abs(current.rssiDbm - it.rssiDbm).toDouble() }
        }
        val variability = deltas.takeIf { it.isNotEmpty() }?.average()
        val movement = variability?.let { (it / 12.0).coerceIn(0.0, 1.0) }
        val hint = when {
            movement == null -> OccupancyHint.UNKNOWN
            movement >= 0.25 -> OccupancyHint.SIGNAL_CHANGED
            else -> OccupancyHint.NO_SIGNIFICANT_CHANGE
        }
        val confidence = when {
            deltas.size >= 4 -> min(0.55, 0.15 + deltas.size * 0.05)
            deltas.isNotEmpty() -> 0.15
            else -> 0.0
        }
        return SceneFeatures(
            DataLevel.WIFI_METADATA_ONLY, observations.size, median, variability, movement, hint,
            confidence, "android_or_iw_rssi", now,
            listOf(
                "RSSI es metadato de señal, no CSI.",
                "Cambios pueden deberse a movimiento, interferencia, roaming o cambios del entorno.",
                "No se calcula número de personas, anatomía, distancia exacta ni plano de habitación.",
                "La acústica debe calibrarse con medición acústica real."
            )
        )
    }

    fun resetSession() {
        synchronized(this) { history.clear() }
        stopCsiSession()
    }

    fun normalizeConfidence(value: Double): Double = max(0.0, min(1.0, value))
}
