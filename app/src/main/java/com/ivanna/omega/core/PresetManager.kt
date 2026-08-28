package com.ivanna.omega.core

import android.content.Context

// LEGADO — no se llama desde ningun lado del repo fuera de este archivo
// (confirmado: 0 usos de PresetManager en toda la app). Los presets de
// sonido SI funcionan, pero por otra ruta: IvannaControlPanel.onPresetSelected
// usa ProfilesLoader.load(context) + profile.audioEngine.* -> dsp.value.copy()
// -> pushToNative() (ver ControlTabScreen.kt y MainActivity.kt). Esta clase
// parece ser una implementacion anterior, completa y funcional, que quedo sin
// usar cuando se migro a ProfilesLoader. No se borra (regla del proyecto:
// marcar legado, no eliminar) y no se cablea de nuevo sin antes decidir cual
// de los dos sistemas de presets debe ser el unico real — cablear ambos
// dejaria dos fuentes de verdad divergentes para lo mismo.
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
