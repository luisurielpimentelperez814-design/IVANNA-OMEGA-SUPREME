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
import com.ivanna.omega.core.ParameterStore as CoreParameterStore

/**
 * Store del blob AudioState (Gson) — NO es la fuente de verdad del estado
 * adaptativo. La SSOT es [com.ivanna.omega.core.ParameterStore]: la UI real
 * (MainActivity / ControlTabScreen / VoiceController) lee y escribe ahí.
 *
 * Esta clase persiste el resto de AudioState (compresor, exciter, EQ, spatial,
 * flags de runtime) en su propio fichero de prefs — se conserva tal cual para
 * NO perder configuración ya guardada por usuarios — y espeja hacia la SSOT los
 * 4 campos que ambos almacenes compartían (split-brain resuelto):
 *   adaptiveMode, adaptiveIntensity, voiceProtectionEnabled, manualModeEnabled.
 */
class ParameterStore(context: Context) {
    private val appContext = context.applicationContext
    private val prefs: SharedPreferences = appContext.getSharedPreferences(
        "ivanna_audio_params", Context.MODE_PRIVATE
    )
    private val core = CoreParameterStore(appContext)
    private val gson = Gson()
    private val handler = Handler(Looper.getMainLooper())
    private var debounceRunnable: Runnable? = null

    // Ultimo estado solicitado via debounce. Sin retenerlo, un flush de
    // emergencia (flushPendingSave) no tendria QUE escribir: el runnable
    // cancelado se lleva su captura de `state` a la tumba.
    @Volatile private var lastRequestedState: AudioState? = null
    
    companion object {
        private const val TAG = "ParameterStore"
        private const val SCHEMA_VERSION_KEY = "schema_version"
        private const val AUDIO_STATE_KEY = "audio_state"
        private const val CURRENT_SCHEMA_VERSION = 2
        private const val DEBOUNCE_DELAY_MS = 500L

        // Instancias vivas con debounce pendiente potencial. AudioStateManager
        // y AdaptiveBackend crean CADA UNO su propio ParameterStore (verificado
        // por grep) — un flush llamado sobre una sola instancia dejaria la
        // ventana de 500ms de la otra abierta. Se registran todas y el flush
        // de ciclo de vida barre el conjunto. WeakReference: la instancia vive
        // lo que viva su duena (manager singleton), sin leaks si alguna se
        // recolecta.
        private val instances = java.util.Collections.synchronizedList(
            java.util.ArrayList<java.lang.ref.WeakReference<ParameterStore>>()
        )

        /**
         * Flush global de emergencia: escribe a disco TODO estado con debounce
         * pendiente en cualquier instancia viva. Llamar desde onTrimMemory /
         * ON_STOP del proceso — si el sistema mata la app dentro de la ventana
         * de 500ms, sin esto el ultimo ajuste del slider se pierde en silencio.
         */
        fun flushAllPending() {
            synchronized(instances) {
                val it = instances.iterator()
                while (it.hasNext()) {
                    val ref = it.next().get()
                    if (ref == null) it.remove() else ref.flushPendingSave()
                }
            }
        }
    }
    
    init {
        instances.add(java.lang.ref.WeakReference(this))
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
        lastRequestedState = state
        
        // Programar nuevo guardar con debounce
        debounceRunnable = Runnable {
            saveParametersNow(state)
        }
        handler.postDelayed(debounceRunnable!!, DEBOUNCE_DELAY_MS)
        Log.d(TAG, "💾 Guardar programado con debounce (500ms)")
    }

    /**
     * Escribe a disco el ultimo estado solicitado AHORA, cancelando el
     * debounce pendiente. Sin este flush, si el proceso moria dentro de la
     * ventana de 500ms (usuario ajusta un slider y el sistema mata la app al
     * ir a background, p.ej. por presion de memoria), el ultimo ajuste se
     * perdia en silencio: el runnable nunca corrio y la RAM muere con el
     * proceso. Idempotente y seguro de llamar aunque no haya nada pendiente.
     */
    fun flushPendingSave() {
        debounceRunnable?.let { handler.removeCallbacks(it) }
        debounceRunnable = null
        lastRequestedState?.let {
            Log.d(TAG, "💾 Flush de emergencia: escribiendo estado pendiente")
            saveParametersNow(it)
        }
    }
    
    /**
     * Guardar parámetros INMEDIATAMENTE
     */
    fun saveParametersNow(state: AudioState) {
        try {
            val json = gson.toJson(state)
            // commit() sincrono, no apply(): el blob es UN string pequeno, el
            // coste de disco es despreciable, y con apply() un kill del proceso
            // antes del flush perdia el cambio sin aviso (mismo bug ya visto y
            // corregido en DSPStatePrefs). commit() devuelve Boolean: si falla,
            // queda log — el llamante ya no "miente" al usuario.
            val ok = prefs.edit()
                .putString(AUDIO_STATE_KEY, json)
                .putLong("last_save", System.currentTimeMillis())
                .commit()
            if (!ok) Log.w(TAG, "⚠️ commit() devolvio false — el blob puede no haber llegado a disco")
            mirrorToCore(state)
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
                val state = reconcileWithCore(gson.fromJson(json, AudioState::class.java))
                Log.d(TAG, "📂 Parámetros cargados: mode=${state.adaptiveMode}")
                state
            } else {
                Log.d(TAG, "ℹ️ Sin blob guardado, sembrando desde la SSOT (core)")
                reconcileWithCore(AudioState())
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
        val ok = prefs.edit().clear().commit()
        if (!ok) Log.w(TAG, "⚠️ clearAll: commit() devolvio false")
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
        if (from < 2 && to >= 2) {
            // v1 → v2: reconciliación de split-brain. Antes de este cambio la UI
            // escribía el estado adaptativo SOLO en core.ParameterStore, así que
            // el blob puede tener valores viejos. Se toma core como autoridad
            // para los 4 campos compartidos y se reescribe el blob.
            saveParametersNow(reconcileWithCore(loadRawBlob()))
            Log.d(TAG, "✓ Migración v1→v2 completada (SSOT = core.ParameterStore)")
        }
    }

    private fun loadRawBlob(): AudioState = try {
        prefs.getString(AUDIO_STATE_KEY, null)
            ?.let { gson.fromJson(it, AudioState::class.java) }
            ?: AudioState()
    } catch (e: Exception) {
        Log.e(TAG, "❌ Blob corrupto, usando defaults", e)
        AudioState()
    }

    /** Toma de core.ParameterStore (SSOT) los campos que el usuario ya tocó. */
    private fun reconcileWithCore(state: AudioState): AudioState {
        var out = state
        if (core.hasAdaptiveMode()) {
            val ordinal = core.getAdaptiveModeOrdinal().coerceIn(0, AdaptiveMode.values().size - 1)
            out = out.copy(adaptiveMode = AdaptiveMode.values()[ordinal])
        }
        if (core.hasAdaptiveIntensity()) {
            // core guarda 0..100 (slider de UI); AudioState usa 0..1
            out = out.copy(adaptiveIntensity = (core.getAdaptiveIntensity() / 100f).coerceIn(0f, 1f))
        }
        if (core.hasVoiceProtection()) {
            out = out.copy(voiceProtectionEnabled = core.isVoiceProtectionEnabled())
        }
        if (core.hasAdaptiveManualMode()) {
            out = out.copy(manualModeEnabled = core.isAdaptiveManualModeEnabled())
        }

        // FIX TAREA 3 (canal muerto del preset signature): el preset
        // IVANNA_OMEGA_SIGNATURE escribe sus valores en core.ParameterStore,
        // pero el restore del DSP lee el AudioState de ESTE archivo. Sin
        // reconciliar las claves del signature aquí, el preset quedaba
        // guardado en disco pero nunca llegaba al motor nativo. Solo se
        // aplican si el signature ya fue sembrado (hasSignatureSeed) Y el
        // campo del AudioState sigue en su default de fábrica — así el
        // preset da el primer arranque premium sin sobrescribir a un
        // usuario que después ajustó algo.
        if (core.hasSignatureSeed()) {
            // Solo sembrar campos que el usuario NO haya tocado aún (default
            // de fábrica). El preset es el punto de partida, no un override.
            if (out.spatialWidth == 1.0f)   out = out.copy(spatialWidth = core.getSpatialWidth().coerceIn(0.5f, 2f))
            if (out.exciterAmount == 0.5f)  out = out.copy(exciterAmount = core.getExciter().coerceIn(0f, 1f))
            if (out.eqTreble == 0f)         out = out.copy(eqTreble = core.getEqGain().coerceIn(-12f, 12f))
            if (out.binaural)               out = out.copy(binaural = core.getNpeHrtf())
            // spatialEnabled / antiDolbyEnabled son toggles de engine que la
            // app aplica en su propio arranque (IvannaGlobalEffectManager /
            // AntiDolbyController) — no viven en AudioState; su reconciliación
            // se hace en el punto donde cada engine lee la SSOT.
        }
        return out
    }

    /** Espeja hacia la SSOT los campos compartidos, para que no divergan. */
    private fun mirrorToCore(state: AudioState) {
        try {
            core.setAdaptiveModeOrdinal(state.adaptiveMode.ordinal)
            core.setAdaptiveIntensity((state.adaptiveIntensity * 100f).coerceIn(0f, 100f))
            core.setVoiceProtectionEnabled(state.voiceProtectionEnabled)
            core.setAdaptiveManualModeEnabled(state.manualModeEnabled)
        } catch (e: Exception) {
            Log.e(TAG, "❌ Error espejando a core.ParameterStore", e)
        }
    }
    
    /**
     * Obtener timestamp del último guardado
     */
    fun getLastSaveTime(): Long {
        return prefs.getLong("last_save", 0L)
    }
}
