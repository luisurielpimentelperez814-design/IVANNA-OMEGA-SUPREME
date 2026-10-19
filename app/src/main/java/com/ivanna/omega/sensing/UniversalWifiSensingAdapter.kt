package com.ivanna.omega.sensing

import android.content.Context
import android.os.Build
import android.os.SystemClock
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * Cross-device sensing contract for FotoHolo.
 *
 * "Universal" means one stable application interface with interchangeable,
 * verified hardware adapters and an honest fallback. It does NOT mean that
 * Android exposes CSI on every chipset. The fallback derives only coarse
 * temporal signal-change indicators from ordinary RSSI observations; it must
 * never infer a person's anatomy, exact headcount, identity, or room geometry.
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

    /**
     * Implementations must be hardware-specific, permission-aware and tested
     * against a real capture. A declaration alone does not prove CSI works.
     */
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

    fun registerProvider(provider: CsiProvider) {
        // Registration is explicit; no arbitrary shell command or guessed plugin is loaded.
        if (provider.capabilities.verifiedOnDevice &&
            provider.capabilities.providerId.isNotBlank() &&
            providers.none { it.capabilities.providerId == provider.capabilities.providerId }) {
            providers += provider
        }
    }

    fun availableCsiProviders(context: Context): List<CsiCapabilities> {
        val device = DeviceDescriptor(
            manufacturer = Build.MANUFACTURER.orEmpty(),
            model = Build.MODEL.orEmpty(),
            androidApi = Build.VERSION.SDK_INT
        )
        return providers.filter { provider ->
            runCatching { provider.capabilities.verifiedOnDevice && provider.isSupported(context, device) }
                .getOrDefault(false)
        }.map { it.capabilities }
    }

    /**
     * Conservative universal fallback: use only the RSSI/time series delivered
     * by the platform or root scanner. This is a room-change cue, not a person
     * counter, anatomical reconstruction, or calibrated acoustic measurement.
     */
    @Synchronized
    fun analyzeMetadata(observations: List<WifiObservation>): SceneFeatures {
        val now = SystemClock.elapsedRealtime()
        if (observations.isEmpty()) {
            return SceneFeatures(
                level = DataLevel.NO_WIFI, observationCount = 0, medianRssiDbm = null,
                signalVariabilityDb = null, movementIndicator = null,
                occupancy = OccupancyHint.UNKNOWN, confidence = 0.0,
                source = "none", timestampMs = now,
                limitations = listOf("No hay observaciones Wi-Fi nuevas.", "No se estima ocupación ni anatomía.")
            )
        }

        val rssi = observations.map { it.rssiDbm }.sorted()
        val median = rssi[rssi.size / 2]
        history.addLast(observations.toList())
        while (history.size > HISTORY_LIMIT) history.removeFirst()

        val prior = history.dropLast(1).lastOrNull()
        val priorByKey = prior.orEmpty().associateBy { it.ssid to it.frequencyMhz }
        val deltas = observations.mapNotNull { current ->
            priorByKey[current.ssid to current.frequencyMhz]?.let { abs(current.rssiDbm - it.rssiDbm).toDouble() }
        }
        val variability = if (deltas.isNotEmpty()) deltas.average() else null
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
            level = DataLevel.WIFI_METADATA_ONLY,
            observationCount = observations.size,
            medianRssiDbm = median,
            signalVariabilityDb = variability,
            movementIndicator = movement,
            occupancy = hint,
            confidence = confidence,
            source = "android_or_iw_rssi",
            timestampMs = now,
            limitations = listOf(
                "RSSI es metadato de señal, no CSI.",
                "Cambios pueden deberse a movimiento, interferencia, roaming o cambios del entorno.",
                "No se calcula número de personas, anatomía, distancia exacta ni plano de la habitación.",
                "La acústica debe calibrarse con medición acústica real y consentimiento separado."
            )
        )
    }

    fun resetSession() {
        synchronized(this) { history.clear() }
    }

    fun normalizeConfidence(value: Double): Double = max(0.0, min(1.0, value))
}
