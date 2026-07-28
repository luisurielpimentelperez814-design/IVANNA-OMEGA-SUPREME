package com.ivanna.omega.core

import android.content.Context

class PresetManager(context: Context) {
    private val prefs = context.getSharedPreferences("ivanna_omega_presets", Context.MODE_PRIVATE)

    private val builtInPresets = listOf(
        "Studio Reference", "Bass Boost", "Vocal Clarity", "Live Room",
        "Cinematic", "Electronic", "Acoustic", "Rock 70s", "Podcast", "Flat"
    )

    // FIX (bug real): antes esta lista era fija y savePreset() guardaba en
    // SharedPreferences un preset que getPresets() jamás iba a mostrar —
    // quedaba huérfano para siempre. Ahora se fusionan builtIn + guardados.
    fun getPresets(): List<String> =
        (builtInPresets + prefs.all.keys.filter { it != "current" && it !in builtInPresets }).distinct()

    fun loadPreset(name: String) { prefs.edit().putString("current", name).apply() }
    fun savePreset(name: String, data: String) { prefs.edit().putString(name, data).apply() }
    fun getCurrentPreset(): String = prefs.getString("current", "Studio Reference") ?: "Studio Reference"
}
