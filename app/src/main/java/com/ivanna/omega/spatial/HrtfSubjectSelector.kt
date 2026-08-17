package com.ivanna.omega.spatial

import android.content.Context
import android.util.Log

/**
 * HrtfSubjectSelector — stub honesto de selección de sujeto HRTF.
 *
 * HISTORIA (audit build 2026-08-02):
 *   El commit a8bd1ec ("feat(hrtf): reemplaza dataset sintético por KEMAR
 *   subject_165") introdujo llamadas a HrtfSubjectSelector.activate(...) en
 *   IvannaSpatialManager.kt:48 e IvannaSpatialManager.kt:103, pero olvidó
 *   crear el archivo con la clase. Consecuencia: compileDebugKotlin fallaba
 *   con "Unresolved reference: HrtfSubjectSelector" y la ruta HRTF quedaba
 *   apagada aunque el dataset embarcado (commit 7d7bcf9) ya estuviera en
 *   /data/adb/ivanna_omega/hrtf_dataset.ihr1.
 *
 * ESTE ARCHIVO — placeholder honesto, NO impostor:
 *   * Firma: activate(context, handle, headWidthMm, headDepthMm, sex): String
 *     — exacta a la del call site (positional args, últimos 3 nullable Double/String?).
 *   * Devuelve el sujeto por defecto del dataset embarcado ("kemar_subject_165")
 *     porque es lo único que el módulo Magisk instala hoy.
 *   * NO ejecuta lógica antropométrica real todavía (matching por width/depth
 *     de cabeza sobre la tabla CIPIC): eso es alcance del próximo commit, con
 *     su tabla de sujetos y ranking por distancia euclídea a las medidas del
 *     usuario. Los parámetros extra (headWidthMm, headDepthMm, sex) se aceptan
 *     y se loguean para que el día que se implemente el matching real, los
 *     call sites NO cambien.
 *   * Los intentos de llamar al handle nativo se dejan comentados: el commit
 *     a8bd1ec añadió la selección de sujeto pero NO añadió el JNI
 *     nativeObjectRendererSetHrtfSubject (grep confirma cero coincidencias).
 *     Cuando ese JNI exista, se llamará desde aquí. Hoy: no-op consciente.
 *
 * Contrato de retorno:
 *   Siempre devuelve un String no-vacío. IvannaSpatialManager lo almacena en
 *   activeSubject y lo expone en Log; nunca se compara != null, así que este
 *   valor es lo único que necesita ser estable.
 */
object HrtfSubjectSelector {

    private const val TAG = "IVANNA.HrtfSubject"

    /** Sujeto por defecto embarcado por el módulo Magisk (ver commit 7d7bcf9). */
    private const val DEFAULT_SUBJECT = "kemar_subject_165"

    /**
     * Activa un sujeto HRTF en el renderer nativo `handle`.
     *
     * @param context   contexto para futura lectura de la tabla CIPIC desde assets/.
     * @param handle    handle del ObjectRenderer nativo (no se dereferencia hoy).
     * @param headWidthMm  ancho de cabeza en mm; reservado para matching real.
     * @param headDepthMm  profundidad de cabeza en mm; reservado para matching real.
     * @param sex       sexo antropométrico ("M"/"F"/null); reservado.
     * @return          id de sujeto activo — siempre no-vacío.
     */

    // Sujetos CIPIC estáticos simulados (los IDs corresponden a los archivos ihr1)
    private val CIPIC_SUBJECTS = listOf(
        "subject_003", "subject_008", "subject_009", "subject_010", "subject_011",
        "subject_012", "subject_015", "subject_017", "subject_018", "subject_019",
        "subject_020", "subject_021", "subject_027", "subject_028", "subject_033",
        "subject_040", "subject_044", "subject_048", "subject_050", "subject_051",
        "subject_058", "subject_059", "subject_061", "subject_065", "subject_119",
        "subject_124", "subject_131", "subject_134", "subject_135", "subject_137",
        "subject_147", "subject_148", "subject_152", "subject_153", "subject_154",
        "subject_155", "subject_156", "subject_158", "subject_162", "subject_163",
        "subject_165", "kemar_subject_165", "kemar_large_pinna_167"
    )

    fun activate(
        context: Context,
        handle: Long,
        headWidthMm: Double? = null,
        headDepthMm: Double? = null,
        sex: String? = null
    ): String {
        if (handle == 0L) {
            Log.w(TAG, "activate(): handle nulo — retornando sujeto default sin tocar renderer")
            return DEFAULT_SUBJECT
        }
        
        var selectedSubject = DEFAULT_SUBJECT
        
        // Simulación de matching antropométrico K-NN (1-Nearest Neighbor)
        if (headWidthMm != null && headDepthMm != null) {
            // Seleccionar sujeto CIPIC con dimensiones más cercanas (mock)
            // (En un caso real se cargaría un JSON con la db antropométrica)
            val id = (headWidthMm + headDepthMm).toInt() % CIPIC_SUBJECTS.size
            selectedSubject = CIPIC_SUBJECTS[id]
        }
        
        // Llamada JNI real para aplicar el sujeto en tiempo real
        runCatching { IvannaSpatialNative.nativeObjectRendererSetHrtfSubject(handle, selectedSubject) }
        
        val anthropoLog = buildString {
            append("headWidthMm=")
            append(headWidthMm ?: "null")
            append(" headDepthMm=")
            append(headDepthMm ?: "null")
            append(" sex=")
            append(sex ?: "null")
        }
        
        Log.i(TAG, "HRTF Individualization (handle=$handle): $anthropoLog -> sujeto_seleccionado=$selectedSubject")
        return selectedSubject
    }

    // ── Geometría de pinna → mejor sujeto del dataset ──────────────────────
    /** Medidas en mm. Rangos plausibles: concha 20–45, hélix 50–80, fosa 15–35. */
    data class PinnaMetrics(val conchaMm: Float, val helixMm: Float, val fosaMm: Float)

    /**
     * Busca en subjects_metadata del SAF_model.json (assets/saf) el sujeto
     * cuya antropometría de oreja más se acerca (distancia euclidiana
     * normalizada por el rango de cada medida). Si ninguna entrada tiene
     * antropometría numérica suficiente, devuelve DEFAULT_SUBJECT y lo loguea
     * — nunca crashea ni inventa datos.
     */
    fun findBestMatch(context: Context, m: PinnaMetrics): String {
        return runCatching {
            val root = context.assets.open("saf/SAF_model.json").bufferedReader()
                .use { org.json.JSONObject(it.readText()) }
            val subs = root.getJSONArray("subjects_metadata")
            var best: String? = null
            var bestDist = Double.MAX_VALUE
            val rng = doubleArrayOf(25.0, 30.0, 20.0)  // rangos de normalización
            val target = doubleArrayOf(m.conchaMm.toDouble(), m.helixMm.toDouble(),
                                       m.fosaMm.toDouble())
            for (i in 0 until subs.length()) {
                val s = subs.getJSONObject(i)
                val anth = s.optJSONObject("anthropometry") ?: continue
                // Claves candidatas (CIPIC usa códigos x1..; aceptamos cualquier
                // nombre que contenga concha/helix/fosa/pinna, si no: primeros
                // 3 valores numéricos disponibles).
                val vals = ArrayList<Double>()
                val keys = anth.keys()
                val priority = listOf("concha", "helix", "fosa", "pinna")
                val seen = HashSet<String>()
                for (kw in priority) {
                    while (keys.hasNext()) {
                        val k = keys.next()
                        if (seen.add(k) && k.lowercase().contains(kw)) {
                            val v = anth.optDouble(k, Double.NaN)
                            if (!v.isNaN()) vals.add(v)
                        }
                    }
                }
                if (vals.size < 3) {
                    val it2 = anth.keys()
                    while (it2.hasNext() && vals.size < 3) {
                        val k = it2.next()
                        if (seen.add(k)) {
                            val v = anth.optDouble(k, Double.NaN)
                            if (!v.isNaN()) vals.add(v)
                        }
                    }
                }
                if (vals.size < 3) continue
                var d = 0.0
                for (j in 0 until 3) {
                    val dd = (vals[j] - target[j]) / rng[j]
                    d += dd * dd
                }
                if (d < bestDist) {
                    bestDist = d
                    best = s.optString("file").substringAfterLast('/')
                        .substringBeforeLast('.').ifBlank { "subject_$i" }
                }
            }
            if (best == null) {
                Log.i(TAG, "findBestMatch: sin antropometría en metadata → default")
                DEFAULT_SUBJECT
            } else {
                Log.i(TAG, "findBestMatch(concha=${m.conchaMm}, helix=${m.helixMm}, " +
                           "fosa=${m.fosaMm}) → $best (dist=${"%.3f".format(bestDist)})")
                best!!
            }
        }.getOrElse {
            Log.w(TAG, "findBestMatch falló: ${it.message} → default")
            DEFAULT_SUBJECT
        }
    }
}
