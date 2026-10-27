package com.ivanna.omega.spatial

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

/**
 * Puente UI -> JNI para la geometría WFS.
 *
 * Coordenadas de sala: X=ancho (3.5 m), Y=altura (3.5 m), Z=fondo (7 m).
 * El layout enviado al renderer se expresa relativo a la posición del oyente.
 * El setter JNI es void: éxito aquí significa que la llamada retornó, no que
 * se haya medido una respuesta acústica ni confirmado el consumo por el daemon.
 */
object WfsCalibrationManager {
    private const val TAG = "WfsCalibration"
    private const val PREFS = "ivanna_wfs_calib"
    private const val KEY_LX = "lx"
    private const val KEY_LY = "ly"
    private const val KEY_LZ = "lz"
    private const val KEY_LYAW = "lyaw"
    private const val KEY_PRESET = "preset"

    val DEFAULT_X = floatArrayOf(0.35f, 3.15f, 0.25f, 3.25f, 0.25f, 3.25f, 3.00f)
    val DEFAULT_Y = floatArrayOf(1.50f, 1.50f, 1.50f, 1.50f, 3.00f, 3.00f, 0.15f)
    val DEFAULT_Z = floatArrayOf(0.00f, 0.00f, 3.50f, 3.50f, 3.50f, 3.50f, 3.00f)

    @Volatile var listenerX = 1.75f
        private set
    @Volatile var listenerY = 1.20f
        private set
    @Volatile var listenerZ = 3.50f
        private set
    @Volatile var listenerYawDegrees = 0f
        private set
    @Volatile var presetName = "7ch-default"
        private set

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    private fun safeStoredFloat(value: Float, fallback: Float, min: Float, max: Float): Float =
        if (value.isFinite()) value.coerceIn(min, max) else fallback

    private fun publishRelativeLayout(context: Context): Boolean {
        if (!IvannaNativeLib.isLoaded) {
            Log.w(TAG, "No se envía WFS: biblioteca nativa no cargada")
            return false
        }
        return runCatching {
            // JNI/C++ expects room-frame speaker coordinates and subtracts its fixed default listener
            // (1.75, 1.20, 3.50 m). Convert the user-relative geometry back into that frame here;
            // otherwise passing relative coordinates would subtract the default listener twice.
            val radians = Math.toRadians(listenerYawDegrees.toDouble())
            val cosYaw = kotlin.math.cos(radians).toFloat()
            val sinYaw = kotlin.math.sin(radians).toFloat()
            val nativeX = FloatArray(DEFAULT_X.size)
            val nativeY = FloatArray(DEFAULT_Y.size)
            val nativeZ = FloatArray(DEFAULT_Z.size)
            for (i in DEFAULT_X.indices) {
                val dx = DEFAULT_X[i] - listenerX
                val dz = DEFAULT_Z[i] - listenerZ
                val rotatedX = dx * cosYaw + dz * sinYaw
                val rotatedZ = -dx * sinYaw + dz * cosYaw
                nativeX[i] = 1.75f + rotatedX
                nativeY[i] = 1.20f + DEFAULT_Y[i] - listenerY
                nativeZ[i] = 3.50f + rotatedZ
            }
            IvannaNativeLib.nativeSetWfsSpeakerLayout(nativeX, nativeY, nativeZ)
            true
        }.getOrElse {
            Log.w(TAG, "No se pudo enviar layout WFS", it)
            false
        }
    }

    /** Devuelve true si el setter JNI retornó sin excepción; no es medición acústica. */
    fun applyLayout(context: Context, name: String = presetName): Boolean {
        presetName = name
        prefs(context).edit().putString(KEY_PRESET, name).apply()
        return publishRelativeLayout(context)
    }

    /** Persiste y aplica la posición del oyente con límites físicos de la sala. */
    fun setListener(context: Context, lx: Float, ly: Float, lz: Float, yawDegrees: Float = listenerYawDegrees): Boolean {
        listenerX = lx.takeIf { it.isFinite() }?.coerceIn(0f, 3.5f) ?: 1.75f
        listenerY = ly.takeIf { it.isFinite() }?.coerceIn(0f, 3.5f) ?: 1.20f
        listenerZ = lz.takeIf { it.isFinite() }?.coerceIn(0f, 7f) ?: 3.50f
        listenerYawDegrees = yawDegrees.takeIf { it.isFinite() }?.let { ((it % 360f) + 360f) % 360f } ?: 0f
        prefs(context).edit()
            .putFloat(KEY_LX, listenerX)
            .putFloat(KEY_LY, listenerY)
            .putFloat(KEY_LZ, listenerZ)
            .putFloat(KEY_LYAW, listenerYawDegrees)
            .apply()
        return publishRelativeLayout(context)
    }

    /** Restaura la geometría relativa al oyente, no el layout absoluto por defecto. */
    fun restore(context: Context): Boolean {
        val p = prefs(context)
        // Preferencias antiguas/corruptas pueden contener NaN o Inf. No las publiques
        // al daemon: la geometría relativa debe permanecer finita y dentro de la sala.
        listenerX = safeStoredFloat(p.getFloat(KEY_LX, 1.75f), 1.75f, 0f, 3.5f)
        listenerY = safeStoredFloat(p.getFloat(KEY_LY, 1.20f), 1.20f, 0f, 3.5f)
        listenerZ = safeStoredFloat(p.getFloat(KEY_LZ, 3.50f), 3.50f, 0f, 7f)
        listenerYawDegrees = safeStoredFloat(p.getFloat(KEY_LYAW, 0f), 0f, 0f, 360f)
        presetName = p.getString(KEY_PRESET, "7ch-default") ?: "7ch-default"
        return publishRelativeLayout(context)
    }
}
