// AdaptiveEngineModulator.kt
// ============================================================================
// MODULACIÓN DEL MOTOR ADAPTATIVO — Curvas no lineales + Soft clipping + Suavizado
// Mapea intensidad → parámetros usando Bézier y tanh
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import android.util.Log
import kotlin.math.tanh
import kotlin.math.pow

class AdaptiveEngineModulator {
    companion object {
        private const val TAG = "AdaptiveEngineModulator"
        private const val SMOOTHING_ALPHA = 0.2f  // Filtro exponencial (0-1)
    }
    
    private var previousModulatedState: AudioState? = null
    
    /**
     * Modular salida del motor adaptativo usando curvas no lineales
     * @param baseState Estado calculado por el motor
     * @param mode Modo adaptativo (OFF, NATURAL, STUDIO, EXTREME)
     * @param intensity Intensidad 0..1
     * @return Estado modulado y suavizado
     */
    fun modulateAdaptiveOutput(
        baseState: AudioState,
        mode: AdaptiveMode,
        intensity: Float
    ): AudioState {
        // 1. Mapear intensidad usando curvas de potencia según modo
        val intensityFactor = mapIntensityToFactor(intensity, mode)
        
        // 2. Aplicar factor a parámetros adaptativos
        val modulatedState = applyIntensityModulation(baseState, intensityFactor)
        
        // 3. Aplicar soft clipping para evitar distorsión
        val clippedState = applySoftClipping(modulatedState)
        
        // 4. Suavizar con filtro exponencial
        val smoothedState = smoothParameter(clippedState)
        
        Log.d(TAG, "🎚️ Modulación: mode=$mode, intensity=$intensity (factor=$intensityFactor)")
        
        return smoothedState
    }
    
    /**
     * Mapear intensidad (0..1) a factor multiplicativo usando curva no lineal
     * Diferentes curvas según modo para diferentes comportamientos
     */
    private fun mapIntensityToFactor(intensity: Float, mode: AdaptiveMode): Float {
        val clamped = intensity.coerceIn(0f, 1f)
        
        return when (mode) {
            AdaptiveMode.OFF -> 0f
            AdaptiveMode.NATURAL -> clamped.pow(0.8f)  // Curva suave (exponente < 1)
            AdaptiveMode.STUDIO -> clamped.pow(1.0f)   // Lineal
            AdaptiveMode.EXTREME -> clamped.pow(1.5f)  // Curva agresiva (exponente > 1)
        }
    }
    
    /**
     * Aplicar factor a todos los parámetros modulables
     */
    private fun applyIntensityModulation(
        state: AudioState,
        factor: Float
    ): AudioState {
        return state.copy(
            // Compressor: más agresivo con mayor intensidad
            compressorRatio = 2.0f + (state.compressorRatio - 2.0f) * factor,
            
            // Exciter: más énfasis con mayor intensidad
            exciterAmount = state.exciterAmount * factor,
            
            // Spatial: más ancho con mayor intensidad
            spatialWidth = 1.0f + (state.spatialWidth - 1.0f) * factor,
            
            // Master gain: amplificar con factor (pero con límite)
            masterGain = state.masterGain * (0.8f + 0.4f * factor)  // 0.8x a 1.2x
        )
    }
    
    /**
     * Aplicar soft clipping (tanh) para evitar valores extremos
     * tanh(x) ≈ x para valores pequeños, se satura smoothly en ±1
     */
    private fun applySoftClipping(state: AudioState): AudioState {
        return state.copy(
            compressorThreshold = softClipValue(state.compressorThreshold, -40f, 0f),
            compressorRatio = softClipValue(state.compressorRatio, 1f, 8f),
            exciterAmount = softClipValue(state.exciterAmount, 0f, 1f),
            spatialWidth = softClipValue(state.spatialWidth, 0f, 2f),
            masterGain = softClipValue(state.masterGain, 0.1f, 2f)
        )
    }
    
    /**
     * Soft clip con tanh — mapea valor a rango usando función sigmoide suave
     */
    private fun softClipValue(value: Float, min: Float, max: Float): Float {
        val normalized = (value - (min + max) / 2) / ((max - min) / 2)
        val clipped = tanh(normalized.toDouble()).toFloat()  // tanh comprime a (-1, 1)
        return (clipped + 1) / 2 * (max - min) + min
    }
    
    /**
     * Filtro exponencial de primer orden (suavizado)
     * new = old * (1 - α) + target * α
     * α pequeño = cambios lentos, α grande = cambios rápidos
     */
    private fun smoothParameter(newState: AudioState): AudioState {
        val previous = previousModulatedState ?: return newState.also {
            previousModulatedState = it
        }
        
        val smoothed = AudioState(
            adaptiveMode = newState.adaptiveMode,  // Discreto
            adaptiveIntensity = newState.adaptiveIntensity,
            
            compressorThreshold = smooth(previous.compressorThreshold, newState.compressorThreshold),
            compressorRatio = smooth(previous.compressorRatio, newState.compressorRatio),
            compressorAttack = smooth(previous.compressorAttack, newState.compressorAttack),
            compressorRelease = smooth(previous.compressorRelease, newState.compressorRelease),
            
            exciterAmount = smooth(previous.exciterAmount, newState.exciterAmount),
            exciterFreq = smooth(previous.exciterFreq, newState.exciterFreq),
            
            spatialWidth = smooth(previous.spatialWidth, newState.spatialWidth),
            spatialIntensity = smooth(previous.spatialIntensity, newState.spatialIntensity),
            
            eqBass = smooth(previous.eqBass, newState.eqBass),
            eqMid = smooth(previous.eqMid, newState.eqMid),
            eqTreble = smooth(previous.eqTreble, newState.eqTreble),
            
            masterGain = smooth(previous.masterGain, newState.masterGain),
            
            binaural = newState.binaural,
            manifold = newState.manifold,
            voiceProtectionEnabled = newState.voiceProtectionEnabled
        )
        
        previousModulatedState = smoothed
        return smoothed
    }
    
    /**
     * Interpolación exponencial: new = old * (1 - α) + target * α
     */
    private fun smooth(old: Float, target: Float): Float {
        return old * (1 - SMOOTHING_ALPHA) + target * SMOOTHING_ALPHA
    }
    
    /**
     * Reset del estado anterior (para cambios bruscos autorizados)
     */
    fun reset() {
        previousModulatedState = null
        Log.d(TAG, "🔄 Modulador reset")
    }
}
