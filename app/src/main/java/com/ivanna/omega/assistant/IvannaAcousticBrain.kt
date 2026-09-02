package com.ivanna.omega.assistant

import android.util.Log
import com.ivanna.omega.agent.IvannaAgentCore
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * IvannaAcousticBrain — capa superior de inteligencia acústica.
 *
 * NO sustituye a IvannaAgentCore (percepción/salud/DSP) ni a
 * IvannaCognitiveCore (razonamiento por intención del usuario). Su función
 * es distinta y no se duplica en ningún otro archivo del proyecto: fusiona
 * en una sola lectura el estado que hoy vive disperso —
 *
 *   percepción       → IvannaAgentCore.state.perception
 *   salud del motor  → IvannaAgentCore.state.health
 *   estado del oyente→ IvannaListenerProfile (fatiga reportada, hábitos)
 *   duración real de escucha → medida aquí mismo, no existía antes
 *
 * — para producir UNA recomendación global explicable, incluso cuando el
 * usuario no ha pedido nada. Por eso IvannaCognitiveCore.proactiveFatigueCheck
 * la consulta: antes esa función solo miraba el contador de reportes de
 * fatiga; ahora también sabe cuánto tiempo lleva sonando algo, que es la
 * señal de fatiga auditiva real (no simulada) que faltaba.
 *
 * IvannaAcousticBrain nunca ejecuta nada por sí misma ni toca el hilo de
 * audio: solo lee IvannaAgentCore.state (ya publicado sin locks por un
 * MutableStateFlow) y devuelve una recomendación que sigue pasando por
 * IvannaCognitiveCore, que conserva la autoridad de decidir si se aplica.
 *
 * Concurrencia: el temporizador de sesión vive en AtomicLong/AtomicBoolean
 * (lectura y escritura sin locks), coherente con la cadencia ~1 Hz a la que
 * IvannaAgentCore publica estado. fuse() no reserva memoria salvo el
 * FusedInsight de salida, que la UI ya necesita.
 */
object IvannaAcousticBrain {

    private const val TAG = "IvannaAcousticBrain"

    /** Referencia ligera de literatura sobre fatiga auditiva por exposición
     *  continua; no es un valor clínico, es el umbral que usa IVANNA para
     *  activar el modo suave de forma proactiva. */
    private const val FATIGUE_WINDOW_MS = 40 * 60 * 1000L

    // ── Sesión de escucha real: arranca cuando hay reproducción activa ──────
    private val sessionStartMs = AtomicLong(0L)
    private val playbackWasActive = AtomicBoolean(false)

    data class FusedInsight(
        val listeningMinutes: Long = 0L,
        val scene: IvannaAgentCore.AcousticScene = IvannaAgentCore.AcousticScene.UNKNOWN,
        val fatigueRisk: Boolean = false,
        val thermalRisk: Boolean = false,
        val clippingRisk: Boolean = false,
        val listenerFatigueReports: Int = 0,
        /** Comando canónico sugerido, o null si no hay nada que aplicar. */
        val recommendation: String? = null,
        /** Frase humana, siempre presente — la usan CognitiveCore y la UI. */
        val explanation: String = "Analizando el entorno acústico…"
    )

    private val _insight = MutableStateFlow(FusedInsight())
    val insight: StateFlow<FusedInsight> = _insight.asStateFlow()

    /**
     * Actualiza el cronómetro de sesión desde el estado real de reproducción.
     * Se llama internamente en cada fuse(); expuesta también por si algún
     * agente externo (AgentApi) quiere alimentar el estado sin pedir una
     * recomendación completa.
     */
    fun trackSession(playbackActive: Boolean) {
        val was = playbackWasActive.getAndSet(playbackActive)
        when {
            playbackActive && !was -> sessionStartMs.set(System.currentTimeMillis())
            !playbackActive -> sessionStartMs.set(0L)
        }
    }

    private fun sessionMinutes(): Long {
        val start = sessionStartMs.get()
        if (start <= 0L) return 0L
        return (System.currentTimeMillis() - start) / 60_000L
    }

    /**
     * Fusiona percepción + salud + perfil del oyente + duración real de la
     * sesión en una sola recomendación explicable. No ejecuta nada: la
     * decisión final sigue siendo de IvannaCognitiveCore.
     */
    fun fuse(profile: IvannaListenerProfile): FusedInsight {
        val s = IvannaAgentCore.state.value
        trackSession(s.perception.playbackActive)
        val minutes = sessionMinutes()

        val fatigueByDuration = s.perception.playbackActive && minutes * 60_000L >= FATIGUE_WINDOW_MS
        val fatigueByProfile  = profile.shouldSuggestGentle
        val thermalRisk       = s.health.thermalLoad >= 0.75f
        val clippingRisk      = s.health.clipping

        val (recommendation, explanation) = when {
            fatigueByDuration -> "gentle_mode" to
                "Reduje la agresividad de agudos porque detecté fatiga auditiva " +
                "después de $minutes minutos de escucha continua."
            fatigueByProfile -> "gentle_mode" to
                "Activo el modo suave: has reportado fatiga ${profile.fatigueReports} veces antes."
            clippingRisk -> "clip-relief" to
                "Hay clipping sostenido en la señal actual — bajo ganancia para proteger el audio."
            thermalRisk -> null to
                "El dispositivo está caliente (${"%.0f".format(s.health.thermalLoad * 100)}%); " +
                "limito la intensidad de los próximos ajustes."
            s.perception.playbackActive -> null to
                "Todo estable: ${s.perception.scene.name.lowercase()}, sin riesgos detectados " +
                "(${minutes} min de sesión)."
            else -> null to "Sin reproducción activa — nada que ajustar."
        }

        val out = FusedInsight(
            listeningMinutes = minutes,
            scene = s.perception.scene,
            fatigueRisk = fatigueByDuration || fatigueByProfile,
            thermalRisk = thermalRisk,
            clippingRisk = clippingRisk,
            listenerFatigueReports = profile.fatigueReports,
            recommendation = recommendation,
            explanation = explanation
        )
        _insight.value = out
        Log.d(TAG, "fuse(): ${out.explanation}")
        return out
    }

    /** Limpia el estado de sesión — invocado por IvannaCognitiveCore.clearDecision(). */
    fun clear() {
        sessionStartMs.set(0L)
        playbackWasActive.set(false)
        _insight.value = FusedInsight(explanation = "Memoria de sesión reiniciada.")
    }
}
