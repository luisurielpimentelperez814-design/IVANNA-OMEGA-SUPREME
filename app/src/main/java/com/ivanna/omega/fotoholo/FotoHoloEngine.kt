package com.ivanna.omega.fotoholo

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.visualizer.IvannaVisualizerBark64Bridge
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Estado de control observable.
 * - [appliedWidth]: último ancho cuyo setter JNI retornó sin excepción (comando, no medición).
 * - [dspWidth]: read-back nativo (nativeGetSpatialWidthDirect) = ancho que el motor entregó al
 *   StereoWidener tras la escala térmica. NaN si aún no se pudo leer. Tampoco es audio medido.
 */
data class FotoHoloTelemetry(
    val enabled: Boolean = false,
    val bypass: Boolean = false,
    val hasSignal: Boolean = false,
    val baseWidth: Float = 1f,
    /** Último ancho cuya llamada JNI retornó sin excepción; NO es read-back del DSP. */
    val appliedWidth: Float = 1f,
    val bands: FloatArray = FloatArray(FotoHoloMath.BANDS),
    val features: FotoHoloFeatures = FotoHoloFeatures.NONE,
    val nativeWriteError: String? = null,
    val nativeWriteAttempted: Boolean = false,
    val nativeCallReturned: Boolean = false,
    val dspWidth: Float = Float.NaN,
) {
    /** true si el motor reporta un ancho finito y coherente con el último comando (puede ser menor por recorte térmico). */
    val readbackConfirmed: Boolean get() = dspWidth.isFinite() && nativeCallReturned && dspWidth <= appliedWidth + 0.01f
    val offset: Float get() = appliedWidth - baseWidth
}

/**
 * Orquesta el control de ancho existente. No crea una ruta de audio ni afirma holografía óptica.
 * El JNI actual expone nativeSetSpatialWidthDirect(Float): Unit, por lo que no hay confirmación
 * de aplicación real del DSP. La telemetría separa fallo de llamada de valor solicitado.
 */
object FotoHoloEngine {
    private const val TAG = "FotoHoloEngine"
    private const val PREFS = "ivanna_fotoholo"
    private const val TICK_MS = 50L
    private const val IDLE_MS = 250L

    @Volatile var enabled: Boolean = false; private set
    @Volatile var intensity: Float = 0.6f; private set
    @Volatile var bypass: Boolean = false; private set
    @Volatile private var viewers = 0
    @Volatile private var started = false

    private val _telemetry = MutableStateFlow(FotoHoloTelemetry())
    val telemetry: StateFlow<FotoHoloTelemetry> = _telemetry

    fun load(context: Context) {
        try {
            val p = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            enabled = p.getBoolean("enabled", false)
            intensity = p.getFloat("intensity", 0.6f).let { if (it.isFinite()) it.coerceIn(0f, 1f) else 0.6f }
        } catch (t: Throwable) {
            Log.w(TAG, "No se pudieron cargar preferencias; se usan valores seguros", t)
        }
    }

    fun setEnabled(context: Context, on: Boolean) { enabled = on; save(context) }
    fun setIntensity(context: Context, v: Float) { intensity = if (v.isFinite()) v.coerceIn(0f, 1f) else 0.6f; save(context) }
    fun setBypass(on: Boolean) { bypass = on }
    fun viewerAttached() { viewers++ }
    fun viewerDetached() { viewers = (viewers - 1).coerceAtLeast(0) }

    private fun save(context: Context) {
        try {
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit().putBoolean("enabled", enabled).putFloat("intensity", intensity).apply()
        } catch (t: Throwable) {
            Log.w(TAG, "No se pudieron guardar preferencias", t)
        }
    }

    /** Lee el ancho efectivo del motor; NaN si la biblioteca/símbolo no está disponible. */
    private fun readDspWidth(): Float {
        if (!IvannaNativeLib.isLoaded) return Float.NaN
        return try {
            IvannaNativeLib.nativeGetSpatialWidthDirect()
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Símbolo JNI de lectura de ancho no disponible", e)
            Float.NaN
        }
    }

    /** null = llamada JNI retornó; texto = fallo conocido. La confirmación del DSP viene de [readDspWidth]. */
    private fun writeWidth(width: Float): String? {
        if (!width.isFinite()) return "Ancho rechazado: valor no finito"
        if (!IvannaNativeLib.isLoaded) return "Biblioteca nativa no cargada; no se envió el ancho"
        return try {
            IvannaNativeLib.nativeSetSpatialWidthDirect(width.coerceIn(0f, 1.5f))
            null
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Símbolo JNI de ancho espacial no disponible", e)
            "Símbolo JNI de ancho espacial no disponible"
        } catch (e: Exception) {
            Log.e(TAG, "Falló la llamada JNI de ancho espacial", e)
            "Falló la llamada JNI: ${e.javaClass.simpleName}"
        } catch (e: LinkageError) {
            Log.e(TAG, "Error de enlace nativo al escribir ancho", e)
            "Error de enlace nativo: ${e.javaClass.simpleName}"
        }
    }

    /** Arranca una sola vez. La cancelación del scope se respeta y no se traga como error normal. */
    fun start(context: Context, scope: CoroutineScope) {
        if (started) return
        synchronized(this) {
            if (started) return
            started = true
        }
        load(context)
        scope.launch(Dispatchers.Default) {
            try {
            val buf = FloatArray(FotoHoloMath.BANDS)
            var prev: FloatArray? = null
            var current = Float.NaN
            var lastCommanded = Float.NaN
            var modulating = false
            var lastError: String? = null
            var lastAttempted = false
            var lastReturned = false
            var lastDspWidth = Float.NaN

            while (true) {
                try {
                    val wantsModulation = enabled && !bypass
                    val shouldSample = wantsModulation || viewers > 0
                    val rawBase = IvannaSpatialEngine.shared.widthFactor
                    val base = if (rawBase.isFinite()) rawBase.coerceIn(0f, 1.5f) else 1f

                    if (!shouldSample && !modulating) {
                        _telemetry.value = _telemetry.value.copy(
                            enabled = enabled, bypass = bypass, baseWidth = base,
                            nativeWriteError = lastError, nativeWriteAttempted = lastAttempted,
                            nativeCallReturned = lastReturned, dspWidth = lastDspWidth
                        )
                        delay(IDLE_MS)
                        continue
                    }

                    val ready = shouldSample && IvannaVisualizerBark64Bridge.isReady
                    val features = if (ready) {
                        IvannaVisualizerBark64Bridge.sampleInto(buf)
                        FotoHoloMath.features(buf, prev).also {
                            if (prev == null) prev = FloatArray(FotoHoloMath.BANDS)
                            buf.copyInto(prev!!)
                        }
                    } else {
                        buf.fill(0f)
                        FotoHoloFeatures.NONE
                    }

                    // No enviar cambios al DSP si no hay una trama Bark válida de la captura.
                    // Si la señal desaparece durante la modulación, el branch de restauración
                    // de abajo devuelve el ancho base y lo reintenta hasta que JNI responda.
                    val shouldModulate = wantsModulation && features.valid
                    var displayedCommand = if (lastCommanded.isFinite()) lastCommanded else base
                    if (shouldModulate) {
                        val target = FotoHoloMath.targetWidth(base, intensity, features)
                        current = FotoHoloMath.slew(if (current.isFinite()) current else base, target)
                        val needsWrite = !lastCommanded.isFinite() || FotoHoloMath.changed(current, lastCommanded) || lastError != null
                        if (needsWrite) {
                            lastAttempted = true
                            lastError = writeWidth(current)
                            lastReturned = lastError == null
                            if (lastReturned) { lastCommanded = current; lastDspWidth = readDspWidth() }
                        }
                        modulating = true
                        displayedCommand = if (lastCommanded.isFinite()) lastCommanded else base
                    } else if (modulating) {
                        // Restore is retried until the setter call returns without a known error.
                        lastAttempted = true
                        lastError = writeWidth(base)
                        lastReturned = lastError == null
                        if (lastReturned) {
                            lastCommanded = base
                            lastDspWidth = readDspWidth()
                            current = Float.NaN
                            modulating = false
                            displayedCommand = base
                        } else {
                            displayedCommand = if (lastCommanded.isFinite()) lastCommanded else base
                        }
                    } else {
                        displayedCommand = base
                    }

                    _telemetry.value = FotoHoloTelemetry(
                        enabled = enabled, bypass = bypass, hasSignal = features.valid,
                        baseWidth = base, appliedWidth = displayedCommand,
                        bands = buf.copyOf(), features = features,
                        nativeWriteError = lastError, nativeWriteAttempted = lastAttempted,
                        nativeCallReturned = lastReturned, dspWidth = lastDspWidth,
                    )
                    delay(TICK_MS)
                } catch (cancel: CancellationException) {
                    throw cancel
                } catch (t: Throwable) {
                    lastError = "Error del lazo: ${t.javaClass.simpleName}"
                    lastReturned = false
                    Log.e(TAG, "Fallo recuperable en lazo de Fotoholo", t)
                    _telemetry.value = _telemetry.value.copy(
                        enabled = enabled, bypass = bypass,
                        nativeWriteError = lastError, nativeCallReturned = false
                    )
                    delay(IDLE_MS)
                }
            }
            } finally {
                // El scope propietario puede cancelarse (p. ej. al recrear la pantalla/servicio).
                // Permitir que una futura llamada a start() vuelva a levantar el lazo.
                synchronized(this@FotoHoloEngine) { started = false }
            }
        }
    }
}
