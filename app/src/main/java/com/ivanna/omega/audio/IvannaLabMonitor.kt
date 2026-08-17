package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

// ─────────────────────────────────────────────────────────────────────────────
// IvannaLabMonitor — puente a nativeLabFeed/Measure/Report/Reset (FASE 3A).
// feed() se llama desde el loop real de captura de PlaybackCaptureService
// (audio ya intercalado L,R capturado por MediaProjection — mismo buffer que
// alimenta IvannaNpeEngine/SpatialAudioEngineV2, no un buffer sintético).
// startAutoMeasure() dispara measure()+report() cada 30s en un coroutine
// propio; el resultado se expone como StateFlow<LabSnapshot?> para
// IvannaLabScreen (o cualquier otra pantalla) sin acoplarse al ciclo de vida
// de la captura.
// ─────────────────────────────────────────────────────────────────────────────
object IvannaLabMonitor {

    private const val TAG = "IvannaLabMonitor"
    private const val AUTO_MEASURE_INTERVAL_MS = 30_000L

    data class LabSnapshot(
        val thd: Float,
        val imd: Float,
        val lufs: Float,
        val luRange: Float,
        val snr: Float,
        val peak: Float,
        val truepeak: Float,
        val report: String,
        val timestampMs: Long
    )

    private val _snapshot = MutableStateFlow<LabSnapshot?>(null)
    val snapshot: StateFlow<LabSnapshot?> = _snapshot

    private val _autoMeasureActive = MutableStateFlow(false)
    val autoMeasureActive: StateFlow<Boolean> = _autoMeasureActive

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var autoJob: Job? = null

    /** Alimenta el analizador con [frames] frames estéreo intercalados. */
    fun feed(interleavedStereo: FloatArray, frames: Int) {
        if (!IvannaNativeLib.isLoaded || frames <= 0) return
        try {
            IvannaNativeLib.nativeLabFeed(interleavedStereo, frames)
        } catch (e: Throwable) {
            Log.w(TAG, "feed: $e")
        }
    }

    /** Mide una vez ahora mismo y actualiza el StateFlow (no bloquea el hilo de audio). */
    fun measureNow() {
        if (!IvannaNativeLib.isLoaded) return
        scope.launch {
            try {
                val vals = IvannaNativeLib.nativeLabMeasure() ?: return@launch
                if (vals.size < 7) return@launch
                val report = runCatching { IvannaNativeLib.nativeLabReport() }.getOrDefault("")
                _snapshot.value = LabSnapshot(
                    thd = vals[0], imd = vals[1], lufs = vals[2], luRange = vals[3],
                    snr = vals[4], peak = vals[5], truepeak = vals[6],
                    report = report, timestampMs = System.currentTimeMillis()
                )
            } catch (e: Throwable) {
                Log.w(TAG, "measureNow: $e")
            }
        }
    }

    fun startAutoMeasure() {
        if (autoJob?.isActive == true) return
        _autoMeasureActive.value = true
        autoJob = scope.launch {
            while (_autoMeasureActive.value) {
                measureNow()
                delay(AUTO_MEASURE_INTERVAL_MS)
            }
        }
    }

    fun stopAutoMeasure() {
        _autoMeasureActive.value = false
        autoJob?.cancel()
        autoJob = null
    }

    fun resetAndStart() {
        if (IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.nativeLabReset() }
        }
        _snapshot.value = null
        startAutoMeasure()
    }
}
