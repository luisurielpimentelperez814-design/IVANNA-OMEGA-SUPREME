package com.ivanna.omega.audio

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

// ── Telemetría en tiempo real del motor (solo lectura, 10Hz) ────────────────
data class AdaptiveTelemetry(
    val rms: Float = 0f,          // RMS de la señal actual
    val peakDb: Float = 0f,       // Pico en dBFS
    val grDb: Float = 0f,         // Ganancia de reducción del compresor (negativo = comprimiendo)
    val targetGain: Float = 1f,   // Ganancia objetivo del motor adaptativo
    val compAmount: Float = 0f,   // Cuánto está comprimiendo Motor A ahora mismo
    val excReduction: Float = 0f, // Reducción de exciter por voice protection
    val spatialWidth: Float = 1f, // Ancho espacial actual en el motor
    val safetyMargin: Float = 0f, // Margen de safety del limiter
    val voiceProtect: Float = 0f, // Score de protección de voz (0..1)
    val motorRunning: Boolean = false  // Si Motor A está activo
)

// ─────────────────────────────────────────────────────────────────────────────
// AdaptiveBackend — Backend real de AdaptiveEngineScreen
//
// Responsabilidades:
// 1. Leer telemetría real de Motor A (nativeGetAdaptiveTelemetry, 10Hz)
// 2. En modo manual: pasar AudioState por AdaptiveEngineModulator
//    (curvas Bézier/tanh/suavizado, ya implementadas pero nunca usadas)
//    y aplicar el resultado vía DspStateUpdater.
// 3. EQ en tiempo real: nativeSetParams con índices [8,9,10] = low/mid/high
//    (confirmado en ivanna_omega_jni.cpp:778-780).
// 4. Persistir estado en audio/ParameterStore (Gson, independiente del
//    core/ParameterStore que usa el resto de la app).
// 5. Exponer StateFlow<AdaptiveTelemetry> para que la UI actualice VU/GR
//    metros y el estado de Motor A sin polling desde el Composable.
// ─────────────────────────────────────────────────────────────────────────────
class AdaptiveBackend(context: Context) {

    private val modulator = AdaptiveEngineModulator()
    private val dspUpdater = DspStateUpdater()
    private val store = ParameterStore(context)
    private val handler = Handler(Looper.getMainLooper())

    private val _telemetry = MutableStateFlow(AdaptiveTelemetry())
    val telemetry: StateFlow<AdaptiveTelemetry> = _telemetry

    private var telemetryRunnable: Runnable? = null
    private var manualModeActive = false

    companion object {
        private const val TAG = "AdaptiveBackend"
        private const val TELEMETRY_INTERVAL_MS = 100L  // 10Hz
    }

    // ── Ciclo de telemetría ──────────────────────────────────────────────────
    fun startTelemetry() {
        stopTelemetry()
        telemetryRunnable = object : Runnable {
            override fun run() {
                pollTelemetry()
                handler.postDelayed(this, TELEMETRY_INTERVAL_MS)
            }
        }
        handler.post(telemetryRunnable!!)
        Log.d(TAG, "Telemetría iniciada @10Hz")
    }

    fun stopTelemetry() {
        telemetryRunnable?.let { handler.removeCallbacks(it) }
        telemetryRunnable = null
    }

    private fun pollTelemetry() {
        try {
            val raw = IvannaNativeLib.nativeGetAdaptiveTelemetry() ?: return
            if (raw.size < 10) return
            _telemetry.value = AdaptiveTelemetry(
                rms          = raw[0],
                peakDb       = raw[1],
                grDb         = raw[2],
                targetGain   = raw[3],
                compAmount   = raw[4],
                excReduction = raw[5],
                spatialWidth = raw[6],
                safetyMargin = raw[7],
                voiceProtect = raw[8],
                motorRunning = IvannaNativeLib.nativeIsAdaptiveEngineRunning()
            )
        } catch (e: Throwable) {
            // Motor no inicializado todavía — no es error
        }
    }

    // ── Modo manual: aplicar AudioState con pipeline de modulación real ──────
    fun applyManualState(state: AudioState) {
        val modulated = modulator.modulateAdaptiveOutput(
            baseState = state,
            mode = state.adaptiveMode,
            intensity = state.adaptiveIntensity
        )
        dspUpdater.requestUpdate(modulated)
        applyEQ(modulated)
        persistState(modulated)
        Log.d(TAG, "Manual: ratio=%.2f exciter=%.2f width=%.2f".format(
            modulated.compressorRatio, modulated.exciterAmount, modulated.spatialWidth))
    }

    fun forceManualState(state: AudioState) {
        val modulated = modulator.modulateAdaptiveOutput(
            baseState = state,
            mode = state.adaptiveMode,
            intensity = state.adaptiveIntensity
        )
        dspUpdater.forceUpdate(modulated)
        applyEQ(modulated)
        persistState(modulated)
    }

    // ── EQ en tiempo real: nativeSetParams índices 8,9,10 = low/mid/high ────
    // Índices confirmados en jni/ivanna_omega_jni.cpp:778-780.
    // nativeSetParams aplica todos los motores (g_eq, g_comp, g_exciter,
    // g_widener, g_gain) de una sola vez — mínima contención de lock.
    // Pasamos solo los índices que nos interesan (array de 11 elementos,
    // el resto en 0f = no modifica esos params en el motor, porque los
    // `if (n>=X)` sí ejecutan pero escriben 0 — mejor enviar los valores
    // actuales de g_params en vez de ceros para no pisar configuraciones
    // de otros módulos. Usamos los campos del AudioState para reconstruir
    // el array completo de forma coherente.
    private fun applyEQ(state: AudioState) {
        try {
            // Array de 13 posiciones = máximo que acepta nativeSetParams.
            // [0]=drive [1]=wet [2]=mix [3]=alpha [4]=beta [5]=gamma
            // [6]=freq  [7]=resonance [8]=low [9]=mid [10]=high
            // [11]=presence [12]=master
            // Solo tocar 8,9,10 (EQ), 12 (master gain) — el resto neutro/0
            // para no interferir con los parámetros que otros módulos ya
            // configuraron. No existe un getter JNI de los params actuales,
            // así que el contrato es claro: este array solo controla EQ+gain.
            val params = FloatArray(13) { 0f }
            params[8]  = state.eqBass        // low shelf (dB)
            params[9]  = state.eqMid         // mid peak (dB)
            params[10] = state.eqTreble      // high shelf (dB)
            params[12] = state.masterGain    // master gain
            IvannaNativeLib.nativeSetParams(params)
        } catch (e: Throwable) {
            Log.w(TAG, "applyEQ: motor no disponible todavía")
        }
    }

    // ── Persistencia ─────────────────────────────────────────────────────────
    private fun persistState(state: AudioState) {
        try {
            store.saveParametersDebounced(state)
        } catch (e: Throwable) {
            Log.w(TAG, "persistState: $e")
        }
    }

    fun restoreState(): AudioState? {
        return try {
            store.loadParameters()
        } catch (e: Throwable) {
            Log.w(TAG, "restoreState: $e")
            null
        }
    }

    fun resetModulator() = modulator.reset()
}
