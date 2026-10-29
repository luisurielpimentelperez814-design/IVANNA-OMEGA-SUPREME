package com.ivanna.omega.audio

import android.content.Context
import kotlinx.serialization.json.Json

/** Base profile only. Transient SAF/live adjustments are never baked into this snapshot. */
object EffectProfileStore {
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }
    internal fun encode(profile: IvannaEffectProfile): String =
        json.encodeToString(IvannaEffectProfile.serializer(), profile)
    internal fun decode(text: String): IvannaEffectProfile? = runCatching {
        json.decodeFromString(IvannaEffectProfile.serializer(), text)
    }.getOrNull()

    fun load(context: Context): IvannaEffectProfile? = runCatching {
        context.getSharedPreferences("ivanna_global_effect_profile", Context.MODE_PRIVATE)
            .getString("base", null)?.let(::decode)
    }.getOrNull()

    fun save(context: Context, profile: IvannaEffectProfile) {
        context.getSharedPreferences("ivanna_global_effect_profile", Context.MODE_PRIVATE)
            .edit().putString("base", encode(profile)).apply()
    }
}
