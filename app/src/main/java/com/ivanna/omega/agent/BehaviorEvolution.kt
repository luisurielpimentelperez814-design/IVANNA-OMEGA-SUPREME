package com.ivanna.omega.agent

import android.content.Context
import android.util.Log

/**
 * BehaviorEvolution — auto-evolución ACOTADA y segura del COMPORTAMIENTO.
 *
 * La app NUNCA reescribe su propio código. Lo que evoluciona son PARÁMETROS
 * de comportamiento (umbrales, pisos de seguridad) a partir de los datos
 * acumulados por SelfHealingAgent, con estas garantías:
 *
 *   - Registro cerrado: solo los parámetros declarados en SPECS pueden
 *     moverse; cualquier otro es inmutable por diseño.
 *   - Movimientos acotados: cada parámetro tiene [min, max] y un paso fijo;
 *     un solo evento mueve como mucho un paso.
 *   - Dirección única por señal: el fracaso repetido de una reparación solo
 *     puede mover el parámetro hacia el lado MÁS PROTECTOR; el éxito
 *     repetido solo lo devuelve hacia el default (nunca más allá).
 *   - Reversión por inactividad: si no hay eventos del parámetro en 7 días,
 *     vuelve solo a su default (el entorno cambió; la adaptación caduca).
 *   - Persistencia en SharedPreferences; si no está inicializado o algo
 *     falla, get() devuelve el default (fail-closed).
 *
 * Parámetro inaugural: clip_gain_floor — el piso de ganancia de emergencia
 * que SelfHealingAgent aplica ante clipping sostenido (default 0.65).
 * Si el corte a 0.65 no frena los clips dos veces seguidas (verificación
 * post-reparación), el piso baja a 0.60, luego 0.55 (mínimo). Si las
 * reparaciones vuelven a tener éxito, sube de vuelta hacia 0.65.
 */
object BehaviorEvolution {

    private const val TAG = "BehaviorEvolution"
    private const val PREFS = "ivanna_behavior_evolution"
    private const val QUIET_REVERT_MS = 7L * 24 * 60 * 60 * 1000 // 7 días

    data class Spec(
        val key: String,
        val default: Float,
        val min: Float,
        val max: Float,
        val step: Float
    )

    const val CLIP_GAIN_FLOOR = "clip_gain_floor"

    val SPECS: Map<String, Spec> = mapOf(
        CLIP_GAIN_FLOOR to Spec(CLIP_GAIN_FLOOR, 0.65f, 0.55f, 0.70f, 0.05f)
    )

    private var prefs: android.content.SharedPreferences? = null

    /** Llamar una vez al arrancar (SelfHealingAgent.start lo hace). */
    @Synchronized
    fun init(context: Context) {
        if (prefs == null) {
            prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        }
    }

    /** Valor vigente; default si no está inicializado o no hay adaptación. */
    @Synchronized
    fun get(key: String): Float {
        val spec = SPECS[key] ?: return 0f
        val p = prefs ?: return spec.default
        val lastEvent = p.getLong("${key}_last_event_ms", 0L)
        if (lastEvent > 0L && System.currentTimeMillis() - lastEvent > QUIET_REVERT_MS) {
            // Sin eventos en 7 días: la adaptación caduca, vuelve al default.
            p.edit().putFloat(key, spec.default).putLong("${key}_last_event_ms", 0L).apply()
            Log.i(TAG, "$key sin eventos 7 días → revertido a default ${spec.default}")
            return spec.default
        }
        return p.getFloat(key, spec.default).coerceIn(spec.min, spec.max)
    }

    /**
     * La reparación asociada tuvo éxito: un paso de vuelta hacia el default
     * (solo si está por debajo de él; nunca se mueve hacia el lado arriesgado).
     */
    @Synchronized
    fun adaptOnSuccess(key: String) {
        val spec = SPECS[key] ?: return
        val p = prefs ?: return
        val current = get(key)
        if (current >= spec.default) return
        val next = (current + spec.step).coerceAtMost(spec.default)
        p.edit().putFloat(key, next).putLong("${key}_last_event_ms", System.currentTimeMillis()).apply()
        Log.i(TAG, "$key éxito repetido → $next (hacia default ${spec.default})")
    }

    /**
     * La reparación asociada fracasó repetidamente pese a aplicarse: un paso
     * hacia el lado más protector (piso más bajo = más protección).
     */
    @Synchronized
    fun adaptOnRepeatedFailure(key: String) {
        val spec = SPECS[key] ?: return
        val p = prefs ?: return
        val current = get(key)
        if (current <= spec.min) return
        // Dirección protectora: para clip_gain_floor, menor = más protección.
        val next = (current - spec.step).coerceAtLeast(spec.min)
        p.edit().putFloat(key, next).putLong("${key}_last_event_ms", System.currentTimeMillis()).apply()
        Log.i(TAG, "$key fracaso repetido → $next (más protector; mínimo ${spec.min})")
    }

    /** Resumen legible para diagnóstico y para el prompt de Gemini. */
    @Synchronized
    fun report(): String {
        val parts = SPECS.values.map { spec ->
            val current = get(spec.key)
            if (current != spec.default) "${spec.key}: ${"%.2f".format(current)} (default ${spec.default})"
            else "${spec.key}: ${spec.default} (default)"
        }
        return parts.joinToString(" | ")
    }
}
