package com.ivanna.omega.agent

import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import org.json.JSONArray
import org.json.JSONObject

/**
 * AgentApi — interfaz pública para agentes externos (Gemini/ADK u otro
 * orquestador) sobre IVANNA Agent Core.
 *
 * FASE 3 (2026-08-31): capa de consulta/comando segura. Un agente externo
 * puede preguntar el estado acústico, pedir la razón de la última decisión,
 * solicitar optimización o un diagnóstico — pero la EJECUCIÓN crítica sigue
 * siendo local: esta API nunca expone parámetros DSP crudos ni el hilo de
 * audio; solo los canales seguros ya existentes (los mismos que usan los 5
 * agentes internos) y respuestas JSON.
 *
 * Todas las funciones son no-bloqueantes, seguras desde cualquier hilo y
 * nunca lanzan: ante cualquier fallo devuelven un JSON con "ok": false y
 * el motivo, para que un agente externo pueda razonar sobre el error en vez
 * de crashear el proceso de audio.
 */
object AgentApi {

    private fun err(msg: String): JSONObject =
        JSONObject().put("ok", false).put("error", msg)

    /** ¿Cuál es el estado acústico actual? (escena + niveles + salud) */
    fun getAcousticState(): JSONObject = runCatching {
        val s = IvannaAgentCore.state.value
        JSONObject().apply {
            put("ok", true)
            put("agentRunning", s.running)
            put("cycles", s.cycles)
            put("scene", s.perception.scene.name)
            put("rms", s.perception.rms.toDouble())
            put("peak", s.perception.peak.toDouble())
            put("crestDb", s.perception.crestDb.toDouble())
            put("voiceScore", s.perception.voiceScore.toDouble())
            put("gainReductionDb", s.perception.gainReductionDb.toDouble())
            put("playbackActive", s.perception.playbackActive)
            put("activePolicy", s.activePolicy)
            put("clipCountDelta", s.health.clipCountDelta)
            put("roundTripLatencyUs", s.health.roundTripLatencyUs)
            put("adaptiveEngineRunning", s.health.adaptiveEngineRunning)
            put("daemonConnected", s.health.daemonConnected)
        }
    }.getOrElse { err("state unavailable: ${it.message}") }

    /** ¿Por qué cambiaste este perfil? — última decisión con su razón. */
    fun explainLastDecision(): JSONObject = runCatching {
        val last = IvannaAgentCore.recentDecisions().lastOrNull()
            ?: return@runCatching err("sin decisiones registradas todavía")
        JSONObject().apply {
            put("ok", true)
            put("action", last.action)
            put("scene", last.scene.name)
            put("reason", last.reason)
            put("applied", last.applied)
            put("timestampMs", last.timestampMs)
        }
    }.getOrElse { err("explain failed: ${it.message}") }

    /** Historial de decisiones (explicabilidad completa, más reciente al final). */
    fun getDecisionHistory(limit: Int = 16): JSONObject = runCatching {
        val log = IvannaAgentCore.recentDecisions().takeLast(limit.coerceIn(1, 64))
        val arr = JSONArray()
        log.forEach { d ->
            arr.put(JSONObject().apply {
                put("action", d.action)
                put("scene", d.scene.name)
                put("reason", d.reason)
                put("applied", d.applied)
                put("timestampMs", d.timestampMs)
            })
        }
        JSONObject().put("ok", true).put("count", log.size).put("decisions", arr)
    }.getOrElse { err("history failed: ${it.message}") }

    /**
     * Optimiza consumo/salud: fuerza la política de alivio del Optimization
     * Agent si hay degradación real. Devuelve la acción tomada (o "none" si
     * el sistema está sano — no inventa trabajo).
     */
    fun requestOptimization(): JSONObject = runCatching {
        val health = IvannaAgentCore.HealthMonitoringAgent.check()
        val relief = IvannaAgentCore.OptimizationAgent.propose(health)
        if (relief == null) {
            JSONObject().put("ok", true).put("action", "none")
                .put("reason", "sistema sano — sin corrección necesaria")
        } else {
            IvannaAgentCore.DspControlAgent.resetHysteresis()  // permitir reenvío
            val applied = IvannaAgentCore.DspControlAgent.apply(relief)
            JSONObject().apply {
                put("ok", applied)
                put("action", relief.name)
                put("reason", relief.reason)
                put("applied", applied)
            }
        }
    }.getOrElse { err("optimization failed: ${it.message}") }

    /** Diagnostica problemas: salud + rutas activas + estado del motor. */
    fun diagnose(): JSONObject = runCatching {
        val h = IvannaAgentCore.HealthMonitoringAgent.check()
        val issues = JSONArray()
        if (h.clipping) issues.put("clipping sostenido (${h.clipCountDelta} clips/ciclo)")
        if (h.latencyDegraded) issues.put("latencia JNI degradada (${h.roundTripLatencyUs} µs)")
        if (!h.adaptiveEngineRunning && IvannaNativeLib.isLoaded)
            issues.put("motor adaptativo nativo no está corriendo")
        if (!h.daemonConnected)
            issues.put("daemon Magisk desconectado (control system-wide inactivo)")
        if (!IvannaNativeLib.isLoaded) issues.put("librería nativa no cargada")

        JSONObject().apply {
            put("ok", true)
            put("healthy", issues.length() == 0)
            put("issues", issues)
            put("nativeLoaded", IvannaNativeLib.isLoaded)
            put("dspBridgeLoaded", DSPBridge.isLoaded)
            put("spatialReady", IvannaSpatialManager.ready)
            put("hrtfLoaded", IvannaSpatialManager.isHrtfDatasetLoaded())
            put("hrtfSubject", IvannaSpatialManager.currentHrtfSubject())
            put("daemonConnected", h.daemonConnected)
        }
    }.getOrElse { err("diagnose failed: ${it.message}") }
}
