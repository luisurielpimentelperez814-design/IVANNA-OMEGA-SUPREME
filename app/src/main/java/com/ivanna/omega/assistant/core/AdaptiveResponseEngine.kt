package com.ivanna.omega.assistant.core

import android.util.Log

/**
 * AdaptiveResponseEngine — Analiza complejidad de query y selecciona perfil de respuesta.
 *
 * Perfiles:
 *   FAST: respuestas cortas, <256 tokens
 *   NORMAL: respuestas estándar, <1024 tokens
 *   DEEP_REASONING: razonamiento profundo, <4096 tokens
 *   ENGINEERING_MODE: código/técnico extenso, <8192 tokens
 */
class AdaptiveResponseEngine {
    companion object { private const val TAG = "AdaptiveResponseEngine" }

    enum class ResponseProfile { FAST, NORMAL, DEEP_REASONING, ENGINEERING_MODE }

    fun analyzeComplexity(query: String, uiContext: String = ""): ResponseProfile {
        val q = query.lowercase()

        // Señales de ENGINEERING_MODE
        val engineeringSignals = listOf("código", "code", "implementa", "debug", "error log", "stack trace", "cmake", "gradle", "compila", "build", "fix", "arregla", "corrige", "optimiza algoritmo", "complejidad O", "kotlin", "cpp", "jni", "native")
        if (engineeringSignals.any { q.contains(it) }) {
            Log.d(TAG, "Selected ENGINEERING_MODE for: ${query.take(40)}")
            return ResponseProfile.ENGINEERING_MODE
        }

        // Señales de DEEP_REASONING
        val deepSignals = listOf("explica detalladamente", "por qué", "porque", "razón", "análisis", "compara", "diferencia entre", "ventajas y desventajas", "pros y contras", "diagnóstico", "investiga", "evalúa")
        if (deepSignals.any { q.contains(it) } || query.length > 150) {
            Log.d(TAG, "Selected DEEP_REASONING for: ${query.take(40)}")
            return ResponseProfile.DEEP_REASONING
        }

        // Señales de FAST
        val fastSignals = listOf("hola", "hey", "ok", "sí", "no", "gracias", "adiós", "qué hora", "qué día", "estado", "clima", "volume up", "volume down", "más alto", "más bajo", "silencio", "mute")
        if (fastSignals.any { q.contains(it) } && query.length < 50) {
            Log.d(TAG, "Selected FAST for: ${query.take(40)}")
            return ResponseProfile.FAST
        }

        Log.d(TAG, "Selected NORMAL for: ${query.take(40)}")
        return ResponseProfile.NORMAL
    }

    fun getMaxTokens(profile: ResponseProfile): Int = when (profile) {
        ResponseProfile.FAST -> 256
        ResponseProfile.NORMAL -> 1024
        ResponseProfile.DEEP_REASONING -> 4096
        ResponseProfile.ENGINEERING_MODE -> 8192
    }

    fun getTemperature(profile: ResponseProfile): Float = when (profile) {
        ResponseProfile.FAST -> 0.3f
        ResponseProfile.NORMAL -> 0.7f
        ResponseProfile.DEEP_REASONING -> 0.5f
        ResponseProfile.ENGINEERING_MODE -> 0.2f
    }
}
