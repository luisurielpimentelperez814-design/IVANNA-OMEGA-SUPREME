package com.ivanna.omega.audio

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.AdaptiveTelemetrySnapshot
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
    private val paramManager = AudioParameterManager()

    private val _telemetry = MutableStateFlow(AdaptiveTelemetry())
    val telemetry: StateFlow<AdaptiveTelemetry> = _telemetry

    // Parche 8: score real de VoiceProtectionController
    @Volatile private var voiceProtectionScore: Float = 0f
    fun setVoiceProtectionScore(score: Float) { voiceProtectionScore = score }

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
            val motorActive = IvannaNativeLib.nativeIsAdaptiveEngineRunning()
            // FIX (telemetria 0% Ruta B): compAmount[4] y voiceProtect[8] en 0
            // con motor activo = Ruta B sin Ruta A. Usar IvannaUnifiedPipeline.
            val src = if (motorActive && raw[4] == 0f && raw[8] == 0f)
                IvannaUnifiedPipeline.toAdaptiveTelemetryArray() else raw
            _telemetry.value = AdaptiveTelemetry(
                rms          = src[0],
                peakDb       = src[1],
                grDb         = src[2],
                targetGain   = src[3],
                compAmount   = src[4],
                excReduction = src[5],
                spatialWidth = src[6],
                safetyMargin = src[7],
                voiceProtect = if (src[8] == 0f && voiceProtectionScore > 0f) voiceProtectionScore else src[8],
                motorRunning = motorActive
            )
        } catch (e: Throwable) {
            // Motor no inicializado todavía — no es error
        }
    }

    // ── Phase Oracle: mapeo de intensidad única a alpha/beta/gamma ──────────
    // Una sola "intensidad de coherencia" (0..1) produce tres valores que
    // describen una curva espectral: máximo en LF, suavizado en MF, mínimo en HF.
    private fun applyPhaseOracle(state: AudioState) {
        if (!IvannaNativeLib.isLoaded) return
        val i = state.phaseOracleIntensity
        if (i == 0f) return  // Off completamente: no tocar el oracle nativo
        try {
            IvannaNativeLib.nativeSetPhaseParameters(
                alpha = i.coerceIn(0f, 1f),          // coherencia LF
                beta  = (i * 0.7f).coerceIn(0f, 1f), // coherencia MF
                gamma = (i * 0.5f).coerceIn(0f, 1f)  // coherencia HF
            )
        } catch (e: Throwable) {
            Log.w(TAG, "applyPhaseOracle: motor no disponible")
        }
    }

    // ── Preset con transición suave (AudioParameterManager) ─────────────────
    // Usa ValueAnimator para interpolar todos los parámetros continuos en 400ms.
    // Llamar desde onPresetSelected (en lugar de pushToNative directo) para
    // evitar el salto brusco en EQ/compresor/exciter/ancho al cambiar preset.
    fun applyPresetWithTransition(toState: AudioState) {
        val fromState = AudioStateManager.getCurrentState()
        paramManager.applyParametersWithTransition(
            fromState   = fromState,
            toState     = toState,
            durationMs  = 400L
        ) { interpolated ->
            dspUpdater.forceUpdate(interpolated)
            applyEQ(interpolated)
            applyPhaseOracle(interpolated)
        }
        // Actualizar AudioStateManager al estado final para que el resto de la
        // app vea el destino correcto incluso antes de que termine la animación
        AudioStateManager.updateState { toState }
        persistState(toState)
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
        applyPhaseOracle(modulated)
        persistState(modulated)
        // Empujar estado al daemon omega_daemon via OmegaEngineBridge:
        // masterGain   → SET_AI_RUNTIME_GAIN  (rango 0.5..1.0)
        // compRatio    → SET_AI_RUNTIME_COMP  (normalizado 1..20 → 0..1)
        // exciterAmount → SET_AI_RUNTIME_EXCRED (reducción = 1 - amount)
        OmegaEngineBridge.pushAdaptiveState(
            targetGain = modulated.masterGain.coerceIn(0.5f, 1.0f),
            compAmount = ((modulated.compressorRatio - 1f) / 19f).coerceIn(0f, 1f),
            excRed     = (1f - modulated.exciterAmount).coerceIn(0f, 1f)
        )
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
        applyPhaseOracle(modulated)
        persistState(modulated)
        OmegaEngineBridge.pushAdaptiveState(
            targetGain = modulated.masterGain.coerceIn(0.5f, 1.0f),
            compAmount = ((modulated.compressorRatio - 1f) / 19f).coerceIn(0f, 1f),
            excRed     = (1f - modulated.exciterAmount).coerceIn(0f, 1f)
        )
    }

    // ── EQ en tiempo real: nativeSetEQParams(low, mid, high, master) ────────
    // FIX QUIRÚRGICO (bug confirmado): la versión anterior armaba un
    // FloatArray(13){0f} y llamaba a nativeSetParams (que sobreescribe TODO
    // g_params y dispara setParams() en g_eq + g_comp + g_exciter + g_widener
    // + g_gain). Como el array solo llenaba los índices 8/9/10/12, el resto
    // (drive/wet/mix/alpha/beta/gamma/freq/resonance, índices 0..7) llegaba
    // en 0 — apagando compresor, exciter y el mix de entrada de la ganancia
    // en cada movimiento de un slider de EQ.
    // Ahora se usa un setter JNI dedicado que solo toca low/mid/high/master
    // y solo reconfigura g_eq/g_gain — nunca toca compresor/exciter/widener.
    private fun applyEQ(state: AudioState) {
        if (!IvannaNativeLib.isLoaded) return
        try {
            IvannaNativeLib.nativeSetEQParams(
                state.eqBass.coerceIn(-18f, 18f),
                state.eqMid.coerceIn(-18f, 18f),
                state.eqTreble.coerceIn(-18f, 18f),
                state.masterGain.coerceIn(0.1f, 2f)
            )
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

// FIX: puente entre la telemetría interna del backend (AdaptiveTelemetry)
// y el snapshot que espera la UI (AdaptiveTelemetrySnapshot). Mismos 9
// campos de datos con distinto naming; appliedCount no existe en el
// origen todavía (no hay contador de aplicaciones en AdaptiveBackend),
// queda en 0 — no se inventa un valor.
fun AdaptiveTelemetry.toSnapshot(): AdaptiveTelemetrySnapshot = AdaptiveTelemetrySnapshot(
    running = motorRunning,
    rms = rms,
    peak = peakDb,
    gainReductionDb = grDb,
    targetGain = targetGain,
    compressorAmount = compAmount,
    exciterReduction = excReduction,
    spatialWidth = spatialWidth,
    safetyMargin = safetyMargin,
    voiceProtectionAmount = voiceProtect,
    appliedCount = 0L
)
