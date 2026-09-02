package com.ivanna.omega.spatial

import android.content.Context

/**
 * SpatialControlStore — persistencia de los controles HRTF/RIR/SAF.
 * SharedPreferences (mismo patrón que ParameterStore). Sobrevive cierres
 * y reboots; al abrir la app la UI restaura y reenvía la config al motor.
 */
object SpatialControlStore {
    private const val PREFS = "ivanna_spatial_control"

    data class SpatialConfig(
        val hrtfEnabled: Boolean = true,
        val hrtfSubject: String = "MIT KEMAR",
        val rirEnabled: Boolean = false,
        val rirRoom: Int = 0,
        val rirWet: Float = 0.25f,
        val safEnabled: Boolean = true,
        val safIntensity: Float = 0.5f
    )

    val SUBJECTS = listOf("MIT KEMAR", "CIPIC", "TU-Berlin", "Pulse")

    fun load(context: Context): SpatialConfig {
        val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return SpatialConfig(
            hrtfEnabled  = p.getBoolean("hrtfEnabled", true),
            hrtfSubject  = p.getString("hrtfSubject", "MIT KEMAR") ?: "MIT KEMAR",
            rirEnabled   = p.getBoolean("rirEnabled", false),
            rirRoom      = p.getInt("rirRoom", 0),
            rirWet       = p.getFloat("rirWet", 0.25f),
            safEnabled   = p.getBoolean("safEnabled", true),
            safIntensity = p.getFloat("safIntensity", 0.5f)
        )
    }

    fun save(context: Context, c: SpatialConfig) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean("hrtfEnabled", c.hrtfEnabled)
            .putString("hrtfSubject", c.hrtfSubject)
            .putBoolean("rirEnabled", c.rirEnabled)
            .putInt("rirRoom", c.rirRoom)
            .putFloat("rirWet", c.rirWet)
            .putBoolean("safEnabled", c.safEnabled)
            .putFloat("safIntensity", c.safIntensity)
            .apply()
    }
}
