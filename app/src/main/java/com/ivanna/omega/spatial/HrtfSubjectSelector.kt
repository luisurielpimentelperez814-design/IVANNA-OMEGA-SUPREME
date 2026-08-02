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

        // TODO (siguiente commit):
        //   1. Cargar assets/hrtf/subjects.json con las medidas antropométricas
        //      de cada sujeto CIPIC (ancho, profundidad, sexo).
        //   2. Rankear por distancia euclídea normalizada a (headWidthMm,
        //      headDepthMm) — sesgo por sexo si está disponible.
        //   3. Llamar a IvannaSpatialNative.nativeObjectRendererSetHrtfSubject(
        //         handle, subjectId) — hoy ese JNI no existe.
        //   4. Devolver subjectId real.
        //
        // Hoy: sujeto default embarcado por el módulo Magisk. El dataset ya
        // vive en /data/adb/ivanna_omega/hrtf_dataset.ihr1 y omega_effect.cpp
        // lo carga al recibir EFFECT_CMD_SET_CONFIG (ver commit 7d7bcf9).

        val anthropoLog = buildString {
            append("headWidthMm=")
            append(headWidthMm ?: "null")
            append(" headDepthMm=")
            append(headDepthMm ?: "null")
            append(" sex=")
            append(sex ?: "null")
        }
        Log.i(TAG, "activate(handle=$handle): $anthropoLog → sujeto=$DEFAULT_SUBJECT (default embarcado)")
        return DEFAULT_SUBJECT
    }
}
