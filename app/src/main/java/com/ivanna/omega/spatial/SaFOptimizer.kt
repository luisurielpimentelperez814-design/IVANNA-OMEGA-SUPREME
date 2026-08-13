package com.ivanna.omega.spatial

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.SAFCore
import com.ivanna.omega.saf.SaFRoomBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * SaFOptimizerState — snapshot del estado del optimizador SAF.
 *
 * iteration     : número de iteración actual (0 = sin calibrar)
 * selectedSubject: ID del sujeto HRTF seleccionado tras la última iteración
 * paramNorm     : norma del vector de parámetros ‖p_t‖ (convergencia)
 * errorEnergy   : energía de error cuadrático E_t en la iteración actual
 */
data class SaFOptimizerState(
    val iteration: Int = 0,
    val selectedSubject: String = "",
    val paramNorm: Float = 0f,
    val errorEnergy: Float = 0f
)

/**
 * SaFOptimizer — optimizador Riemanniano de sujeto HRTF.
 *
 * IVANNAApplication.onCreate() llama a init(context) en el arranque.
 * IvannaAppShell.SpatialTab observa state como StateFlow.
 * SaFCalibrationScreen llama a runCalibrationStep() al completar la calibración.
 */
object SaFOptimizer {

    private const val TAG = "SaFOptimizer"

    private val _state = MutableStateFlow(SaFOptimizerState())
    val state: StateFlow<SaFOptimizerState> = _state

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    // Tabla mínima de sujetos conocidos (subset CIPIC + MIT).
    // El matching real usará distancia euclidiana sobre head-width / head-depth;
    // por ahora la selección es round-robin sobre esta lista durante la calib.
    private val KNOWN_SUBJECTS = listOf(
        "kemar_subject_165", "subject_003", "subject_008",
        "subject_009", "subject_010", "subject_011"
    )

    private const val PREFS_NAME = "saf_optimizer_prefs"

    /**
     * Inicializar el optimizador.
     * Llamado desde IVANNAApplication.onCreate() — carga el estado persistido
     * si existe, o deja el estado por defecto (iteration=0, sin calibrar).
     */
    fun init(context: Context) {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val iter = prefs.getInt("iteration", 0)
        val subj = prefs.getString("selectedSubject", "") ?: ""
        val pNorm = prefs.getFloat("paramNorm", 0f)
        val eEnergy = prefs.getFloat("errorEnergy", 0f)
        
        _state.value = SaFOptimizerState(iter, subj, pNorm, eEnergy)
        if (iter > 0) {
            syncToRoomBridge()
        }
        Log.d(TAG, "SaFOptimizer inicializado (iteration=${_state.value.iteration})")
    }
    
    private fun saveState(context: Context) {
        val st = _state.value
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit()
            .putInt("iteration", st.iteration)
            .putString("selectedSubject", st.selectedSubject)
            .putFloat("paramNorm", st.paramNorm)
            .putFloat("errorEnergy", st.errorEnergy)
            .apply()
    }

    /**
     * Ejecuta un paso de calibración dado un array de energía de error
     * medido por dirección [energy_front, energy_rear, energy_left, energy_right, energy_top].
     *
     * @param directionEnergies FloatArray[5] — error percibido por el usuario
     *                          en cada dirección (0=correcto, 1=máximo error).
     */
    fun runCalibrationStep(directionEnergies: FloatArray) {
        scope.launch {
            val current = _state.value

            // Usar SAFCore para actualizar el estado del optimizador
            val currentParams = doubleArrayOf(
                current.paramNorm.toDouble(),
                current.errorEnergy.toDouble(),
                current.iteration.toDouble(),
                0.0
            )
            val targetParams = DoubleArray(4) { 0.0 }  // convergencia a cero error
            val metric       = DoubleArray(4) { 1.0 }  // métrica uniforme

            SAFCore.update(currentParams, targetParams, metric)
            val safState = SAFCore.getState()  // [deltaEnergy, metricNorm, memory, gain]

            val newIteration   = current.iteration + 1
            val newErrorEnergy = directionEnergies.average().toFloat()
            val newParamNorm   = safState[1].toFloat()

            // Seleccionar sujeto: round-robin en calibración inicial.
            val subjectIdx = newIteration % KNOWN_SUBJECTS.size
            val subject    = KNOWN_SUBJECTS[subjectIdx]

            _state.value = SaFOptimizerState(
                iteration       = newIteration,
                selectedSubject = subject,
                paramNorm       = newParamNorm,
                errorEnergy     = newErrorEnergy
            )

            Log.d(TAG, "Step $newIteration: subject=$subject " +
                       "paramNorm=%.3f errorEnergy=%.3f".format(newParamNorm, newErrorEnergy))

            // FIX: sincronizar estado actualizado con SaFRoomBridge → M_t refleja H_t real
            syncToRoomBridge(rt60 = 0.3f)  // RT60 por defecto; RoomSimulator lo sobreescribirá
        }
    }

    /** Reinicia el optimizador al estado inicial. */
    fun reset() {
        _state.value = SaFOptimizerState()
        SaFRoomBridge.reset()
        val prefs = com.ivanna.omega.core.IVANNAApplication.instance.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().clear().apply()
        Log.d(TAG, "Optimizer reset")
    }

    /**
     * Alimenta SaFRoomBridge con el estado actual del optimizador HRTF.
     *
     * Debe llamarse en cada iteración de calibración para que M_t en C++
     * refleje el mismatch H_t real (y no el valor por defecto 0).
     *
     * @param rt60  Tiempo de reverberación del entorno de escucha [0, 5] s
     *              (puede venir de RoomSimulator.estimateRT60())
     */
    fun syncToRoomBridge(rt60: Float = 0.3f) {
        val st = _state.value
        // H_t: mismatch normalizado — errorEnergy ya está en [0,1] por diseño
        SaFRoomBridge.setHrtfState(
            mismatchEnergy  = st.errorEnergy,
            convergenceRate = if (st.iteration > 0) st.paramNorm else 1.0f
        )
        // R_t: sala actual
        SaFRoomBridge.setRoomState(rt60 = rt60, drr = 6.0f)
        // Target en el espacio latente SAF-Room: dirección de menor error
        // Proxy: vector cero (KEMAR medio) cuando sin datos de calibración
        SaFRoomBridge.setTarget(FloatArray(7) { 0.0f })
        // Ejecutar un paso Φ_SAF-Room^∞ con el contexto actual
        val alpha = SaFRoomBridge.step()
        Log.v(TAG, "syncToRoomBridge: iteration=${st.iteration} α*=$alpha rt60=$rt60")
    }
}
