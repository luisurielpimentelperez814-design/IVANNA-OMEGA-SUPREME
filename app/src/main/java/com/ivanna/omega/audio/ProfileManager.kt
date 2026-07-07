package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import kotlinx.serialization.*
import kotlinx.serialization.json.Json
import java.io.InputStreamReader

/**
 * ProfileManager — Carga y aplica presets de audio desde audio_profiles.json
 * Gestiona perfiles para bandas específicas (Steve Miller, RUSH, Budgie, Grand Funk)
 */

@Serializable
data class AudioEngineParams(
    val gain: Float,
    val exciterAmount: Float,
    val eqGain: Float,
    val widthAmount: Float,
    val bypass: Boolean = false
)

@Serializable
data class AntiDolbyParams(
    val speechThreshold: Float,
    val bassThreshold: Float,
    val eqBoost2k4k: Float,
    val exciterLowOnly: Boolean = false,
    val widenerMultiplier: Float
)

@Serializable
data class NeuromorphicParams(
    val harmonicGain: Float,
    val lateralInhibition: Float,
    val ohcCompression: Float,
    val masterGainDb: Float,
    val cochlearBandwidth: String = "adaptive"
)

@Serializable
data class RouteParams(
    val bassBoostDb: Float,
    val dialogBoostDb: Float,
    val widenerMult: Float
)

@Serializable
data class AudioProfile(
    val id: String,
    val name: String,
    val description: String,
    val category: String,
    val priority: Int,
    val audioEngine: AudioEngineParams,
    val antiDolby: AntiDolbyParams,
    val neuromorphic: NeuromorphicParams,
    val route: RouteParams,
    val tags: List<String>,
    val recommendedFor: String
)

@Serializable
data class AudioProfilesContainer(
    val audioProfiles: List<AudioProfile>,
    val metadata: ProfileMetadata
)

@Serializable
data class ProfileMetadata(
    val version: String,
    val createdDate: String,
    val lastModified: String,
    val totalProfiles: Int,
    val trainingSequence: List<String>,
    val trainingNotes: String
)

class ProfileManager(private val context: Context, private val audioEngine: AudioEngine) {
    private val TAG = "ProfileManager"
    private var profiles: Map<String, AudioProfile> = emptyMap()
    private var currentProfileId: String? = null
    private var metadata: ProfileMetadata? = null

    init {
        loadProfiles()
    }

    /**
     * Carga los presets de audio desde audio_profiles.json (raw resources)
     */
    fun loadProfiles(): Boolean {
        return try {
            val inputStream = context.resources.openRawResource(
                context.resources.getIdentifier(
                    "audio_profiles",
                    "raw",
                    context.packageName
                )
            )
            val reader = InputStreamReader(inputStream)
            val json = Json {
                ignoreUnknownKeys = true
                coerceInputValues = true
            }
            val container = json.decodeFromString<AudioProfilesContainer>(reader.readText())
            
            profiles = container.audioProfiles.associateBy { it.id }
            metadata = container.metadata
            
            Log.i(TAG, "✓ Cargados ${profiles.size} perfiles de audio")
            profiles.forEach { (id, profile) ->
                Log.d(TAG, "  • $id: ${profile.name}")
            }
            true
        } catch (e: Exception) {
            Log.e(TAG, "✗ Error cargando perfiles: ${e.message}", e)
            false
        }
    }

    /**
     * Aplica un perfil al AudioEngine (por ID)
     */
    fun applyProfile(profileId: String): Boolean {
        val profile = profiles[profileId] ?: run {
            Log.e(TAG, "Perfil no encontrado: $profileId")
            return false
        }

        return try {
            // Aplicar parámetros de AudioEngine
            audioEngine.setGain(profile.audioEngine.gain)
            audioEngine.setExciter(profile.audioEngine.exciterAmount)
            audioEngine.setEqGain(profile.audioEngine.eqGain)
            audioEngine.setWidth(profile.audioEngine.widthAmount)
            audioEngine.setBypass(profile.audioEngine.bypass)

            // Aplicar ruta de audio
            AudioEngine.nativeSetRouteProfileStatic(
                profile.route.bassBoostDb,
                profile.route.dialogBoostDb,
                profile.route.widenerMult
            )

            currentProfileId = profileId
            Log.i(TAG, "✓ Aplicado perfil: ${profile.name}")
            true
        } catch (e: Exception) {
            Log.e(TAG, "✗ Error aplicando perfil $profileId: ${e.message}", e)
            false
        }
    }

    /**
     * Obtiene información detallada de un perfil
     */
    fun getProfile(profileId: String): AudioProfile? = profiles[profileId]

    /**
     * Lista todos los perfiles disponibles
     */
    fun getAllProfiles(): List<AudioProfile> = profiles.values.sortedBy { it.priority }

    /**
     * Obtiene la secuencia recomendada de entrenamiento
     */
    fun getTrainingSequence(): List<AudioProfile> {
        return metadata?.trainingSequence?.mapNotNull { profiles[it] } ?: emptyList()
    }

    /**
     * Obtiene el ID del perfil actual
     */
    fun getCurrentProfileId(): String? = currentProfileId

    /**
     * Obtiene el perfil actual
     */
    fun getCurrentProfile(): AudioProfile? = currentProfileId?.let { profiles[it] }

    /**
     * Busca perfiles por etiquetas
     */
    fun findProfilesByTag(tag: String): List<AudioProfile> {
        return profiles.values.filter { it.tags.contains(tag) }
    }

    /**
     * Busca perfiles por categoría
     */
    fun findProfilesByCategory(category: String): List<AudioProfile> {
        return profiles.values.filter { it.category == category }
    }

    /**
     * Obtiene las notas de entrenamiento
     */
    fun getTrainingNotes(): String? = metadata?.trainingNotes

    /**
     * Aplica un perfil por nombre (fuzzy search)
     */
    fun applyProfileByName(name: String): Boolean {
        val profile = profiles.values.find { 
            it.name.equals(name, ignoreCase = true) 
        }
        return if (profile != null) {
            applyProfile(profile.id)
        } else {
            Log.w(TAG, "Perfil no encontrado por nombre: $name")
            false
        }
    }

    /**
     * Obtiene estadísticas de los perfiles
     */
    fun getProfileStats(): String {
        val allProfiles = getAllProfiles()
        val avgGain = allProfiles.map { it.audioEngine.gain }.average()
        val avgExciter = allProfiles.map { it.audioEngine.exciterAmount }.average()
        val avgEq = allProfiles.map { it.audioEngine.eqGain }.average()
        val avgWidth = allProfiles.map { it.audioEngine.widthAmount }.average()

        return """
            ╔════════════════════════════════════════╗
            ║     ESTADÍSTICAS DE PERFILES IVANNA    ║
            ╚════════════════════════════════════════╝
            
            Total de perfiles: ${profiles.size}
            Versión: ${metadata?.version}
            Última actualización: ${metadata?.lastModified}
            
            PROMEDIOS:
            ├─ Gain:          ${"%.2f".format(avgGain)}
            ├─ Exciter:       ${"%.2f".format(avgExciter)}
            ├─ EQ (dB):       ${"%.2f".format(avgEq)}
            └─ Width:         ${"%.2f".format(avgWidth)}
            
            PERFILES DISPONIBLES:
            ${allProfiles.joinToString("\n") { 
                "├─ [${it.id}] ${it.name}\n" +
                "│  Tags: ${it.tags.joinToString(", ")}\n" +
                "│  Para: ${it.recommendedFor}"
            }}
        """.trimIndent()
    }
}
