package com.ivanna.omega.fotoholo

import android.content.Context
import android.os.SystemClock
import android.webkit.JavascriptInterface
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.saf.SaFEngine
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicLong

/**
 * Read-only JS bridge for the FotoHolo WebGL dashboard when it is hosted by a trusted WebView.
 *
 * Values sourced from FotoHoloEngine are live Bark telemetry. DSP width is the native read-back
 * exposed by FotoHoloEngine and is not a microphone measurement. Dataset presence is reported
 * separately from runtime activation; the current native APIs do not expose definitive RIR/SAF
 * activation read-back, so those fields remain explicitly unconfirmed.
 *
 * The host must only install this interface for trusted bundled content. Do not expose it to
 * arbitrary remote pages.
 */
class FotoHoloTelemetryBridge(context: Context) {
    companion object {
        /** Call before loading the trusted, bundled dashboard page into this WebView. */
        fun installBeforeLoadingTrustedBundle(webView: android.webkit.WebView, context: Context): FotoHoloTelemetryBridge {
            val bridge = FotoHoloTelemetryBridge(context)
            webView.addJavascriptInterface(bridge, "IvannaFotoHoloBridge")
            return bridge
        }
    }

    private val appContext = context.applicationContext
    private val sequence = AtomicLong(0L)
    @Volatile private var viewerCounted = false

    @JavascriptInterface
    fun attach() {
        synchronized(this) {
            if (!viewerCounted) {
                FotoHoloEngine.viewerAttached()
                viewerCounted = true
            }
        }
    }

    @JavascriptInterface
    fun release() {
        synchronized(this) {
            if (viewerCounted) {
                FotoHoloEngine.viewerDetached()
                viewerCounted = false
            }
        }
    }

    /** Apply the UI's base spatial width to the native engine; modulation may subsequently adjust it. */
    @JavascriptInterface
    fun setSpatialWidth(width: Float): Boolean {
        if (!width.isFinite()) return false
        val safe = width.coerceIn(0f, 1.5f)
        return runCatching {
            IvannaSpatialEngine.setWidth(safe)
            if (!IvannaNativeLib.isLoaded) return false
            IvannaNativeLib.nativeSetSpatialWidthDirect(safe)
            true
        }.getOrDefault(false)
    }

    /** Apply a UI azimuth in degrees to the shared spatial engine and native renderer. */
    @JavascriptInterface
    fun setSpatialAngleDeg(angleDeg: Float): Boolean {
        if (!angleDeg.isFinite()) return false
        val safeDeg = angleDeg.coerceIn(-180f, 180f)
        val radians = Math.toRadians(safeDeg.toDouble()).toFloat()
        return runCatching {
            IvannaSpatialEngine.setAzimuth(radians)
            if (!IvannaNativeLib.isLoaded) return false
            IvannaNativeLib.nativeSetSpatialAngleRad(radians)
            true
        }.getOrDefault(false)
    }

    @JavascriptInterface
    fun getSnapshotJson(): String = runCatching {
        val tel = FotoHoloEngine.telemetry.value
        val metrics = OmegaMetrics.shared.value
        val saf = SaFEngine.latestTelemetry()
        val hrtfLoaded = runCatching { IvannaSpatialManager.isHrtfDatasetLoaded() }.getOrDefault(false)
        val hrtfSubject = runCatching { IvannaSpatialManager.currentHrtfSubject() }.getOrDefault("unknown")
        val rirLoaded = runCatching { OmegaEngineBridge.isRirDatasetLoaded() }.getOrDefault(false)
        val rirRoomCount = runCatching { OmegaEngineBridge.rirRoomCount() }.getOrDefault(0)
        val nativeState = if (IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.nativeGetSpatialState() }.getOrNull().orEmpty()
        } else ""

        JSONObject().apply {
            put("schemaVersion", 1)
            put("sequence", sequence.incrementAndGet())
            put("timestampMonotonicNs", SystemClock.elapsedRealtimeNanos())
            put("source", "android-native-bridge")
            put("signalValid", tel.hasSignal && tel.features.valid)
            put("sampleTimestampMonotonicNs", tel.sampleTimestampMonotonicNs)
            put("sampleAgeMs", if (tel.sampleTimestampMonotonicNs > 0L) ((SystemClock.elapsedRealtimeNanos() - tel.sampleTimestampMonotonicNs).coerceAtLeast(0L) / 1_000_000.0) else JSONObject.NULL)
            put("bands", JSONArray().apply { tel.bands.forEach { put(if (it.isFinite()) it.toDouble() else 0.0) } })
            put("features", JSONObject().apply {
                put("energy", finiteOrNull(tel.features.energy))
                put("lowShare", finiteOrNull(tel.features.lowShare))
                put("highShare", finiteOrNull(tel.features.highShare))
                put("centroid", finiteOrNull(tel.features.centroid))
                put("flux", finiteOrNull(tel.features.flux))
                put("valid", tel.features.valid)
            })
            put("commandedWidth", finiteOrNull(tel.appliedWidth))
            put("dspWidth", finiteOrNull(tel.dspWidth))
            put("nativeWriteError", tel.nativeWriteError ?: JSONObject.NULL)
            put("nativeCallReturned", tel.nativeCallReturned)
            put("outputRoute", metrics.audioRoute.takeIf { it.isNotBlank() && it != "—" } ?: "unreported")
            put("sampleRateHz", metrics.sampleRate.takeIf { it in 8000..384000 } ?: 0)
            put("latencyMs", finiteOrNull(metrics.latencyMs) ?: JSONObject.NULL)
            put("underruns", metrics.underrunCount.coerceAtLeast(0))
            put("hrtfAssetAvailable", assetExists("ivanna_omega/hrtf/hrtf_index.json"))
            put("rirAssetAvailable", assetExists("ivanna_omega/rir/metadata.csv") && assetExists("ivanna_omega/rir/rir_0000.wav"))
            put("rirDatasetLoaded", rirLoaded)
            put("rirRoomCount", rirRoomCount.coerceAtLeast(0))
            put("safAssetAvailable", assetExists("ivanna_omega/SAF_model.json"))
            put("hrtfDatasetLoaded", hrtfLoaded)
            put("hrtfSubject", hrtfSubject)
            put("hrtfRuntime", when {
                !IvannaNativeLib.isLoaded -> "native-library-unavailable"
                nativeState.contains("HRTF_ON", ignoreCase = true) -> "reported-active"
                hrtfLoaded -> "dataset-loaded-active-path-unconfirmed"
                nativeState.isBlank() -> "unavailable"
                else -> "not-loaded"
            })
            put("rirRuntime", when {
                !IvannaNativeLib.isLoaded -> "native-library-unavailable"
                rirLoaded && rirRoomCount > 0 -> "dataset-loaded-${rirRoomCount}-rooms-application-unconfirmed"
                else -> "dataset-not-loaded"
            })
            put("safRuntime", when {
                saf == null -> "not-initialized"
                !saf.jniLoaded -> "jni-unavailable"
                saf.modelLoaded && saf.converged -> "loaded-converged"
                saf.modelLoaded -> "loaded-not-converged"
                else -> "model-not-confirmed"
            })
            put("safModelLoaded", saf?.modelLoaded ?: JSONObject.NULL)
            put("safConverged", saf?.converged ?: JSONObject.NULL)
            put("safPhase", saf?.phase?.name ?: "not-initialized")
            put("safIteration", saf?.iteration ?: JSONObject.NULL)
            put("safErrorEnergy", saf?.errorEnergy?.let { finiteOrNull(it) } ?: JSONObject.NULL)
            put("nativeSpatialState", nativeState.take(512))
            put("freshnessMs", 0)
        }.toString()
    }.getOrElse {
        JSONObject().apply {
            put("schemaVersion", 1)
            put("sequence", sequence.incrementAndGet())
            put("timestampMonotonicNs", SystemClock.elapsedRealtimeNanos())
            put("source", "android-native-bridge")
            put("signalValid", false)
            put("error", it.javaClass.simpleName)
            put("reasonUnavailable", "snapshot-read-failed")
        }.toString()
    }

    private fun assetExists(path: String): Boolean = runCatching {
        appContext.assets.open(path).use { true }
    }.getOrDefault(false)

    private fun finiteOrNull(value: Float): Any =
        if (value.isFinite()) value.toDouble() else JSONObject.NULL
}
