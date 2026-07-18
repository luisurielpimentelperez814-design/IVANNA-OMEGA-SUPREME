// AudioParameterManager.kt
// ============================================================================
// APLICACIÓN DE PARÁMETROS CON TRANSICIÓN SUAVE (ValueAnimator)
// Evita saltos bruscos al cambiar parámetros
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import android.animation.ValueAnimator
import android.util.Log
import kotlin.math.pow

class AudioParameterManager {
    private var currentAnimator: ValueAnimator? = null
    
    companion object {
        private const val TAG = "AudioParameterManager"
        private const val DEFAULT_TRANSITION_MS = 300L
    }
    
    /**
     * Aplicar parámetros con transición suave
     * @param fromState Estado anterior
     * @param toState Estado objetivo
     * @param durationMs Duración de la transición (ms)
     * @param onUpdate Callback cada frame de animación
     */
    fun applyParametersWithTransition(
        fromState: AudioState,
        toState: AudioState,
        durationMs: Long = DEFAULT_TRANSITION_MS,
        onUpdate: (AudioState) -> Unit
    ) {
        // Cancelar animación anterior si existe
        currentAnimator?.cancel()
        
        // Crear animador
        currentAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = durationMs
            
            addUpdateListener { animator ->
                val progress = animator.animatedValue as Float
                
                // Interpolar entre estados
                val interpolatedState = AudioState(
                    adaptiveMode = toState.adaptiveMode,  // Cambios discretos
                    adaptiveIntensity = lerp(fromState.adaptiveIntensity, toState.adaptiveIntensity, progress),
                    compressorThreshold = lerp(fromState.compressorThreshold, toState.compressorThreshold, progress),
                    compressorRatio = lerp(fromState.compressorRatio, toState.compressorRatio, progress),
                    exciterAmount = lerp(fromState.exciterAmount, toState.exciterAmount, progress),
                    spatialWidth = lerp(fromState.spatialWidth, toState.spatialWidth, progress),
                    eqBass = lerp(fromState.eqBass, toState.eqBass, progress),
                    eqMid = lerp(fromState.eqMid, toState.eqMid, progress),
                    eqTreble = lerp(fromState.eqTreble, toState.eqTreble, progress),
                    masterGain = lerp(fromState.masterGain, toState.masterGain, progress),
                    binaural = toState.binaural,
                    manifold = toState.manifold,
                    voiceProtectionEnabled = toState.voiceProtectionEnabled
                )
                
                onUpdate(interpolatedState)
            }
            
            addListener(object : android.animation.Animator.AnimatorListener {
                override fun onAnimationStart(animation: android.animation.Animator) {
                    Log.d(TAG, "▶️ Transición de parámetros iniciada (${durationMs}ms)")
                }
                override fun onAnimationEnd(animation: android.animation.Animator) {
                    Log.d(TAG, "✅ Transición completada")
                    onUpdate(toState)
                }
                override fun onAnimationCancel(animation: android.animation.Animator) {
                    Log.d(TAG, "⏸️ Transición cancelada")
                }
                override fun onAnimationRepeat(animation: android.animation.Animator) {}
            })
        }
        
        currentAnimator?.start()
    }
    
    /**
     * Interpolación lineal entre dos valores
     */
    private fun lerp(from: Float, to: Float, progress: Float): Float {
        return from + (to - from) * progress
    }
    
    /**
     * Cancelar animación en progreso
     */
    fun cancelTransition() {
        currentAnimator?.cancel()
        Log.d(TAG, "❌ Animación cancelada")
    }
}
