package com.ivanna.omega.spatial

/**
 * Contrato de entrada de la calibración SAF: EXACTAMENTE cinco errores direccionales
 * normalizados, finitos y en [0, 1] (frente, atrás, izquierda, derecha, arriba;
 * 0 = correcto, 1 = error máximo).
 *
 * Es Kotlin puro (sin Android ni JNI) para poder probarse en JVM. Antes,
 * SaFOptimizer.runCalibrationStep() promediaba el array tal cual: un array vacío daba NaN, y
 * un NaN/Inf o un valor fuera de rango contaminaba errorEnergy, que se persiste y se envía al
 * optimizador nativo (SaFRoomBridge.setHrtfState).
 */
object SaFCalibrationValidator {

    const val DIRECTIONS = 5

    /**
     * Devuelve una COPIA defensiva del array si cumple el contrato, o null si no.
     * La copia evita que el llamador mute el array mientras la coroutine lo promedia.
     */
    fun sanitize(errors: FloatArray): FloatArray? {
        if (errors.size != DIRECTIONS) return null
        for (e in errors) {
            if (!e.isFinite() || e < 0f || e > 1f) return null
        }
        return errors.copyOf()
    }

    /** Media de errores de un array YA validado con [sanitize]. */
    fun meanError(validated: FloatArray): Float {
        var sum = 0f
        for (e in validated) sum += e
        return sum / validated.size
    }
}
