package com.ivanna.omega.spatial

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.SAFCore
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

    /**
     * Inicializar el optimizador.
     * Llamado desde IVANNAApplication.onCreate() — carga el estado persistido
     * si existe, o deja el estado por defecto (iteration=0, sin calibrar).
     */
    fun init(context: Context) {
        // FIX: IVANNAApplication llama a SaFOptimizer.init(this) en onCreate().
        // Por ahora no hay persistencia; el estado se restablece a default en
        // cada arranque. Fase futura: cargar desde SharedPreferences o archivo.
        Log.d(TAG, "SaFOptimizer inicializado (iteration=${_state.value.iteration})")
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
        }
    }

    /** Reinicia el optimizador al estado inicial. */
    fun reset() {
        _state.value = SaFOptimizerState()
        Log.d(TAG, "Optimizer reset")
    }
}
