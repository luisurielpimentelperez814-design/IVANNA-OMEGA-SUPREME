package com.ivanna.omega

import android.content.Context
import android.util.Log
import androidx.compose.runtime.*
import androidx.compose.ui.platform.LocalContext
import com.ivanna.omega.audio.AudioProfile
import com.ivanna.omega.audio.ProfileManager
import com.ivanna.omega.audio.ProfilesLoader
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.audio.IvannaAudioProfile

/**
 * ProfileManagerBridge — Puente real entre ProfileManager (legacy JSON)
 * y la cadena DSP activa (DSPState + PiLstmBridge + nativa).
 *
 * Uso en MainActivity:
 *
 *   val profileBridge = remember { ProfileManagerBridge(context) }
 *
 *   ProfileSelectorScreen(
 *       profiles = profileBridge.ivannaProfiles,
 *       onApply  = { p -> profileBridge.applyProfile(p.id, dsp.value) { dsp.value = it } }
 *   )
 */
class ProfileManagerBridge(context: Context) {

    private val TAG = "ProfileManagerBridge"
    private val manager = ProfileManager(context, com.ivanna.omega.audio.AudioEngine())

    /** Perfiles en formato IvannaAudioProfile para ProfileSelectorScreen */
    val ivannaProfiles: List<IvannaAudioProfile> = ProfilesLoader.load(context)

    /**
     * Aplica el perfil completo a la cadena DSP activa.
     * @param profileId     ID del perfil a aplicar
     * @param currentDsp    Estado DSP actual
     * @param onDspUpdated  Callback con el nuevo DSPState (asignar a dsp.value)
     */
    fun applyProfile(
        profileId: String,
        currentDsp: DSPState,
        onDspUpdated: (DSPState) -> Unit
    ) {
        val ok = manager.applyToDsp(currentDsp, profileId, onDspUpdated)
        if (!ok) Log.w(TAG, "applyProfile falló para: $profileId")
    }

    fun getAllProfiles(): List<AudioProfile> = manager.getAllProfiles()
    fun getCurrentProfileId(): String? = manager.getCurrentProfileId()
    fun getTrainingSequence(): List<AudioProfile> = manager.getTrainingSequence()
}
