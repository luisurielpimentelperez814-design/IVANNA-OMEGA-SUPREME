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
        val previousState = lastState ?: AudioState()
        if (!IvannaNativeLib.isLoaded) {
            lastState = newState
            return
        }
        try {
            val deltas = mutableMapOf<String, Float>()

            fun safeCompressor() = runCatching {
                IvannaNativeLib.nativeSetCompressorParams(
                    newState.compressorThreshold.coerceIn(-60f, 0f),
                    newState.compressorRatio.coerceIn(1f, 20f),
                    newState.compressorAttack.coerceIn(0.1f, 500f),
                    newState.compressorRelease.coerceIn(1f, 2000f)
                )
            }.onFailure { Log.w(TAG, "comp native: ${it.message}") }

            if (newState.compressorThreshold != previousState.compressorThreshold) {
                deltas["threshold"] = newState.compressorThreshold
                safeCompressor()
            }
            if (newState.compressorRatio != previousState.compressorRatio) {
                deltas["ratio"] = newState.compressorRatio
                safeCompressor()
            }

            if (newState.exciterAmount != previousState.exciterAmount) {
                deltas["exciter"] = newState.exciterAmount
                runCatching {
                    IvannaNativeLib.nativeSetHarmonicGain(
                        newState.exciterAmount.coerceIn(0f, 1f)
                    )
                }.onFailure { Log.w(TAG, "harmonic native: ${it.message}") }
            }

            if (newState.spatialWidth != previousState.spatialWidth) {
                deltas["spatial_width"] = newState.spatialWidth
                runCatching {
                    IvannaNativeLib.nativeSetSpatialWidthDirect(
                        newState.spatialWidth.coerceIn(0f, 2f)
                    )
                }.onFailure { Log.w(TAG, "spatial native: ${it.message}") }
            }

            if (newState.eqBass != previousState.eqBass ||
                newState.eqMid != previousState.eqMid ||
                newState.eqTreble != previousState.eqTreble) {
                deltas["eq"] = newState.eqBass + newState.eqMid + newState.eqTreble
            }

            if (newState.voiceProtectionEnabled != previousState.voiceProtectionEnabled) {
                deltas["voice_protection"] = if (newState.voiceProtectionEnabled) 1f else 0f
            }

            if (deltas.isNotEmpty()) {
                Log.d(TAG, "📡 Delta update enviado: ${deltas.keys.joinToString(", ")}")
            }

            lastState = newState
        } catch (t: Throwable) {
            Log.e(TAG, "❌ Error en applyUpdate", t)
        }
    }

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
