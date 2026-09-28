package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import kotlinx.serialization.*
import kotlinx.serialization.json.Json
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.neuromorphic.PiLstmBridge
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
            val resId = context.resources.getIdentifier(
                "audio_profiles", "raw", context.packageName
            )
            if (resId == 0) {
                Log.e(TAG, "Recurso raw/audio_profiles no encontrado")
                profiles = emptyMap()
                metadata = null
                return false
            }
            val inputStream = context.resources.openRawResource(resId)
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
            // Validar floats del JSON antes de JNI (valores corruptos crashean)
            val gain    = profile.audioEngine.gain.takeIf { it.isFinite() } ?: 0f
            val exciter = profile.audioEngine.exciterAmount.takeIf { it.isFinite() } ?: 0.3f
            val eqGain  = profile.audioEngine.eqGain.takeIf { it.isFinite() } ?: 0f
            val width   = profile.audioEngine.widthAmount.takeIf { it.isFinite() } ?: 0.5f
            // Aplicar parámetros de AudioEngine
            audioEngine.setGain(gain)
            audioEngine.setExciter(exciter)
            audioEngine.setEqGain(eqGain)
            audioEngine.setWidth(width)
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


    /**
     * applyToDsp — ruta REAL magistral.
     * Aplica el perfil completo a la cadena DSP activa:
     *   DSPState → pushToNative (gain/exciter/eq/width/stereo)
     *   PiLstmBridge (harmonic/lateralInhib/ohc/masterGain)
     *   nativeSetRouteProfileStatic (bass/dialog/widener)
     *   nativeSetAntiDolbyScoresStatic (preset scores)
     *
     * @param currentDsp estado actual (se usa como base para copy)
     * @param onDspUpdated callback con el nuevo DSPState listo para
     *        asignar a dsp.value en el Composable caller.
     */
    fun applyToDsp(
        currentDsp: DSPState,
        profileId: String,
        onDspUpdated: (DSPState) -> Unit
    ): Boolean {
        val profile = profiles[profileId] ?: run {
            Log.e(TAG, "applyToDsp: perfil no encontrado: $profileId")
            return false
        }
        fun Float.safe(default: Float, min: Float, max: Float): Float =
            if (isFinite()) coerceIn(min, max) else default

        return try {
            val gainSafe    = profile.audioEngine.gain.safe(0f,  -24f, 24f)
            val exciterSafe = profile.audioEngine.exciterAmount.safe(0.3f, 0f, 1f)
            val eqSafe      = profile.audioEngine.eqGain.safe(0f,  -18f, 18f)
            val widthSafe   = profile.audioEngine.widthAmount.safe(1f, 0f, 2f)

            val newDsp = currentDsp.copy(
                master       = gainSafe,
                wet          = exciterSafe,
                low          = eqSafe,
                mid          = eqSafe,
                high         = eqSafe,
                presence     = eqSafe,
                stereoWidth  = widthSafe
            )
            onDspUpdated(newDsp)
            runCatching { newDsp.pushToNative() }
                .onFailure { Log.e(TAG, "pushToNative: $it") }

            if (PiLstmBridge.isReady && IvannaNativeLib.isLoaded) {
                runCatching {
                    PiLstmBridge.setHarmonicGain(profile.neuromorphic.harmonicGain.safe(0.3f, 0f, 1f))
                    PiLstmBridge.setBeta(profile.neuromorphic.lateralInhibition.safe(0.5f, 0f, 1f))
                    PiLstmBridge.setAlpha(profile.neuromorphic.ohcCompression.safe(0.4f, 0f, 1f))
                    PiLstmBridge.setMasterGain(profile.neuromorphic.masterGainDb.safe(0f, -18f, 18f))
                }.onFailure { Log.e(TAG, "PiLstm neuro: $it") }
            }

            if (IvannaNativeLib.isLoaded) {
                runCatching {
                    AudioEngine.nativeSetRouteProfileStatic(
                        profile.route.bassBoostDb.safe(0f, -18f, 18f),
                        profile.route.dialogBoostDb.safe(0f, -18f, 18f),
                        profile.route.widenerMult.safe(1f, 0f, 3f)
                    )
                }.onFailure { Log.e(TAG, "route: $it") }

                val sp = profile.antiDolby.speechThreshold.safe(0.33f, 0f, 1f)
                val ba = profile.antiDolby.bassThreshold.safe(0.33f, 0f, 1f)
                val mu = (1f - sp - ba).coerceIn(0f, 1f)
                val total = sp + ba + mu
                val nSp = if (total > 0f) sp / total else 0.34f
                val nBa = if (total > 0f) ba / total else 0.33f
                val nMu = if (total > 0f) mu / total else 0.33f
                runCatching {
                    AudioEngine.nativeSetAntiDolbyScoresStatic(nSp, nMu, nBa)
                }.onFailure { Log.e(TAG, "antiDolby scores: $it") }
            }

            currentProfileId = profileId
            runCatching { ParameterStore(context).setCurrentAudioProfileId(profileId) }
            Log.i(TAG, "applyToDsp OK: ${profile.name}")
            true
        } catch (t: Throwable) {
            Log.e(TAG, "applyToDsp fatal: ${t.message}", t)
            false
        }
    }
}