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
        val rirEnabled: Boolean = true,
        val rirRoom: Int = 51,
        val rirWet: Float = 0.22f,
        val reflectionDistance: Float = 1.0f,
        val safEnabled: Boolean = true,
        val safIntensity: Float = 0.78f,
        val upmixerEnabled: Boolean = true,
        val selectedStem: Int = 0,
        val stem0X: Float = 0.0f, val stem0Y: Float = 0.0f, val stem0Z: Float = 1.2f, val stem0Width: Float = 0.15f,
        val stem1X: Float = 0.0f, val stem1Y: Float = 0.3f, val stem1Z: Float = -0.8f, val stem1Width: Float = 0.45f,
        val stem2X: Float = 0.0f, val stem2Y: Float = -0.8f, val stem2Z: Float = 0.2f, val stem2Width: Float = 0.25f,
        val stem3X: Float = 0.0f, val stem3Y: Float = 0.1f, val stem3Z: Float = 0.0f, val stem3Width: Float = 0.85f
    )

    val SUBJECTS = listOf("MIT KEMAR", "CIPIC", "TU-Berlin", "Pulse")
    val STEMS = listOf("VOCALS", "DRUMS", "BASS", "OTHER")

    fun load(context: Context): SpatialConfig {
        val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return SpatialConfig(
            hrtfEnabled    = p.getBoolean("hrtfEnabled", true),
            hrtfSubject    = p.getString("hrtfSubject", "MIT KEMAR") ?: "MIT KEMAR",
            rirEnabled     = p.getBoolean("rirEnabled", true),
            rirRoom        = p.getInt("rirRoom", 51),
            rirWet         = p.getFloat("rirWet", 0.22f),
            reflectionDistance = p.getFloat("reflectionDistance", 1.0f).coerceIn(0.5f, 2.0f),
            safEnabled     = p.getBoolean("safEnabled", true),
            safIntensity   = p.getFloat("safIntensity", 0.78f),
            upmixerEnabled = p.getBoolean("upmixerEnabled", true),
            selectedStem   = p.getInt("selectedStem", 0).coerceIn(0, 3),
            stem0X         = p.getFloat("stem0X", 0.0f),
            stem0Y         = p.getFloat("stem0Y", 0.0f),
            stem0Z         = p.getFloat("stem0Z", 1.2f),
            stem0Width     = p.getFloat("stem0Width", 0.15f),
            stem1X         = p.getFloat("stem1X", 0.0f),
            stem1Y         = p.getFloat("stem1Y", 0.3f),
            stem1Z         = p.getFloat("stem1Z", -0.8f),
            stem1Width     = p.getFloat("stem1Width", 0.45f),
            stem2X         = p.getFloat("stem2X", 0.0f),
            stem2Y         = p.getFloat("stem2Y", -0.8f),
            stem2Z         = p.getFloat("stem2Z", 0.2f),
            stem2Width     = p.getFloat("stem2Width", 0.25f),
            stem3X         = p.getFloat("stem3X", 0.0f),
            stem3Y         = p.getFloat("stem3Y", 0.1f),
            stem3Z         = p.getFloat("stem3Z", 0.0f),
            stem3Width     = p.getFloat("stem3Width", 0.85f)
        )
    }

    fun save(context: Context, c: SpatialConfig) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean("hrtfEnabled", c.hrtfEnabled)
            .putString("hrtfSubject", c.hrtfSubject)
            .putBoolean("rirEnabled", c.rirEnabled)
            .putInt("rirRoom", c.rirRoom)
            .putFloat("rirWet", c.rirWet)
            .putFloat("reflectionDistance", c.reflectionDistance)
            .putBoolean("safEnabled", c.safEnabled)
            .putFloat("safIntensity", c.safIntensity)
            .putBoolean("upmixerEnabled", c.upmixerEnabled)
            .putInt("selectedStem", c.selectedStem)
            .putFloat("stem0X", c.stem0X).putFloat("stem0Y", c.stem0Y).putFloat("stem0Z", c.stem0Z).putFloat("stem0Width", c.stem0Width)
            .putFloat("stem1X", c.stem1X).putFloat("stem1Y", c.stem1Y).putFloat("stem1Z", c.stem1Z).putFloat("stem1Width", c.stem1Width)
            .putFloat("stem2X", c.stem2X).putFloat("stem2Y", c.stem2Y).putFloat("stem2Z", c.stem2Z).putFloat("stem2Width", c.stem2Width)
            .putFloat("stem3X", c.stem3X).putFloat("stem3Y", c.stem3Y).putFloat("stem3Z", c.stem3Z).putFloat("stem3Width", c.stem3Width)
            .apply()
    }
}
