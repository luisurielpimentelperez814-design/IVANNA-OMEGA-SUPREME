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
        IvannaSpatialNative.nativeObjectRendererSetHrtfSubject(handle, selectedSubject)
        
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

