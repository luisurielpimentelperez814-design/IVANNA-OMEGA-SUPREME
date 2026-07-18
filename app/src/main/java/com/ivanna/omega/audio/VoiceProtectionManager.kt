// VoiceProtectionManager.kt
// ============================================================================
// VOICE PROTECTION MANAGER — Restauración correcta + Indicadores visuales
// Gestiona voice protection con callbacks cuando bridgePlayer esté listo
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import android.util.Log
import com.ivanna.omega.audio.bridge.BridgePlayer

class VoiceProtectionManager(
    private val parameterStore: ParameterStore
) {
    private val _voiceProtectionActive = MutableLiveData(false)
    val voiceProtectionActive: LiveData<Boolean> = _voiceProtectionActive
    
    private var bridgePlayer: BridgePlayer? = null
    private var pendingApplications = mutableListOf<Boolean>()
    
    companion object {
        private const val TAG = "VoiceProtectionManager"
    }
    
    /**
     * Habilitar voice protection
     */
    fun enable() {
        Log.d(TAG, "🎤 Habilitando Voice Protection")
        _voiceProtectionActive.value = true
        
        // Guardar en persistencia
        AudioStateManager.updateState {
            it.copy(voiceProtectionEnabled = true)
        }
        
        // Aplicar al engine si está disponible
        applyToEngine(true)
    }
    
    /**
     * Deshabilitar voice protection
     */
    fun disable() {
        Log.d(TAG, "🎤 Deshabilitando Voice Protection")
        _voiceProtectionActive.value = false
        
        // Guardar en persistencia
        AudioStateManager.updateState {
            it.copy(voiceProtectionEnabled = false)
        }
        
        // Aplicar al engine
        applyToEngine(false)
    }
    
    /**
     * Togglear voice protection
     */
    fun toggle() {
        if (isActive()) {
            disable()
        } else {
            enable()
        }
    }
    
    /**
     * ¿Está activo?
     */
    fun isActive(): Boolean = _voiceProtectionActive.value == true
    
    /**
     * Registrar bridgePlayer y aplicar protección pendiente
     */
    fun registerBridgePlayer(player: BridgePlayer) {
        bridgePlayer = player
        Log.d(TAG, "✅ BridgePlayer registrado")
        
        // Aplicar cualquier protección pendiente
        pendingApplications.forEach { enabled ->
            applyToEngine(enabled)
        }
        pendingApplications.clear()
    }
    
    /**
     * Cargar y restaurar voice protection al arrancar app
     */
    fun loadAndRestore() {
        try {
            val savedState = parameterStore.loadParameters()
            val wasEnabled = savedState.voiceProtectionEnabled
            
            if (wasEnabled) {
                Log.d(TAG, "📂 Restaurando Voice Protection (estaba habilitado)")
                _voiceProtectionActive.value = true
                applyToEngine(true)
            } else {
                Log.d(TAG, "📂 Voice Protection estaba deshabilitado")
                _voiceProtectionActive.value = false
            }
        } catch (e: Exception) {
            Log.e(TAG, "❌ Error restaurando Voice Protection", e)
        }
    }
    
    /**
     * Aplicar voice protection al engine nativo
     */
    private fun applyToEngine(enabled: Boolean) {
        if (bridgePlayer != null) {
            try {
                // Aplicar al bridgePlayer
                bridgePlayer?.setVoiceProtection(enabled)
                Log.d(TAG, if (enabled) "✅ Voice Protection ACTIVADO en engine" else "❌ Voice Protection DESACTIVADO")
            } catch (e: Exception) {
                Log.e(TAG, "Error aplicando voice protection", e)
            }
        } else {
            // Guardar como pendiente para aplicar cuando esté disponible
            if (!pendingApplications.contains(enabled)) {
                pendingApplications.add(enabled)
                Log.d(TAG, "⏳ Voice Protection marcado como pendiente (bridgePlayer no disponible)")
            }
        }
    }
    
    /**
     * Mostrar estado en logs para debugging
     */
    fun debugState() {
        Log.d(TAG, """
            ========== Voice Protection State ==========
            Activo: ${isActive()}
            BridgePlayer registrado: ${bridgePlayer != null}
            Aplicaciones pendientes: ${pendingApplications.size}
            ==========================================
        """.trimIndent())
    }
}
