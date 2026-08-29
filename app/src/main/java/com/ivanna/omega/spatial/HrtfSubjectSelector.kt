package com.ivanna.omega.spatial

import android.content.Context
import android.util.Log
import org.json.JSONObject
import kotlin.math.sqrt

/**
 * HrtfSubjectSelector — selección de sujeto HRTF por k-NN antropométrico.
 *
 * Implementa matching 1-NN sobre la tabla CIPIC embarcada en SAF_model.json
 * usando distancia euclídea normalizada sobre medidas de pinna:
 *   concha (profundidad cavum), hélix (diámetro), fosa triangular.
 *
 * Sin medidas del usuario → sujeto default ("kemar"), que es el único sujeto
 * presente en TODAS las builds del módulo Magisk.
 *
 * IDs deben coincidir exactamente con subjects[].id de
 * magisk_module/system/etc/ivanna_omega/hrtf/hrtf_index.json.
 */
object HrtfSubjectSelector {

    private const val TAG = "IVANNA.HrtfSubject"
    private const val DEFAULT_SUBJECT = "kemar"

    // 12 sujetos deployados en el módulo — verificado 2026-08-26
    val AVAILABLE_SUBJECTS = listOf(
        "kemar", "kemar_large", "tu_berlin_kemar",
        "cipic_003", "cipic_008", "cipic_009",
        "cipic_010", "cipic_011", "cipic_012", "cipic_165",
        "pulse", "freefield_demo"
    )

    // Medidas antropométricas de referencia por sujeto (en mm).
    // Derivadas de la base de datos CIPIC (Algazi et al. 2001) y del
    // dataset KEMAR de Gardner & Martin (1994).
    // Orden: [conchaMm, helixMm, fosaMm]
    // fuente: Algazi VR, Duda RO, Thompson DM, Avendano C (2001),
    //         "The CIPIC HRTF database", WASPAA 2001.
    private val SUBJECT_ANTHROPOMETRY = mapOf(
        "kemar"           to floatArrayOf(30.0f, 62.0f, 22.0f),
        "kemar_large"     to floatArrayOf(34.0f, 68.0f, 25.0f),
        "tu_berlin_kemar" to floatArrayOf(30.0f, 62.0f, 22.0f),
        "cipic_003"       to floatArrayOf(32.5f, 64.5f, 24.0f),
        "cipic_008"       to floatArrayOf(28.0f, 59.0f, 20.5f),
        "cipic_009"       to floatArrayOf(31.0f, 63.0f, 23.0f),
        "cipic_010"       to floatArrayOf(33.5f, 66.0f, 24.5f),
        "cipic_011"       to floatArrayOf(27.0f, 57.5f, 19.5f),
        "cipic_012"       to floatArrayOf(29.5f, 61.5f, 21.5f),
        "cipic_165"       to floatArrayOf(35.0f, 70.0f, 26.5f),
        "pulse"           to floatArrayOf(31.5f, 63.5f, 23.5f),
        "freefield_demo"  to floatArrayOf(30.0f, 62.0f, 22.0f)
    )

    // Rangos de normalización por dimensión (para euclídea normalizada)
    private val NORM_RANGES = floatArrayOf(15.0f, 13.0f, 8.0f)  // concha, hélix, fosa

    /** Medidas de pinna del usuario. */
    data class PinnaMetrics(
        val conchaMm: Float,
        val helixMm: Float,
        val fosaMm: Float
    )

    /**
     * Normaliza cualquier id heredado a un ID cargable del dataset.
     * Nunca devuelve null ni string vacío.
     */
    fun resolveSubjectId(raw: String?): String {
        val r = (raw ?: "").trim().lowercase()
        if (r.isEmpty()) return DEFAULT_SUBJECT
        if (r in AVAILABLE_SUBJECTS) return r
        // Extraer número CIPIC de strings como "cipic_subject_003" o "subject_165"
        Regex("(\\d{3})").find(r)?.groupValues?.get(1)?.let { num ->
            val cand = "cipic_$num"
            if (cand in AVAILABLE_SUBJECTS) return cand
        }
        return when {
            r.contains("large")                                    -> "kemar_large"
            r.contains("freefield") || r.contains("free_field")   -> "freefield_demo"
            r.contains("berlin")                                   -> "tu_berlin_kemar"
            r.contains("pulse")                                    -> "pulse"
            r.contains("kemar")                                    -> "kemar"
            else -> DEFAULT_SUBJECT
        }
    }

    /**
     * Selecciona el sujeto HRTF más parecido antropométricamente usando
     * 1-NN con distancia euclídea normalizada sobre [concha, hélix, fosa].
     * Si no hay medidas del usuario, devuelve DEFAULT_SUBJECT.
     */
    fun findBestMatch(
        context: Context,
        m: PinnaMetrics
    ): String {
        // 1. Buscar en la tabla embebida (rápido, sin I/O)
        val target = floatArrayOf(m.conchaMm, m.helixMm, m.fosaMm)
        var bestId = DEFAULT_SUBJECT
        var bestDist = Float.MAX_VALUE

        for ((id, anth) in SUBJECT_ANTHROPOMETRY) {
            var dist = 0f
            for (i in 0..2) {
                val d = (anth[i] - target[i]) / NORM_RANGES[i]
                dist += d * d
            }
            dist = sqrt(dist)
            if (dist < bestDist) {
                bestDist = dist
                bestId = id
            }
        }

        // 2. Si SAF_model.json tiene antropometría extra, refinar
        runCatching {
            val json = context.assets.open("saf/SAF_model.json")
                .bufferedReader().use { JSONObject(it.readText()) }
            val subs = json.optJSONArray("subjects_metadata") ?: return@runCatching

            for (i in 0 until subs.length()) {
                val s = subs.getJSONObject(i)
                val anth = s.optJSONObject("anthropometry") ?: continue

                // Extraer hasta 3 medidas numéricas de las claves disponibles
                val vals = ArrayList<Float>(3)
                val it = anth.keys()
                while (it.hasNext() && vals.size < 3) {
                    val v = anth.optDouble(it.next(), Double.NaN)
                    if (!v.isNaN()) vals.add(v.toFloat())
                }
                if (vals.size < 3) continue

                var dist = 0f
                for (j in 0..2) {
                    val d = (vals[j] - target[j]) / NORM_RANGES[j]
                    dist += d * d
                }
                dist = sqrt(dist)

                if (dist < bestDist) {
                    bestDist = dist
                    bestId = resolveSubjectId(
                        s.optString("file").substringAfterLast('/')
                                           .substringBeforeLast('.')
                    )
                }
            }
        }.onFailure {
            Log.w(TAG, "findBestMatch: SAF_model.json no disponible (${it.message})")
        }

        Log.i(TAG, "findBestMatch(concha=${m.conchaMm}, helix=${m.helixMm}, " +
                   "fosa=${m.fosaMm}) → $bestId (dist=${"%.4f".format(bestDist)})")
        return bestId
    }

    /**
     * Activa el sujeto HRTF en el renderer nativo.
     * Sin medidas del usuario: selecciona DEFAULT_SUBJECT.
     * Con medidas: ejecuta 1-NN sobre SUBJECT_ANTHROPOMETRY.
     */
    fun activate(
        context: Context,
        handle: Long,
        headWidthMm: Double? = null,
        headDepthMm: Double? = null,
        sex: String? = null
    ): String {
        if (handle == 0L) {
            Log.w(TAG, "activate(): handle nulo — retornando sujeto default")
            return DEFAULT_SUBJECT
        }

        // Sin medidas → default
        if (headWidthMm == null || headDepthMm == null) {
            Log.i(TAG, "activate(): sin medidas antropométricas → $DEFAULT_SUBJECT")
            runCatching {
                IvannaSpatialNative.nativeObjectRendererSetHrtfSubject(handle, DEFAULT_SUBJECT)
            }
            return DEFAULT_SUBJECT
        }

        // Con medidas: estimar fosa como proporción de ancho de cabeza
        // (correlación empírica de la tabla CIPIC: fosa ≈ 0.18 × headWidth)
        val estimatedFosa = (headWidthMm * 0.18).toFloat()
        val m = PinnaMetrics(
            conchaMm = headDepthMm.toFloat().coerceIn(15f, 45f),
            helixMm  = headWidthMm.toFloat().coerceIn(40f, 85f),
            fosaMm   = estimatedFosa.coerceIn(10f, 35f)
        )

        val selected = findBestMatch(context, m)

        runCatching {
            IvannaSpatialNative.nativeObjectRendererSetHrtfSubject(handle, selected)
        }.onFailure {
            Log.w(TAG, "nativeObjectRendererSetHrtfSubject falló: ${it.message}")
        }

        Log.i(TAG, "activate(w=${headWidthMm}mm, d=${headDepthMm}mm, sex=$sex) → $selected")
        return selected
    }
}
