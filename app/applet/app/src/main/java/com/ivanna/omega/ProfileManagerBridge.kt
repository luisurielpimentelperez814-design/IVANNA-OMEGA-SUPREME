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

    // FIX (crash de inicialización): el constructor se invoca dentro de un
    // remember{} de Compose en MainActivity (ruta "profiles"). El contexto
    // que llega ahí es el de la Activity, que puede estar parcialmente
    // inicializado si Compose recompone antes de tiempo (rotación, process
    // death, backstack). Cualquier acceso a resources / JSON / JNI con ese
    // contexto en el init{} revienta la composición en caliente.
    //
    // Solución: se usa applicationContext para TODO el trabajo pesado — es
    // inmune al ciclo de vida de la Activity y nunca se invalida a mitad
    // de una recomposición. Además el init entero va envuelto en
    // runCatching para que un fallo puntual (resources/JNI) degrade a una
    // lista vacía en vez de tumbar la app.
    private val appContext = context.applicationContext

    private val manager: ProfileManager by lazy {
        ProfileManager(
            appContext,
            com.ivanna.omega.audio.AudioEngine().apply { runCatching { initialize() } }
        )
    }

    /** Perfiles en formato IvannaAudioProfile para ProfileSelectorScreen */
    val ivannaProfiles: List<IvannaAudioProfile> by lazy {
        runCatching { ProfilesLoader.load(appContext) }
            .onFailure { Log.e(TAG, "ivannaProfiles load falló: ${it.message}") }
            .getOrElse { emptyList() }
    }

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
