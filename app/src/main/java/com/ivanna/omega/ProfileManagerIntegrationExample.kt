package com.ivanna.omega

import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import com.ivanna.omega.audio.AudioEngine
import com.ivanna.omega.audio.ProfileManager
import com.ivanna.omega.ui.ProfileSelectorScreen
import android.content.Context
import android.util.Log

/**
 * INTEGRATION EXAMPLE: Cómo usar ProfileManager en tu MainActivity
 * 
 * Este archivo muestra cómo integrar la gestión de perfiles en tu app.
 */

// ════════════════════════════════════════════════════════════════════════════════
// OPCIÓN 1: En MainActivity (Jetpack Compose)
// ════════════════════════════════════════════════════════════════════════════════

// @Composable
// fun MainActivityContent() {
//     val context = LocalContext.current
//     val audioEngine = remember { AudioEngine() }
//     val profileManager = remember { ProfileManager(context, audioEngine) }
//
//     // Estado para mostrar selector de perfiles
//     var showProfileSelector by remember { mutableStateOf(false) }
//
//     Scaffold(
//         topBar = {
//             TopAppBar(
//                 title = { Text("IVANNA OMEGA SUPREME") },
//                 actions = {
//                     IconButton(onClick = { showProfileSelector = !showProfileSelector }) {
//                         Icon(Icons.Default.Settings, contentDescription = "Profiles")
//                     }
//                 }
//             )
//         }
//     ) { paddingValues ->
//         if (showProfileSelector) {
//             ProfileSelectorScreen(
//                 profileManager = profileManager,
//                 onProfileSelected = { profileId ->
//                     profileManager.applyProfile(profileId)
//                     showProfileSelector = false
//                 },
//                 modifier = Modifier.padding(paddingValues)
//             )
//         } else {
//             // ... tu contenido normal de la app
//         }
//     }
// }

// ════════════════════════════════════════════════════════════════════════════════
// OPCIÓN 2: En Activity (sin Compose) — Uso directo
// ════════════════════════════════════════════════════════════════════════════════

object ProfileManagerUsageExample {
    
    fun exampleUsageInActivity(context: Context, audioEngine: AudioEngine) {
        val TAG = "ProfileUsageExample"
        
        // ── Inicializar ProfileManager ──────────────────────────────────────
        val profileManager = ProfileManager(context, audioEngine)
        
        // ── Opción A: Aplicar perfil por ID ─────────────────────────────────
        profileManager.applyProfile("steve_miller_band")
        // Resultado: AudioEngine configurado con parámetros de Steve Miller
        
        // ── Opción B: Aplicar perfil por nombre ─────────────────────────────
        profileManager.applyProfileByName("RUSH")
        // Resultado: AudioEngine configurado con parámetros de RUSH
        
        // ── Opción C: Obtener todos los perfiles ────────────────────────────
        val allProfiles = profileManager.getAllProfiles()
        allProfiles.forEach { profile ->
            Log.d(TAG, "${profile.name}: ${profile.description}")
        }
        
        // ── Opción D: Obtener perfil específico ─────────────────────────────
        val rushProfile = profileManager.getProfile("rush")
        rushProfile?.let {
            Log.d(TAG, "RUSH - Gain: ${it.audioEngine.gain}, EQ: ${it.audioEngine.eqGain}dB")
        }
        
        // ── Opción E: Buscar perfiles por etiqueta ──────────────────────────
        val heavyRockProfiles = profileManager.findProfilesByTag("hard_rock")
        // Resultado: [Budgie]
        
        // ── Opción F: Buscar perfiles por categoría ─────────────────────────
        val classicRockProfiles = profileManager.findProfilesByCategory("classic_rock_70s")
        // Resultado: [Steve Miller, Grand Funk Railroad]
        
        // ── Opción G: Obtener secuencia de entrenamiento recomendada ────────
        val trainingSequence = profileManager.getTrainingSequence()
        Log.d(TAG, "Entrenamiento recomendado:")
        trainingSequence.forEachIndexed { index, profile ->
            Log.d(TAG, "  ${index + 1}. ${profile.name}")
        }
        // Resultado:
        // 1. Steve Miller Band
        // 2. Grand Funk Railroad
        // 3. RUSH
        // 4. Budgie
        
        // ── Opción H: Obtener notas de entrenamiento ────────────────────────
        val notes = profileManager.getTrainingNotes()
        Log.d(TAG, notes ?: "Sin notas de entrenamiento")
        
        // ── Opción I: Obtener perfil actual ────────────────────────────────
        val currentId = profileManager.getCurrentProfileId()
        val currentProfile = profileManager.getCurrentProfile()
        Log.d(TAG, "Perfil actual: ${currentProfile?.name}")
        
        // ── Opción J: Obtener estadísticas ─────────────────────────────────
        val stats = profileManager.getProfileStats()
        Log.d(TAG, stats)
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// OPCIÓN 3: Listener de cambios de perfil
// ════════════════════════════════════════════════════════════════════════════════

interface ProfileChangeListener {
    fun onProfileChanged(profileId: String, profileName: String)
    fun onProfileError(error: String)
}

class ProfileManagerWithListener(
    private val context: Context,
    private val audioEngine: AudioEngine
) : ProfileChangeListener {
    private val profileManager = ProfileManager(context, audioEngine)
    private val listeners = mutableListOf<ProfileChangeListener>()
    
    fun addListener(listener: ProfileChangeListener) {
        listeners.add(listener)
    }
    
    fun applyProfileWithNotification(profileId: String): Boolean {
        val success = profileManager.applyProfile(profileId)
        
        if (success) {
            val profile = profileManager.getProfile(profileId)
            profile?.let {
                listeners.forEach { listener ->
                    listener.onProfileChanged(profileId, it.name)
                }
            }
        } else {
            listeners.forEach { listener ->
                listener.onProfileError("No se pudo aplicar perfil: $profileId")
            }
        }
        
        return success
    }
    
    override fun onProfileChanged(profileId: String, profileName: String) {
        // Implementar en UI
    }
    
    override fun onProfileError(error: String) {
        // Implementar en UI
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// OPCIÓN 4: Integración con entrenamiento automático
// ════════════════════════════════════════════════════════════════════════════════

class AutomaticTrainingSequence(
    private val context: Context,
    private val audioEngine: AudioEngine
) {
    private val profileManager = ProfileManager(context, audioEngine)
    private val TAG = "AutoTraining"
    
    /**
     * Inicia secuencia automática de entrenamiento de IVANNA
     * Applica cada perfil en orden recomendado
     */
    suspend fun startTrainingSequence(
        hoursPerProfile: Int = 12,
        onProgressUpdate: (profileName: String, progress: Int) -> Unit = { _, _ -> }
    ) {
        val sequence = profileManager.getTrainingSequence()
        
        sequence.forEachIndexed { index, profile ->
            Log.i(TAG, "Iniciando entrenamiento con: ${profile.name}")
            profileManager.applyProfile(profile.id)
            
            // Simular duración (en producción, sería real basado en uso)
            repeat(hoursPerProfile * 60) { minute ->
                val progress = ((minute + 1) / (hoursPerProfile * 60f)) * 100
                onProgressUpdate(profile.name, progress.toInt())
                
                // Aquí va el procesamiento de audio real
                // audioEngine.processAudio(...)
            }
            
            Log.i(TAG, "✓ Completado: ${profile.name}")
        }
        
        Log.i(TAG, "✓ Secuencia de entrenamiento COMPLETADA")
        Log.i(TAG, profileManager.getProfileStats())
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// USO EN CORRUTINA (Kotlin Coroutines)
// ════════════════════════════════════════════════════════════════════════════════

// viewModel.launchUI {
//     val trainer = AutomaticTrainingSequence(context, audioEngine)
//     trainer.startTrainingSequence(hoursPerProfile = 12) { profileName, progress ->
//         updateUI {
//             trainingLabel.text = "Training: $profileName"
//             progressBar.progress = progress
//         }
//     }
// }

// ════════════════════════════════════════════════════════════════════════════════
// QUICK REFERENCE - Funciones principales
// ════════════════════════════════════════════════════════════════════════════════

/*
FUNCIONES PRINCIPALES DEL ProfileManager:

1. loadProfiles()
   └─ Carga perfiles desde audio_profiles.json
   
2. applyProfile(profileId: String): Boolean
   └─ Aplica perfil al AudioEngine por ID
   
3. applyProfileByName(name: String): Boolean
   └─ Aplica perfil al AudioEngine por nombre
   
4. getProfile(profileId: String): AudioProfile?
   └─ Obtiene perfil específico
   
5. getAllProfiles(): List<AudioProfile>
   └─ Obtiene todos los perfiles (ordenados por prioridad)
   
6. findProfilesByTag(tag: String): List<AudioProfile>
   └─ Busca perfiles por etiqueta
   
7. findProfilesByCategory(category: String): List<AudioProfile>
   └─ Busca perfiles por categoría
   
8. getTrainingSequence(): List<AudioProfile>
   └─ Obtiene secuencia recomendada de entrenamiento
   
9. getTrainingNotes(): String?
   └─ Obtiene notas sobre el entrenamiento
   
10. getCurrentProfileId(): String?
    └─ Obtiene ID del perfil actual
    
11. getCurrentProfile(): AudioProfile?
    └─ Obtiene perfil actual completo
    
12. getProfileStats(): String
    └─ Obtiene estadísticas formateadas de todos los perfiles

ETIQUETAS DISPONIBLES:
├─ vocal
├─ funk
├─ warm
├─ clarity
├─ progressive
├─ technical
├─ high_dynamics
├─ spatial
├─ complex
├─ hard_rock
├─ power
├─ dense
├─ heavy_bass
├─ compressed
├─ groove
├─ powerful_vocals
├─ classic_rock
└─ danceable

CATEGORÍAS DISPONIBLES:
├─ classic_rock_70s
├─ progressive_rock
└─ hard_rock
*/
