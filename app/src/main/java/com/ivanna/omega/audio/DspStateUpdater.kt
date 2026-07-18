// DspStateUpdater.kt
// ============================================================================
// DELTA UPDATES + DEBOUNCE — Solo envía parámetros que cambiaron
// Sincroniza con ciclo de audio (50ms)
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import android.os.Handler
import android.os.Looper
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

class DspStateUpdater {
    private val handler = Handler(Looper.getMainLooper())
    private var updateRunnable: Runnable? = null
    private var lastState: AudioState? = null
    
    companion object {
        private const val TAG = "DspStateUpdater"
        private const val UPDATE_INTERVAL_MS = 50L  // Sincronizado con audio @ 50ms
        private const val DEBOUNCE_DELAY_MS = 24L
    }
    
    /**
     * Solicitar actualización de DSP (con debounce y delta)
     */
    fun requestUpdate(newState: AudioState) {
        // Cancelar update anterior si existe
        updateRunnable?.let { handler.removeCallbacks(it) }
        
        // Programar nuevo update
        updateRunnable = Runnable {
            applyUpdate(newState)
        }
        handler.postDelayed(updateRunnable!!, DEBOUNCE_DELAY_MS)
    }
    
    /**
     * Aplicar actualización (solo parámetros que cambiaron)
     */
    private fun applyUpdate(newState: AudioState) {
        val lastState = lastState ?: AudioState()
        
        try {
            // Calcular deltas
            val deltas = mutableMapOf<String, Float>()
            
            if (newState.compressorThreshold != lastState.compressorThreshold) {
                deltas["threshold"] = newState.compressorThreshold
                IvannaNativeLib.nativeSetCompressorParams(
                    newState.compressorThreshold,
                    newState.compressorRatio
                )
            }
            
            if (newState.compressorRatio != lastState.compressorRatio) {
                deltas["ratio"] = newState.compressorRatio
                IvannaNativeLib.nativeSetCompressorParams(
                    newState.compressorThreshold,
                    newState.compressorRatio
                )
            }
            
            if (newState.exciterAmount != lastState.exciterAmount) {
                deltas["exciter"] = newState.exciterAmount
                IvannaNativeLib.nativeSetHarmonicGain(newState.exciterAmount)
            }
            
            if (newState.spatialWidth != lastState.spatialWidth) {
                deltas["spatial_width"] = newState.spatialWidth
                IvannaNativeLib.nativeSetSpatialWidthDirect(newState.spatialWidth)
            }
            
            if (newState.eqBass != lastState.eqBass ||
                newState.eqMid != lastState.eqMid ||
                newState.eqTreble != lastState.eqTreble) {
                // Aplicar EQ (si existe método nativo)
                // IvannaNativeLib.nativeSetEQ(newState.eqBass, newState.eqMid, newState.eqTreble)
                deltas["eq"] = newState.eqBass + newState.eqMid + newState.eqTreble
            }
            
            if (newState.voiceProtectionEnabled != lastState.voiceProtectionEnabled) {
                deltas["voice_protection"] = if (newState.voiceProtectionEnabled) 1f else 0f
                // Aplicar voice protection
            }
            
            // Log de delta updates
            if (deltas.isNotEmpty()) {
                Log.d(TAG, "📡 Delta update enviado: ${deltas.keys.joinToString(", ")}")
            }
            
            lastState = newState
            
        } catch (e: Exception) {
            Log.e(TAG, "❌ Error en applyUpdate", e)
        }
    }
    
    /**
     * Forzar actualización sin debounce (para cambios críticos)
     */
    fun forceUpdate(newState: AudioState) {
        updateRunnable?.let { handler.removeCallbacks(it) }
        applyUpdate(newState)
    }
    
    /**
     * Obtener último estado aplicado
     */
    fun getLastAppliedState(): AudioState? = lastState
}
