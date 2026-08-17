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

            // FIX quirúrgico (audit 2026-07-28): antes safeCompressor() sólo se
            // disparaba cuando cambiaba threshold O ratio — si el usuario movía
            // Attack o Release en AdaptiveEngineScreen, el delta ni se registraba.
            // nativeSetCompressorParams SÍ recibe attackMs/releaseMs
            // (IvannaNativeLib.kt:117, firma de 4 args viva desde v3.0).
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
            if (newState.compressorAttack != previousState.compressorAttack) {
                deltas["attack"] = newState.compressorAttack
                safeCompressor()
            }
            if (newState.compressorRelease != previousState.compressorRelease) {
                deltas["release"] = newState.compressorRelease
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
                com.ivanna.omega.audio.OmegaMetrics.updateSharedLevels(spatialWidth = newState.spatialWidth)
            }

            if (newState.eqBass != previousState.eqBass ||
                newState.eqMid != previousState.eqMid ||
                newState.eqTreble != previousState.eqTreble ||
                newState.masterGain != previousState.masterGain) {
                deltas["eq"] = newState.eqBass + newState.eqMid + newState.eqTreble
                // FIX: antes el delta "eq" se contabilizaba y logueaba pero
                // NUNCA se enviaba al motor nativo desde esta ruta. La ruta
                // primaria (AdaptiveBackend.applyEQ) sí lo envía, pero
                // requestUpdate() debe ser autosuficiente: cualquier caller
                // que empuje un AudioState nuevo por aquí debe tener EQ real,
                // no sólo un log. Firma real: IvannaNativeLib.kt:42.
                // También incluye masterGain, que antes ni disparaba delta.
                runCatching {
                    IvannaNativeLib.nativeSetEQParams(
                        newState.eqBass.coerceIn(-18f, 18f),
                        newState.eqMid.coerceIn(-18f, 18f),
                        newState.eqTreble.coerceIn(-18f, 18f),
                        newState.masterGain.coerceIn(0.1f, 2f)
                    )
                }.onFailure { Log.w(TAG, "eq native: ${it.message}") }
            }

            if (newState.voiceProtectionEnabled != previousState.voiceProtectionEnabled) {
                deltas["voice_protection"] = if (newState.voiceProtectionEnabled) 1f else 0f
                // Nota: la ruta real de voz es VoiceProtectionManager.applyToEngine()
                // — este delta es sólo informativo para el log, no dispara C++.
            }

            if (deltas.isNotEmpty()) {
                Log.d(TAG, "📡 Delta update enviado: ${deltas.keys.joinToString(", ")}")
            }

            lastState = newState
        } catch (t: Throwable) {
            Log.e(TAG, "❌ Error en applyUpdate", t)
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
    
    val lastSpatialWidth: Float
        get() = lastState?.spatialWidth ?: 0f
}
