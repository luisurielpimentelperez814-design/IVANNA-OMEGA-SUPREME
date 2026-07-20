package com.ivanna.omega.adaptive

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * ViewModel del motor adaptativo magistral.
 * Analiza el audio en tiempo real y controla automáticamente
 * todos los parámetros DSP @ 10Hz.
 */
class AdaptiveViewModel : ViewModel() {

    // ── Estado de telemetría ──
    var telemetry by mutableStateOf(AdaptiveTelemetrySnapshot())
        private set

    // ── Estado del motor adaptativo ──
    var isRunning by mutableStateOf(false)
        private set

    var isVoiceProtectionEnabled by mutableStateOf(false)
        private set

    var isManualMode by mutableStateOf(false)
        private set

    // ── Parámetros objetivo calculados por el motor ──
    var targetDrive by mutableFloatStateOf(0.3f)
        private set
    var targetWet by mutableFloatStateOf(0.5f)
        private set
    var targetMix by mutableFloatStateOf(0.7f)
        private set
    var targetAlpha by mutableFloatStateOf(0.4f)
        private set
    var targetBeta by mutableFloatStateOf(0.6f)
        private set
    var targetGamma by mutableFloatStateOf(0.5f)
        private set
    var targetLow by mutableFloatStateOf(0f)
        private set
    var targetMid by mutableFloatStateOf(0f)
        private set
    var targetHigh by mutableFloatStateOf(0f)
        private set
    var targetPresence by mutableFloatStateOf(0f)
        private set
    var targetMaster by mutableFloatStateOf(0f)
        private set

    // ── Estado del análisis ──
    var currentBpm by mutableFloatStateOf(120f)
        private set
    var spectralClass by mutableIntStateOf(0)
        private set
    var genomeFitness by mutableFloatStateOf(0f)
        private set
    var analysisConfidence by mutableFloatStateOf(0f)
        private set

    // ── Callback para aplicar cambios al DSP nativo ──
    private var onApplyDsp: ((AdaptiveTelemetrySnapshot) -> Unit)? = null

    fun setDspCallback(callback: (AdaptiveTelemetrySnapshot) -> Unit) {
        onApplyDsp = callback
    }

    // ── Control del motor ──
    fun start() {
        if (isRunning) return
        isRunning = true
        viewModelScope.launch {
            adaptiveLoop()
        }
    }

    fun stop() {
        isRunning = false
    }

    fun toggleVoiceProtection() {
        isVoiceProtectionEnabled = !isVoiceProtectionEnabled
    }

    fun toggleManualMode() {
        isManualMode = !isManualMode
    }

    // ── Loop adaptativo @ 10Hz ──
    private suspend fun adaptiveLoop() {
        while (isRunning) {
            // Simular análisis del backend (en producción vendría del JNI)
            val snapshot = analyzeAudio()
            telemetry = snapshot

            if (!isManualMode) {
                // Motor de control automático
                calculateTargets(snapshot)
                applyTargets()
            }

            delay(100L) // 10Hz
        }
    }

    /**
     * Análisis inteligente del audio.
     * En producción esto vendría del backend C++ vía JNI.
     */
    private fun analyzeAudio(): AdaptiveTelemetrySnapshot {
        // Simulación del análisis espectral + evolutivo
        val time = System.currentTimeMillis() / 1000.0
        val simulatedBpm = 120f + (kotlin.math.sin(time) * 20).toFloat()
        val simulatedClass = ((time / 10) % 4).toInt()
        val simulatedFitness = (kotlin.math.sin(time * 0.5) * 0.5 + 0.5).toFloat()

        return AdaptiveTelemetrySnapshot(
            bpm = simulatedBpm.coerceIn(60f, 200f),
            spectralClass = simulatedClass,
            genomeFitness = simulatedFitness,
            mode = if (isVoiceProtectionEnabled) 2 else 1
        )
    }

    /**
     * Calcula los parámetros objetivo basados en el análisis.
     * Convierte cualquier melodía en deleite auditivo.
     */
    private fun calculateTargets(snapshot: AdaptiveTelemetrySnapshot) {
        val fitness = snapshot.genomeFitness
        val bpmFactor = (snapshot.bpm - 60f) / 140f // Normalizar 60-200 BPM

        // Compresor adaptativo
        targetAlpha = (0.3f + fitness * 0.4f).coerceIn(0f, 1f)
        targetBeta = (0.4f + bpmFactor * 0.3f).coerceIn(0f, 1f)

        // Excitador armónico
        targetDrive = (0.2f + fitness * 0.5f).coerceIn(0f, 1f)
        targetWet = (0.4f + fitness * 0.4f).coerceIn(0f, 1f)
        targetMix = (0.6f + bpmFactor * 0.2f).coerceIn(0f, 1f)

        // EQ adaptativo por clase espectral
        when (snapshot.spectralClass) {
            0 -> { // Bajos dominantes
                targetLow = 2f * fitness
                targetMid = 0.5f * fitness
                targetHigh = -1f * fitness
                targetPresence = 0.5f * fitness
            }
            1 -> { // Medios dominantes
                targetLow = 0f
                targetMid = 3f * fitness
                targetHigh = 1f * fitness
                targetPresence = 2f * fitness
            }
            2 -> { // Altos dominantes
                targetLow = -1f * fitness
                targetMid = 1f * fitness
                targetHigh = 3f * fitness
                targetPresence = 2f * fitness
            }
            3 -> { // Espectral plano / ruido
                targetLow = 1f * fitness
                targetMid = 1f * fitness
                targetHigh = 1f * fitness
                targetPresence = 1f * fitness
            }
        }

        targetMaster = 0f
        analysisConfidence = fitness
    }

    private fun applyTargets() {
        val snapshot = AdaptiveTelemetrySnapshot(
            drive = targetDrive,
            wet = targetWet,
            mix = targetMix,
            alpha = targetAlpha,
            beta = targetBeta,
            gamma = targetGamma,
            low = targetLow,
            mid = targetMid,
            high = targetHigh,
            presence = targetPresence,
            master = targetMaster,
            mode = if (isVoiceProtectionEnabled) 2 else 1,
            bpm = currentBpm,
            spectralClass = spectralClass,
            genomeFitness = genomeFitness
        )
        onApplyDsp?.invoke(snapshot)
    }

    // ── Aplicar parámetros manuales ──
    fun applyManualParams(params: AdaptiveTelemetrySnapshot) {
        if (isManualMode) {
            onApplyDsp?.invoke(params)
        }
    }

    override fun onCleared() {
        super.onCleared()
        isRunning = false
    }
}
