package com.ivannafusion
import android.util.Log
class OmegaEngineBridge {
    fun connect(): Boolean { Log.d("OmegaBridge", "connect stub"); return true }
    fun disconnect() { Log.d("OmegaBridge", "disconnect stub") }
}

    // ── AI adaptativa ─────────────────────────────────────────────────────────

    /** Activa/desactiva el AGC en el hot path del efecto AudioFlinger. */
    fun setAiEnabled(enabled: Boolean) {
        DSPState.aiEnabled = enabled
        send("SET_AI_ENABLED:${if (enabled) 1 else 0}")
    }

    /**
     * Activa el auto-adapt: el daemon ajusta `intensity` y `bypass` automáticamente
     * en función de la temperatura del dispositivo y la latencia medida.
     */
    fun setAiAutoAdapt(enabled: Boolean) {
        DSPState.aiAutoAdapt = enabled
        send("SET_AI_AUTO_ADAPT:${if (enabled) 1 else 0}")
    }

    /**
     * Sensibilidad del AGC y del auto-adapt.
     * 0 = time-constant lento / ajuste suave.
     * 1 = time-constant rápido / ajuste agresivo.
     */
    fun setAiSensitivity(v: Float) {
        val clamped = v.coerceIn(0f, 1f)
        DSPState.aiSensitivity = clamped
        send("SET_AI_SENSITIVITY:$clamped")
    }
