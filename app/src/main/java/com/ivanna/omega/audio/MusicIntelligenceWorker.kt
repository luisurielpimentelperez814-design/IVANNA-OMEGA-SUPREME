package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONObject

/**
 * MusicIntelligenceWorker — cierra el bucle del IME (PASO 2/3/4):
 * audio real → extractor nativo (RT-safe) → decide() → parámetros DSP reales.
 * Dispatchers.Default cada 2 s: NUNCA en el hilo de audio, sin locks en RT
 * (el bridge nativo usa seqlock), sin allocations en el callback.
 * Suavizado exponencial (SMOOTH) — los cambios convergen en ~6 s sin clicks.
 * Opt-in explícito: solo actúa con el toggle del panel activo.
 */
object MusicIntelligenceWorker {
    private const val TAG = "MusicIntelWorker"
    private const val PREFS = "ivanna_ime"
    private const val SMOOTH = 0.35f
    private const val MIN_BLOCKS = 24L
    private const val MIN_CONFIDENCE = 0.25f

    data class ImeState(
        val active: Boolean = false,
        val style: String = "—",
        val confidence: Float = 0f,
        val wfsSpread: Float = 0.5f,
        val hrtfDepth: Float = 0.5f,
        val eqTiltDb: Float = 0f,
        val dynamicsAmount: Float = 1f,
        val envDepth: Float = 0.3f,
        val blocks: Long = 0,
        val adaptMs: Long = 0,
        val lastAppliedAtMs: Long = 0
    )

    private val _state = MutableStateFlow(ImeState())
    val state: StateFlow<ImeState> = _state

    private var scope: CoroutineScope? = null
    @Volatile private var enabled = false
    private var curWfs = 0.5f; private var curHrtf = 0.5f; private var curTilt = 0f
    private var curDyn = 1f;  private var curEnv = 0.3f

    fun isEnabled(ctx: Context): Boolean =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("enabled", false)

    fun setEnabled(ctx: Context, on: Boolean) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean("enabled", on).apply()
        enabled = on
        try {
            if (IvannaNativeLib.isLoaded) {
                IvannaNativeLib.nativeImeSetEnabled(on)
            }
        } catch (e: Throwable) {
            Log.w(TAG, "nativeImeSetEnabled failed: ${e.message}")
        }
        _state.value = _state.value.copy(active = on)
    }

    fun start(appContext: Context) {
        if (scope != null) return
        enabled = isEnabled(appContext)
        try {
            if (IvannaNativeLib.isLoaded) {
                IvannaNativeLib.nativeImeSetEnabled(enabled)
            }
        } catch (e: Throwable) {
            Log.w(TAG, "nativeImeSetEnabled failed: ${e.message}")
        }
        val s = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        scope = s
        s.launch {
            while (isActive) {
                delay(2000)
                if (enabled) {
                    try {
                        tick()
                    } catch (e: Throwable) {
                        Log.w(TAG, "tick: ${e.message}")
                    }
                }
            }
        }
    }

    private fun tick() {
        if (!IvannaNativeLib.isLoaded) return
        val rawJson: String = IvannaNativeLib.nativeImeDecideNow()
        val o = JSONObject(rawJson)
        val blocks = o.optLong("blocks", 0L)
        val conf = o.optDouble("confidence", 0.0).toFloat()
        val tWfs  = o.optDouble("wfsSpread", 0.5).toFloat()
        val tHrtf = o.optDouble("hrtfDepth", 0.5).toFloat()
        val tTilt = o.optDouble("eqTiltDb", 0.0).toFloat()
        val tDyn  = o.optDouble("dynamicsAmount", 1.0).toFloat()
        val tEnv  = o.optDouble("envDepth", 0.3).toFloat()
        _state.value = _state.value.copy(
            active = true, style = o.optString("style", "—"), confidence = conf,
            wfsSpread = tWfs, hrtfDepth = tHrtf, eqTiltDb = tTilt,
            dynamicsAmount = tDyn, envDepth = tEnv,
            blocks = blocks, adaptMs = o.optLong("adaptMs", 0L))
        if (blocks < MIN_BLOCKS || conf < MIN_CONFIDENCE) return
        applySmoothed(tWfs, tHrtf, tTilt, tDyn, tEnv)
    }

    private fun applySmoothed(tWfs: Float, tHrtf: Float, tTilt: Float, tDyn: Float, tEnv: Float) {
        curWfs  += SMOOTH * (tWfs  - curWfs)
        curHrtf += SMOOTH * (tHrtf - curHrtf)
        curTilt += SMOOTH * (tTilt - curTilt)
        curDyn  += SMOOTH * (tDyn  - curDyn)
        curEnv  += SMOOTH * (tEnv  - curEnv)
        try {
            if (IvannaNativeLib.isLoaded) {
                IvannaNativeLib.nativeSetWfsEnabled(true)
                IvannaNativeLib.nativeSetWfsSpread((0.4f + 0.8f * curWfs).coerceIn(0f, 2f))
                IvannaNativeLib.nativeSetEQParams(-curTilt / 2f, 0f, curTilt / 2f, 1f)
                IvannaNativeLib.nativeSetCompressorAmount(curDyn.coerceIn(0f, 1f))
                IvannaNativeLib.nativeSetSpatialWet(curHrtf.coerceIn(0f, 1f))
                IvannaNativeLib.nativeSetSpatialWidthDirect((0.3f + curEnv).coerceIn(0f, 1.5f))
            }
        } catch (e: Throwable) {
            Log.w(TAG, "applySmoothed failed: ${e.message}")
        }
        _state.value = _state.value.copy(lastAppliedAtMs = System.currentTimeMillis())
    }
}
