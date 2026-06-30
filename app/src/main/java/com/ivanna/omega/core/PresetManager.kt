package com.ivanna.omega.core

import android.content.Context

class PresetManager(context: Context) {
    private val prefs = context.getSharedPreferences("ivanna_omega_presets", Context.MODE_PRIVATE)

    fun getPresets(): List<String> = listOf(
        "Studio Reference", "Bass Boost", "Vocal Clarity", "Live Room",
        "Cinematic", "Electronic", "Acoustic", "Rock 70s", "Podcast", "Flat"
    )

    fun loadPreset(name: String) { prefs.edit().putString("current", name).apply() }
    fun savePreset(name: String, data: String) { prefs.edit().putString(name, data).apply() }
    fun getCurrentPreset(): String = prefs.getString("current", "Studio Reference") ?: "Studio Reference"
}
