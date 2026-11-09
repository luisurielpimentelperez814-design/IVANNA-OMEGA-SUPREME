package com.ivanna.omega.agent

import android.content.Context
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject

/**
 * IvannaDecisionAuthority — AUTONOMÍA CON DECISIÓN: qué decide IVANNA sola,
 * qué necesita al usuario y qué tiene prohibido.
 *
 * El DecisionAgent ya decidía y aplicaba políticas de forma autónoma, pero
 * sin matriz de autoridad explícita, sin log persistido y sin forma de que
 * el usuario vetara una decisión que deshizo (el agente la volvía a aplicar
 * en el siguiente ciclo). Esta capa lo formaliza y lo hace auditable:
 *
 *   AUTÓNOMO (bajo riesgo, reversible, acotado): IVANNA DECIDE Y ACTÚA
 *   sola, sin esperar al usuario. Cada decisión queda registrada con sus
 *   parámetros y su motivo.
 *     - policy_apply: política del DecisionAgent DENTRO de rangos seguros
 *       (targetGain 0.5..1.0, compAmount 0..1, excReduction 0..1,
 *       spatialWidth 0.5..2.0). Fuera de rango → DENEGADO aunque sea autónomo.
 *     - optimize / diagnose: optimización y diagnóstico (sin cambios de audio
 *       destructivos).
 *     - repair_*: reparaciones de SelfHealingAgent (daemon, ADE, clip, DSP,
 *       térmica, guía de captura). Son acciones de salud, no de gusto.
 *     - preference_promotion: defaults aprendidos (acotados al rango del
 *       parámetro, con evidencia de días).
 *     - preset_apply: comandos DSP de la whitelist validada.
 *     - evolution_step: pasos de BehaviorEvolution (un paso, acotado).
 *
 *   REQUIERE CONFIRMACIÓN (riesgo medio/alto o difícil de revertir): IVANNA
 *   PROPONE y espera al usuario. Nunca se ejecuta solo.
 *     - clear_memory / clear_profile: borrar memoria o perfil del oyente.
 *     - disable_safety_limiter / disable_thermal_protection: bajar guardas.
 *     - change_hrtf_subject: cambiar el sujeto HRTF.
 *     - module_operation: instalar/quitar/recargar el módulo Magisk.
 *     - delete_preset: borrar un preset guardado.
 *
 *   PROHIBIDO (nunca, ni con confirmación vía agente): reescribir código,
 *   desactivar IntegrityGate/anti-tamper, acceder a credenciales, enviar
 *   datos fuera del dispositivo, compras/pagos, overclock por encima de la
 *   tabla del kernel, desactivar el throttling térmico.
 *
 * REVERSIÓN Y VETO: si el usuario deshace manualmente una decisión autónoma
 * (mueve el slider de vuelta, revierte un preset), markUndone() registra el
 * veto: la misma decisión no se vuelve a aplicar sola durante VETO_DAYS.
 * La excepción es repair_clip_gain_cut (protección auditiva ante clipping):
 * la seguridad no se veta; el usuario la desactiva desde los ajustes, no
 * deshaciendo la acción.
 *
 * El log persiste en SharedPreferences (máx. 100 decisiones) para que
 * sobreviva a reinicios y alimente el prompt de Gemini (salud del sistema).
 */
object IvannaDecisionAuthority {

    private const val TAG = "IvannaDecisionAuth"
    private const val PREFS = "ivanna_decision_authority"
    private const val MAX_LOG = 100
    private const val VETO_DAYS = 3L
    private const val VETO_MS = VETO_DAYS * 24 * 60 * 60 * 1000L

    enum class Tier { AUTONOMOUS, NEEDS_CONFIRMATION, FORBIDDEN }

    enum class Verdict { ALLOWED, NEEDS_USER, DENIED }

    data class Authorization(val verdict: Verdict, val reason: String)

    data class DecisionRecord(
        val timestampMs: Long,
        val action: String,
        val tier: Tier,
        val params: Map<String, Float>,
        val applied: Boolean,
        val reason: String,
        val undone: Boolean = false
    )

    // ── Matriz de autoridad ──────────────────────────────────────────────────

    private val TIERS: Map<String, Tier> = mapOf(
        "policy_apply" to Tier.AUTONOMOUS,
        "optimize" to Tier.AUTONOMOUS,
        "diagnose" to Tier.AUTONOMOUS,
        "repair_daemon_reconnect" to Tier.AUTONOMOUS,
        "repair_ade_restart" to Tier.AUTONOMOUS,
        "repair_clip_gain_cut" to Tier.AUTONOMOUS,
        "repair_dsp_reinit" to Tier.AUTONOMOUS,
        "repair_thermal_mitigation" to Tier.AUTONOMOUS,
        "repair_capture_guidance" to Tier.AUTONOMOUS,
        "preference_promotion" to Tier.AUTONOMOUS,
        "preset_apply" to Tier.AUTONOMOUS,
        "evolution_step" to Tier.AUTONOMOUS,
        "clear_memory" to Tier.NEEDS_CONFIRMATION,
        "clear_profile" to Tier.NEEDS_CONFIRMATION,
        "disable_safety_limiter" to Tier.NEEDS_CONFIRMATION,
        "disable_thermal_protection" to Tier.NEEDS_CONFIRMATION,
        "change_hrtf_subject" to Tier.NEEDS_CONFIRMATION,
        "module_operation" to Tier.NEEDS_CONFIRMATION,
        "delete_preset" to Tier.NEEDS_CONFIRMATION,
        "rewrite_code" to Tier.FORBIDDEN,
        "disable_integrity_gate" to Tier.FORBIDDEN,
        "access_credentials" to Tier.FORBIDDEN,
        "send_data_offdevice" to Tier.FORBIDDEN,
        "purchase" to Tier.FORBIDDEN,
        "overclock_beyond_kernel" to Tier.FORBIDDEN,
        "disable_thermal_throttle" to Tier.FORBIDDEN
    )

    /** Rangos seguros por parámetro para policy_apply (fail-closed). */
    private val PARAM_BOUNDS: Map<String, ClosedFloatingPointRange<Float>> = mapOf(
        "targetGain" to 0.5f..1.0f,
        "compAmount" to 0.0f..1.0f,
        "excReduction" to 0.0f..1.0f,
        "spatialWidth" to 0.5f..2.0f
    )

    /** Acciones de seguridad que el veto del usuario NO puede bloquear. */
    private val UNVETOABLE = setOf("repair_clip_gain_cut")

    private var prefs: android.content.SharedPreferences? = null

    @Synchronized
    fun init(context: Context) {
        if (prefs == null) {
            prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        }
    }

    fun tierOf(action: String): Tier = TIERS[action] ?: Tier.NEEDS_CONFIRMATION

    /**
     * Veredicto antes de ejecutar. Orden de chequeo:
     *   1. Acción desconocida → NEEDS_USER (no se inventa autoridad).
     *   2. FORBIDDEN → DENIED siempre.
     *   3. Parámetros fuera de rango → DENIED (fail-closed).
     *   4. Veto vigente del usuario → NEEDS_USER (no re-aplicar lo deshecho).
     *   5. AUTONOMOUS → ALLOWED. NEEDS_CONFIRMATION → NEEDS_USER.
     */
    @Synchronized
    fun authorize(action: String, params: Map<String, Float> = emptyMap()): Authorization {
        val tier = TIERS[action] ?: return Authorization(
            Verdict.NEEDS_USER, "acción '$action' sin tier declarado — sin autoridad autónoma"
        )
        if (tier == Tier.FORBIDDEN) return Authorization(
            Verdict.DENIED, "acción '$action' prohibida para la capa agéntica"
        )
        for ((name, value) in params) {
            val bound = PARAM_BOUNDS[name] ?: continue
            if (value !in bound) return Authorization(
                Verdict.DENIED, "parámetro '$name'=$value fuera de rango seguro $bound"
            )
        }
        if (action !in UNVETOABLE && isVetoed(action, params)) return Authorization(
            Verdict.NEEDS_USER, "el usuario deshizo esta decisión hace menos de $VETO_DAYS días — no re-aplicar sola"
        )
        return when (tier) {
            Tier.AUTONOMOUS -> Authorization(Verdict.ALLOWED, "autónomo: $action")
            Tier.NEEDS_CONFIRMATION -> Authorization(Verdict.NEEDS_USER, "'$action' requiere confirmación del usuario")
            Tier.FORBIDDEN -> Authorization(Verdict.DENIED, "prohibido")
        }
    }

    /** Registra una decisión tomada (aplicada o no) con su motivo. */
    @Synchronized
    fun recordDecision(action: String, params: Map<String, Float>, applied: Boolean, reason: String) {
        val p = prefs ?: return
        val records = loadLog(p).toMutableList()
        records += DecisionRecord(
            System.currentTimeMillis(), action, tierOf(action), params, applied, reason
        )
        saveLog(p, records.takeLast(MAX_LOG))
        Log.i(TAG, "Decisión [$action] aplicada=$applied — $reason")
    }

    /**
     * El usuario deshizo manualmente una decisión autónoma: se registra el
     * veto para no volver a aplicarla sola durante VETO_DAYS. La firma es
     * acción + parámetros redondeados (misma decisión, mismos valores).
     */
    @Synchronized
    fun markUndone(action: String, params: Map<String, Float> = emptyMap()) {
        val p = prefs ?: return
        val signature = signatureOf(action, params)
        p.edit().putLong("veto_$signature", System.currentTimeMillis()).apply()
        val records = loadLog(p).toMutableList()
        val idx = records.indexOfLast { it.action == action && !it.undone }
        if (idx >= 0) records[idx] = records[idx].copy(undone = true)
        saveLog(p, records.takeLast(MAX_LOG))
        Log.i(TAG, "Veto registrado: '$action' no se re-aplicará sola durante $VETO_DAYS días")
    }

    /** ¿Hay un veto vigente para esta decisión exacta? */
    @Synchronized
    fun isVetoed(action: String, params: Map<String, Float> = emptyMap()): Boolean {
        val p = prefs ?: return false
        val at = p.getLong("veto_${signatureOf(action, params)}", 0L)
        return at > 0L && System.currentTimeMillis() - at < VETO_MS
    }

    /** Últimas decisiones (para diagnóstico y para el prompt de Gemini). */
    @Synchronized
    fun recentDecisions(limit: Int = 8): List<DecisionRecord> {
        val p = prefs ?: return emptyList()
        return loadLog(p).takeLast(limit)
    }

    /** Resumen legible de las últimas decisiones autónomas. */
    @Synchronized
    fun report(): String {
        val recent = recentDecisions(5)
        if (recent.isEmpty()) return "Sin decisiones autónomas registradas todavía."
        return recent.joinToString(" | ") { r ->
            "${r.action}(${if (r.applied) "aplicada" else "no aplicada"}${if (r.undone) ", deshecha por el usuario" else ""}): ${r.reason}"
        }
    }

    private fun signatureOf(action: String, params: Map<String, Float>): String {
        val paramPart = params.entries.sortedBy { it.key }
            .joinToString(",") { "${it.key}=${"%.2f".format(it.value)}" }
        return "$action|$paramPart".hashCode().toString(16)
    }

    private fun loadLog(p: android.content.SharedPreferences): List<DecisionRecord> {
        val raw = p.getString("decision_log", null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            List(arr.length()) { i ->
                val o = arr.getJSONObject(i)
                val params = mutableMapOf<String, Float>()
                val pj = o.optJSONObject("params")
                if (pj != null) {
                    val keys = pj.keys()
                    while (keys.hasNext()) {
                        val k = keys.next()
                        params[k] = pj.optDouble(k, 0.0).toFloat()
                    }
                }
                DecisionRecord(
                    o.optLong("t", 0L),
                    o.optString("action", "?"),
                    Tier.valueOf(o.optString("tier", "NEEDS_CONFIRMATION")),
                    params,
                    o.optBoolean("applied", false),
                    o.optString("reason", ""),
                    o.optBoolean("undone", false)
                )
            }
        }.getOrDefault(emptyList())
    }

    private fun saveLog(p: android.content.SharedPreferences, records: List<DecisionRecord>) {
        runCatching {
            val arr = JSONArray()
            records.forEach { r ->
                arr.put(JSONObject().apply {
                    put("t", r.timestampMs)
                    put("action", r.action)
                    put("tier", r.tier.name)
                    put("params", JSONObject().apply {
                        r.params.forEach { (k, v) -> put(k, v.toDouble()) }
                    })
                    put("applied", r.applied)
                    put("reason", r.reason)
                    put("undone", r.undone)
                })
            }
            p.edit().putString("decision_log", arr.toString()).apply()
        }
    }
}
