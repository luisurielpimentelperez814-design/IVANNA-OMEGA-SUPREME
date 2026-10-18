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
 * paramNorm     : métrica interna publicada por SAFCore.getState()[1]. NO equivale por sí sola
 *                 a convergencia perceptual (no hay objetivo, coste ni criterio de parada definidos)
 * errorEnergy   : media de los cinco errores direccionales aportados por la calibración, en [0,1]
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

    // FIX (desconexión): reset() accedía a IVANNAApplication.instance para
    // borrar las prefs — acoplamiento frágil (si el companion no expone
    // instance, o el proceso aún no tiene Application viva, crash). Se
    // conserva el applicationContext recibido en init() y se reutiliza.
    private var appContext: Context? = null

    // RT60 del entorno que se envía al puente SAF-Room. Arranca en 0.3 s, que es un PRESET, no
    // una medición: solo deja de serlo cuando la UI/medición llama a syncToRoomBridge(rt60).
    // Antes runCalibrationStep() lo forzaba a 0.3f en cada paso y pisaba el valor de la sala.
    @Volatile private var roomRt60Seconds: Float = DEFAULT_RT60_PRESET_S

    private const val DEFAULT_RT60_PRESET_S = 0.3f

    private const val PREFS_NAME = "saf_optimizer_prefs"

    /**
     * Inicializar el optimizador.
     * Llamado desde IVANNAApplication.onCreate() — carga el estado persistido
     * si existe, o deja el estado por defecto (iteration=0, sin calibrar).
     */
    fun init(context: Context) {
        appContext = context.applicationContext
        val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
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
        val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
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
        // Contrato: exactamente cinco errores finitos en [0,1]. Se rechaza ANTES de lanzar la
        // coroutine (un NaN/Inf o un array vacío contaminaba errorEnergy, que se persiste y se
        // envía al optimizador nativo) y se trabaja sobre una copia defensiva.
        val errors = SaFCalibrationValidator.sanitize(directionEnergies)
        if (errors == null) {
            Log.w(TAG, "Calibración rechazada: se requieren ${SaFCalibrationValidator.DIRECTIONS} " +
                       "errores finitos en [0,1] (recibidos=${directionEnergies.size})")
            return
        }
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
            val newErrorEnergy = SaFCalibrationValidator.meanError(errors)
            // safState[1] es la norma de la métrica de SAFCore; si el nativo devuelve un array
            // corto o un valor no finito se conserva el anterior en vez de propagar NaN.
            val newParamNorm   = safState.getOrNull(1)?.toFloat()
                ?.takeIf { it.isFinite() && it >= 0f } ?: current.paramNorm

            // Sujeto REAL del motor espacial: antes rotaba por una lista (round-robin), lo que
            // simulaba una personalización que no existe y quizá apuntaba a sujetos no cargados.
            val subject = runCatching { IvannaSpatialManager.currentHrtfSubject() }
                .getOrDefault(current.selectedSubject)

            _state.value = SaFOptimizerState(
                iteration       = newIteration,
                selectedSubject = subject,
                paramNorm       = newParamNorm,
                errorEnergy     = newErrorEnergy
            )

            Log.d(TAG, "Step $newIteration: subject=$subject " +
                       "paramNorm=%.3f errorEnergy=%.3f".format(newParamNorm, newErrorEnergy))

            // Persistir: init() restaura iteration/subject/paramNorm/errorEnergy de las prefs, pero
            // nadie llamaba a saveState(), así que lo restaurado era siempre el estado de fábrica.
            appContext?.let { saveState(it) }

            // FIX: sincronizar estado actualizado con SaFRoomBridge → M_t refleja H_t real.
            // Usa el RT60 recordado (el de la sala si la UI/medición lo ha enviado; si no, el preset).
            syncToRoomBridge()
        }
    }

    /** Reinicia el optimizador al estado inicial. */
    fun reset() {
        _state.value = SaFOptimizerState()
        runCatching { SaFRoomBridge.reset() }
            .onFailure { Log.w(TAG, "SaFRoomBridge.reset no disponible: ${it.message}") }
        appContext?.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            ?.edit()?.clear()?.apply()
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
    fun syncToRoomBridge(rt60: Float = roomRt60Seconds) {
        val st = _state.value
        // Recordar el RT60 recibido (si es válido): las llamadas internas sin argumento usan el
        // de la sala en lugar de volver al preset de 0.3 s.
        if (rt60.isFinite()) roomRt60Seconds = rt60.coerceIn(0f, 5f)
        val rt60Applied = roomRt60Seconds
        // FIX (desconexión): SaFRoomBridge es un singleton con estado nativo
        // — si la librería SAF no cargó o el bridge aún no está listo, cada
        // llamada lanzaba y el paso de calibración moría a mitad. Un fallo
        // aquí no debe abortar la calibración: se registra y se continúa.
        try {
            // H_t: mismatch normalizado — errorEnergy ya está en [0,1] por diseño
            // convergenceRate está definido como EMA de ‖Δ_t‖/‖Δ_0‖ (0 = convergido, 1 = inicio).
            // paramNorm (SAFCore.getState()[1]) NO es esa magnitud, y 0.0 afirmaría "convergido":
            // sin una métrica de progreso medida se envía 1.0 (sin evidencia de convergencia).
            SaFRoomBridge.setHrtfState(
                mismatchEnergy  = st.errorEnergy,
                convergenceRate = 1.0f
            )
            // R_t: sala actual (rt60 recordado; preset 0.3 s hasta que llegue uno de la sala).
            // drr sigue siendo un valor fijo (6 dB): no hay una medición conectada.
            SaFRoomBridge.setRoomState(rt60 = rt60Applied, drr = 6.0f)
            // NO se escribe target aquí. Antes se pisaba con un vector de CEROS en cada paso: el
            // target por defecto del optimizador nativo es el Golden Master (kMasterSafGoldenQ), no
            // cero, y SAFCore.update() fija el suyo. No existe una conversión validada de los cinco
            // errores direccionales a un target latente 7-D, así que no se inventa uno.
            // Ejecutar un paso Φ_SAF-Room^∞ con el contexto actual
            val alpha = SaFRoomBridge.step()
            Log.v(TAG, "syncToRoomBridge: iteration=${st.iteration} α*=$alpha rt60=$rt60Applied")
        } catch (t: Throwable) {
            Log.w(TAG, "syncToRoomBridge omitido (bridge no listo): ${t.message}")
        }
    }
}
