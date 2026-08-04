package com.ivanna.omega.spatial

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
 * Expone `state` como StateFlow para que la UI lo observe.
 * La UI llama a runCalibrationStep() para cada dirección de calibración;
 * el optimizador actualiza el estado vía SAFCore y selecciona el sujeto HRTF
 * con menor energía de error acumulada.
 *
 * Diseño deliberadamente minimalista: la lógica de búsqueda real
 * (matching antropométrico sobre la tabla CIPIC) queda para fases futuras.
 * Lo que ya funciona aquí: el estado se expone de forma correcta y
 * compile-safe para que IvannaAppShell.kt pueda observarlo.
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
     * Ejecuta un paso de calibración dado un array de energía de error
     * medido por dirección [energy_front, energy_rear, energy_left, energy_right, energy_top].
     *
     * @param directionEnergies FloatArray[5] — error percibido por el usuario
     *                          en cada dirección de calibración (0=correcto, 1=máximo error).
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

            val newIteration = current.iteration + 1
            val newErrorEnergy = directionEnergies.average().toFloat()
            val newParamNorm   = safState[1].toFloat()

            // Seleccionar sujeto: round-robin en calibración inicial,
            // en fases futuras se usará matching por head-width/depth.
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
        SAFCore
        _state.value = SaFOptimizerState()
        Log.d(TAG, "Optimizer reset")
    }
}
