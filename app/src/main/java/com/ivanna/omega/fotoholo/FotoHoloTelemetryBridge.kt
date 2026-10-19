package com.ivanna.omega.fotoholo

import android.content.Context
import android.os.SystemClock
import android.webkit.JavascriptInterface
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.core.IvannaNativeLib
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

    @JavascriptInterface
    fun getSnapshotJson(): String = runCatching {
        val tel = FotoHoloEngine.telemetry.value
        val metrics = OmegaMetrics.shared.value
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
            put("safAssetAvailable", assetExists("ivanna_omega/SAF_model.json"))
            put("hrtfRuntime", when {
                !IvannaNativeLib.isLoaded -> "native-library-unavailable"
                nativeState.contains("HRTF_ON", ignoreCase = true) -> "reported-active"
                nativeState.isBlank() -> "unavailable"
                else -> "not-confirmed-by-readback"
            })
            put("rirRuntime", "not-exposed-by-native-readback")
            put("safRuntime", "not-exposed-by-shared-state")
            put("safModelLoaded", JSONObject.NULL)
            put("safConverged", JSONObject.NULL)
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
