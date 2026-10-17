package com.ivanna.omega.fotoholo

import android.content.Context
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.visualizer.IvannaVisualizerBark64Bridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/** Lo que el panel muestra: valores MEDIDOS/APLICADOS, no deseados. */
data class FotoHoloTelemetry(
    val enabled: Boolean = false,
    val bypass: Boolean = false,
    val hasSignal: Boolean = false,
    val baseWidth: Float = 1f,
    val appliedWidth: Float = 1f,
    val bands: FloatArray = FloatArray(FotoHoloMath.BANDS),
    val features: FotoHoloFeatures = FotoHoloFeatures.NONE,
) {
    val offset: Float get() = appliedWidth - baseWidth
}

/**
 * Fotoholografía·Nanotomix — capa de orquestación sobre el motor espacial existente.
 *
 *   64 bandas Bark reales (captura Ruta A) → rasgos → ancho estéreo objetivo acotado → slew →
 *   IvannaNativeLib.nativeSetSpatialWidthDirect (el MISMO camino que los sliders de ancho: StereoWidener).
 *
 * Reglas:
 *  - Corre en un hilo de fondo (Dispatchers.Default, 20 Hz); nunca toca el hilo de audio ni el de UI.
 *  - Autoridad sobre el ancho solo mientras está activo y sin bypass; al desactivar (o bypass A/B)
 *    restituye EXACTAMENTE el ancho que fijó el usuario ([IvannaSpatialEngine.shared].widthFactor).
 *  - Sin señal (sin captura Ruta A) no modula: el ancho queda en el del usuario y se informa "sin señal".
 *  - No introduce una ruta de audio nueva ni suma procesamiento: modula un parámetro existente.
 */
object FotoHoloEngine {
    private const val PREFS = "ivanna_fotoholo"
    private const val TICK_MS = 50L
    private const val IDLE_MS = 250L

    @Volatile var enabled: Boolean = false; private set
    @Volatile var intensity: Float = 0.6f; private set
    /** A/B de sesión (no se persiste): true = se oye el ancho del usuario sin modulación. */
    @Volatile var bypass: Boolean = false; private set
    @Volatile private var viewers = 0
    @Volatile private var started = false

    private val _telemetry = MutableStateFlow(FotoHoloTelemetry())
    val telemetry: StateFlow<FotoHoloTelemetry> = _telemetry

    fun load(context: Context) {
        runCatching {
            val p = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            enabled = p.getBoolean("enabled", false)
            intensity = p.getFloat("intensity", 0.6f).coerceIn(0f, 1f)
        }
    }

    fun setEnabled(context: Context, on: Boolean) { enabled = on; save(context) }
    fun setIntensity(context: Context, v: Float) { intensity = if (v.isFinite()) v.coerceIn(0f, 1f) else 0.6f; save(context) }
    fun setBypass(on: Boolean) { bypass = on }

    /** El panel avisa que está visible para que se muestre el espectro aunque el efecto esté apagado. */
    fun viewerAttached() { viewers++ }
    fun viewerDetached() { viewers = (viewers - 1).coerceAtLeast(0) }

    private fun save(context: Context) {
        runCatching {
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit().putBoolean("enabled", enabled).putFloat("intensity", intensity).apply()
        }
    }

    private fun writeWidth(w: Float) {
        if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetSpatialWidthDirect(w) }
    }

    /** Arranca el lazo de control una sola vez (lo llama la Application). */
    fun start(context: Context, scope: CoroutineScope) {
        if (started) return
        started = true
        load(context)
        scope.launch(Dispatchers.Default) {
            val buf = FloatArray(FotoHoloMath.BANDS)
            var prev: FloatArray? = null
            var current = Float.NaN
            var lastWritten = Float.NaN
            var modulating = false
            while (true) {
                runCatching {
                    val modulate = enabled && !bypass
                    val sample = modulate || viewers > 0
                    val base = IvannaSpatialEngine.shared.widthFactor

                    if (!sample && !modulating) { delay(IDLE_MS); return@runCatching }

                    val ready = sample && IvannaVisualizerBark64Bridge.isReady
                    val feats: FotoHoloFeatures
                    if (ready) {
                        IvannaVisualizerBark64Bridge.sampleInto(buf)
                        feats = FotoHoloMath.features(buf, prev)
                        prev = (prev ?: FloatArray(FotoHoloMath.BANDS)).also { buf.copyInto(it) }
                    } else {
                        feats = FotoHoloFeatures.NONE
                        buf.fill(0f)
                    }

                    var applied = base
                    if (modulate) {
                        val target = FotoHoloMath.targetWidth(base, intensity, feats)
                        current = FotoHoloMath.slew(if (current.isNaN()) base else current, target)
                        if (lastWritten.isNaN() || FotoHoloMath.changed(current, lastWritten)) {
                            writeWidth(current); lastWritten = current
                        }
                        modulating = true
                        applied = current
                    } else if (modulating) {
                        // Apagado o bypass: se devuelve exactamente el ancho del usuario.
                        writeWidth(base); lastWritten = Float.NaN; current = Float.NaN
                        modulating = false
                    }

                    _telemetry.value = FotoHoloTelemetry(
                        enabled = enabled, bypass = bypass, hasSignal = feats.valid,
                        baseWidth = base, appliedWidth = applied,
                        bands = buf.copyOf(), features = feats,
                    )
                    delay(TICK_MS)
                }.onFailure { runCatching { kotlinx.coroutines.delay(IDLE_MS) } }
            }
        }
    }
}
