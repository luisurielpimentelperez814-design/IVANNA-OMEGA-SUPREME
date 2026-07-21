package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import kotlinx.serialization.*
import kotlinx.serialization.json.Json
import java.io.InputStreamReader

/**
 * ProfilesLoader — coagulo único para cargar los presets de audio desde
 * app/src/main/res/raw/audio_profiles.json (4 perfiles: Steve Miller, RUSH,
 * Budgie, Grand Funk Railroad).
 *
 * Reutilizado por ProfileSelector.kt (UI) y por MainActivity cuando un
 * preset se aplica (MainActivity.applyAudioProfile(...) toma el AudioProfile
 * y lo replica a dspState / DSPBridge / globalEffectManager).
 *
 * Data class AudioProfile / AudioEngineParams / AntiDolbyParams /
 * NeuromorphicParams / RouteParams viven en este mismo archivo audio/
 * (definidos aquí también para no acoplarse a ProfileManager, que depende
 * de la clase AudioEngine y rota en una rama legacy — regla de oro: no
 * borramos, mantenemos ProfileManager intacto y agregamos una vía limpia).
 */
@Serializable
data class IvannaAudioProfile(
    val id: String,
    val name: String,
    val description: String,
    val category: String,
    val priority: Int,
    val audioEngine: IvannaAudioEngineParams,
    val antiDolby: IvannaAntiDolbyParams,
    val neuromorphic: IvannaNeuromorphicParams,
    val route: IvannaRouteParams,
    val tags: List<String>,
    val recommendedFor: String
)

@Serializable
data class IvannaAudioEngineParams(
    val gain: Float,
    val exciterAmount: Float,
    val eqGain: Float,
    val widthAmount: Float,
    val bypass: Boolean = false
)

@Serializable
data class IvannaAntiDolbyParams(
    val speechThreshold: Float,
    val bassThreshold: Float,
    val eqBoost2k4k: Float,
    val exciterLowOnly: Boolean = false,
    val widenerMultiplier: Float
)

@Serializable
data class IvannaNeuromorphicParams(
    val harmonicGain: Float,
    val lateralInhibition: Float,
    val ohcCompression: Float,
    val masterGainDb: Float,
    val cochlearBandwidth: String = "adaptive"
)

@Serializable
data class IvannaRouteParams(
    val bassBoostDb: Float,
    val dialogBoostDb: Float,
    val widenerMult: Float
)

@Serializable
data class IvannaProfilesContainer(
    val audioProfiles: List<IvannaAudioProfile>,
    val metadata: IvannaProfileMetadata
)

@Serializable
data class IvannaProfileMetadata(
    val version: String,
    val createdDate: String,
    val lastModified: String,
    val totalProfiles: Int,
    val trainingSequence: List<String>,
    val trainingNotes: String
)

object ProfilesLoader {
    private const val TAG = "ProfilesLoader"
    private const val RES_NAME = "audio_profiles"

    private val json = Json {
        ignoreUnknownKeys = true
        coerceInputValues = true
    }

    /** Carga la lista de perfiles desde res/raw/audio_profiles.json. */
    fun load(context: Context): List<IvannaAudioProfile> = try {
        val resId = context.resources.getIdentifier(RES_NAME, "raw", context.packageName)
        if (resId == 0) {
            Log.w(TAG, "Recurso raw/$RES_NAME no encontrado")
            emptyList()
        }
        context.resources.openRawResource(resId).use { stream ->
            val container = json.decodeFromString(
                IvannaProfilesContainer.serializer(),
                InputStreamReader(stream).readText()
            )
            Log.i(TAG, "✓ Cargados ${container.audioProfiles.size} perfiles")
            container.audioProfiles.sortedBy { it.priority }
        }
    } catch (e: Exception) {
        Log.e(TAG, "Error cargando profiles: ${e.message}", e)
        emptyList()
    }

    fun loadMetadata(context: Context): IvannaProfileMetadata? = try {
        val resId = context.resources.getIdentifier(RES_NAME, "raw", context.packageName)
        context.resources.openRawResource(resId).use { stream ->
            json.decodeFromString(
                IvannaProfilesContainer.serializer(),
                InputStreamReader(stream).readText()
            ).metadata
        }
    } catch (_: Exception) { null }
}
