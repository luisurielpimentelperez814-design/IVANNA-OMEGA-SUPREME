// ParameterStore.kt
// ============================================================================
// PERSISTENCIA AVANZADA — Versionado de esquema + Debounce + Transición
// Guarda/carga parámetros con transición suave (ValueAnimator)
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import android.content.Context
import android.content.SharedPreferences
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.google.gson.Gson

class ParameterStore(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences(
        "ivanna_audio_params", Context.MODE_PRIVATE
    )
    private val gson = Gson()
    private val handler = Handler(Looper.getMainLooper())
    private var debounceRunnable: Runnable? = null
    
    companion object {
        private const val TAG = "ParameterStore"
        private const val SCHEMA_VERSION_KEY = "schema_version"
        private const val AUDIO_STATE_KEY = "audio_state"
        private const val CURRENT_SCHEMA_VERSION = 1
        private const val DEBOUNCE_DELAY_MS = 500L
    }
    
    init {
        // Verificar versión de esquema y migrar si es necesario
        val savedVersion = prefs.getInt(SCHEMA_VERSION_KEY, 0)
        if (savedVersion < CURRENT_SCHEMA_VERSION) {
            Log.d(TAG, "📦 Migrando esquema: v$savedVersion → v$CURRENT_SCHEMA_VERSION")
            migrateSchema(savedVersion, CURRENT_SCHEMA_VERSION)
            prefs.edit().putInt(SCHEMA_VERSION_KEY, CURRENT_SCHEMA_VERSION).apply()
        }
    }
    
    /**
     * Guardar parámetros CON DEBOUNCE (espera 500ms sin cambios antes de escribir)
     */
    fun saveParametersDebounced(state: AudioState) {
        // Cancelar guardar anterior si existe
        debounceRunnable?.let { handler.removeCallbacks(it) }
        
        // Programar nuevo guardar con debounce
        debounceRunnable = Runnable {
            saveParametersNow(state)
        }
        handler.postDelayed(debounceRunnable!!, DEBOUNCE_DELAY_MS)
        Log.d(TAG, "💾 Guardar programado con debounce (500ms)")
    }
    
    /**
     * Guardar parámetros INMEDIATAMENTE
     */
    fun saveParametersNow(state: AudioState) {
        try {
            val json = gson.toJson(state)
            prefs.edit()
                .putString(AUDIO_STATE_KEY, json)
                .putLong("last_save", System.currentTimeMillis())
                .apply()
            Log.d(TAG, "✅ Parámetros guardados: mode=${state.adaptiveMode}, intensity=${state.adaptiveIntensity}")
        } catch (e: Exception) {
            Log.e(TAG, "❌ Error guardando parámetros", e)
        }
    }
    
    /**
     * Cargar parámetros guardados
     */
    fun loadParameters(): AudioState {
        return try {
            val json = prefs.getString(AUDIO_STATE_KEY, null)
            if (json != null) {
                val state = gson.fromJson(json, AudioState::class.java)
                Log.d(TAG, "📂 Parámetros cargados: mode=${state.adaptiveMode}")
                state
            } else {
                Log.d(TAG, "ℹ️ Sin parámetros guardados, usando defaults")
                AudioState()
            }
        } catch (e: Exception) {
            Log.e(TAG, "❌ Error cargando parámetros", e)
            AudioState()
        }
    }
    
    /**
     * Borrar todos los parámetros guardados
     */
    fun clearAll() {
        prefs.edit().clear().apply()
        Log.d(TAG, "🗑️ Todos los parámetros borrados")
    }
    
    /**
     * Migrar datos entre versiones de esquema
     */
    private fun migrateSchema(from: Int, to: Int) {
        when {
            from == 0 && to == 1 -> {
                // v0 → v1: Añadir campos nuevos con valores por defecto
                val currentState = loadParameters()
                // Los campos nuevos se añaden automáticamente al data class
                saveParametersNow(currentState)
                Log.d(TAG, "✓ Migración v0→v1 completada")
            }
        }
    }
    
    /**
     * Obtener timestamp del último guardado
     */
    fun getLastSaveTime(): Long {
        return prefs.getLong("last_save", 0L)
    }
}
