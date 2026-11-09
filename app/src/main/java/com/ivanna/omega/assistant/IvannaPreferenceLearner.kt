package com.ivanna.omega.assistant

import android.content.Context
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject

/**
 * IvannaPreferenceLearner — auto-refinamiento por aprendizaje de preferencias.
 *
 * Cierra el bucle que IvannaListenerProfile dejaba abierto: los campos
 * preferredSpatialWidth / preferredBassGain existían pero NADIE los escribía
 * (siempre devolvían el default) y preferredMode se aprendía por frecuencia
 * pero NADIE lo aplicaba. Este learner registra los ajustes MANUALES del
 * usuario (sliders de la UI) y, cuando la evidencia es suficiente, PROMUEVE
 * el valor ajustado a default aprendido:
 *
 *   - ≥3 ajustes en la misma dirección (todos por encima o todos por debajo
 *     del default de fábrica, más allá de un epsilon),
 *   - repartidos en ≥2 días distintos (una tarde de pruebas no basta),
 *   - valores agrupados (max-min ≤ 15% del rango del parámetro),
 *   - sin reversión reciente (el último ajuste no volvió al default).
 *
 * El valor promovido es la mediana de los 3 últimos ajustes en esa dirección,
 * acotada al rango seguro del parámetro. La promoción se escribe también en
 * IvannaListenerProfile (preferredSpatialWidth / preferredBassGain) para que
 * los lectores existentes vean el valor aprendido, no el default.
 *
 * Seguridad (fail-closed):
 *   - Solo parámetros del registro SPECS (rango + epsilon conocidos).
 *   - La promoción NUNCA sale del rango [min, max] del parámetro.
 *   - Si el usuario revierte manualmente al default de fábrica, la
 *     promoción se bloquea hasta que haya evidencia nueva (el último ajuste
 *     manda).
 *   - Los ajustes por arrastre (drag) se fusionan: un gesto = un registro
 *     (ventana de coalescencia de 2 s), para no inflar la evidencia.
 *
 * La lógica de decisión (PromotionPolicy.decide) es pura Kotlin sin
 * dependencias Android: unit-testeable sin Robolectric.
 */
class IvannaPreferenceLearner(context: Context) {

    data class ParamSpec(
        val key: String,
        val factoryDefault: Float,
        val min: Float,
        val max: Float,
        val epsilon: Float
    )

    companion object {
        private const val TAG = "IvannaPrefLearner"
        private const val PREFS = "ivanna_preference_learner"
        private const val MIN_EVIDENCE = 3
        private const val MIN_DAYS = 2
        private const val MAX_HISTORY = 20
        private const val COALESCE_MS = 2000L
        private const val CLUSTER_FRACTION = 0.15f

        const val SPATIAL_WIDTH = "spatial_width"
        const val EXCITER_AMOUNT = "exciter_amount"
        const val MASTER_GAIN = "master_gain"
        const val BASS_GAIN_DB = "bass_gain_db"

        /** Registro de parámetros aprendibles. factoryDefault = default de AudioState. */
        val SPECS: Map<String, ParamSpec> = mapOf(
            SPATIAL_WIDTH to ParamSpec(SPATIAL_WIDTH, 1.32f, 0.5f, 2.0f, 0.05f),
            EXCITER_AMOUNT to ParamSpec(EXCITER_AMOUNT, 0.55f, 0.0f, 1.0f, 0.05f),
            MASTER_GAIN to ParamSpec(MASTER_GAIN, 1.08f, 0.1f, 2.0f, 0.05f),
            BASS_GAIN_DB to ParamSpec(BASS_GAIN_DB, 2.2f, -6.0f, 6.0f, 0.5f)
        )
    }

    private val appContext = context.applicationContext
    private val prefs = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** Núcleo puro de la política de promoción (sin Android). */
    object PromotionPolicy {
        data class Adjustment(val value: Float, val dayBucket: Long)
        data class Promotion(val value: Float, val evidence: Int, val days: Int)

        fun decide(
            history: List<Adjustment>,
            factoryDefault: Float,
            min: Float,
            max: Float,
            epsilon: Float
        ): Promotion? {
            if (history.size < MIN_EVIDENCE) return null
            // Reversión reciente al default: el usuario deshizo el ajuste —
            // la promoción queda bloqueada hasta evidencia nueva.
            val last = history.last()
            if (kotlin.math.abs(last.value - factoryDefault) <= epsilon) return null
            val direction = if (last.value > factoryDefault) 1 else -1
            val sameDir = history.filter {
                (it.value - factoryDefault) * direction > epsilon
            }
            if (sameDir.size < MIN_EVIDENCE) return null
            val days = sameDir.map { it.dayBucket }.distinct().size
            if (days < MIN_DAYS) return null
            val recent = sameDir.takeLast(3).map { it.value }
            val span = max - min
            if (recent.max() - recent.min() > CLUSTER_FRACTION * span) return null
            val promoted = recent.sorted()[1].coerceIn(min, max)
            return Promotion(promoted, sameDir.size, days)
        }
    }

    private data class StoredAdjustment(val value: Float, val dayBucket: Long, val timestampMs: Long)

    /**
     * Registra un ajuste MANUAL del usuario. Llamar desde onValueChange de
     * los sliders (la coalescencia fusiona cada gesto de arrastre en un solo
     * registro). Ignora valores fuera de rango y parámetros desconocidos.
     */
    @Synchronized
    fun recordManualAdjustment(param: String, value: Float) {
        val spec = SPECS[param] ?: return
        val clamped = value.coerceIn(spec.min, spec.max)
        val now = System.currentTimeMillis()
        val history = loadHistory(param).toMutableList()
        val last = history.lastOrNull()
        if (last != null && now - last.timestampMs < COALESCE_MS) {
            // Mismo gesto de arrastre: actualizar el valor, no apilar evidencia.
            history[history.lastIndex] = last.copy(value = clamped, timestampMs = now)
        } else {
            history += StoredAdjustment(clamped, dayBucket(now), now)
        }
        saveHistory(param, history.takeLast(MAX_HISTORY))
        // Si la evidencia ya alcanza, la promoción se materializa también en
        // el perfil persistente existente (revive sus campos muertos).
        val promotion = PromotionPolicy.decide(
            history.takeLast(MAX_HISTORY).map { PromotionPolicy.Adjustment(it.value, it.dayBucket) },
            spec.factoryDefault, spec.min, spec.max, spec.epsilon
        )
        if (promotion != null) {
            runCatching {
                val profile = IvannaListenerProfile(appContext)
                when (param) {
                    SPATIAL_WIDTH -> profile.preferredSpatialWidth = promotion.value
                    BASS_GAIN_DB -> profile.preferredBassGain = promotion.value
                }
            }
            Log.i(TAG, "Promoción aprendida: $param → ${promotion.value} (${promotion.evidence} ajustes, ${promotion.days} días)")
        }
    }

    /** Default vigente: el aprendido si hay promoción, si no el de fábrica. */
    @Synchronized
    fun learnedDefault(param: String): Float {
        val spec = SPECS[param] ?: return 0f
        val history = loadHistory(param)
        val promotion = PromotionPolicy.decide(
            history.map { PromotionPolicy.Adjustment(it.value, it.dayBucket) },
            spec.factoryDefault, spec.min, spec.max, spec.epsilon
        )
        return promotion?.value ?: spec.factoryDefault
    }

    /** ¿Hay alguna promoción activa? Resumen legible para diagnóstico y prompt. */
    @Synchronized
    fun promotionReport(): String {
        val parts = SPECS.keys.mapNotNull { key ->
            val spec = SPECS.getValue(key)
            val history = loadHistory(key)
            val promotion = PromotionPolicy.decide(
                history.map { PromotionPolicy.Adjustment(it.value, it.dayBucket) },
                spec.factoryDefault, spec.min, spec.max, spec.epsilon
            )
            promotion?.let { "$key: default aprendido ${"%.2f".format(it.value)} (fábrica ${spec.factoryDefault}, ${it.evidence} ajustes en ${it.days} días)" }
        }
        return if (parts.isEmpty()) "Sin defaults aprendidos todavía."
        else parts.joinToString(" | ")
    }

    /** Historial crudo de un parámetro (para diagnóstico). */
    @Synchronized
    fun adjustmentCount(param: String): Int = loadHistory(param).size

    private fun dayBucket(timestampMs: Long): Long = timestampMs / 86_400_000L

    private fun loadHistory(param: String): List<StoredAdjustment> {
        val raw = prefs.getString("hist_$param", null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            List(arr.length()) { i ->
                val o = arr.getJSONObject(i)
                StoredAdjustment(
                    o.optDouble("v", 0.0).toFloat(),
                    o.optLong("d", 0L),
                    o.optLong("t", 0L)
                )
            }
        }.getOrDefault(emptyList())
    }

    private fun saveHistory(param: String, history: List<StoredAdjustment>) {
        runCatching {
            val arr = JSONArray()
            history.forEach { a ->
                arr.put(JSONObject().apply {
                    put("v", a.value.toDouble())
                    put("d", a.dayBucket)
                    put("t", a.timestampMs)
                })
            }
            prefs.edit().putString("hist_$param", arr.toString()).apply()
        }
    }
}
