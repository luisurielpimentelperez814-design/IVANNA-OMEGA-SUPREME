package com.ivanna.omega.audio.engine

/**
 * Gobernador de la carga REAL de una vuelta del bucle de audio de la Ruta A (lógica pura, probada en JVM).
 *
 * `AudioThreadBudgetGuard` solo cronometra captura→DSP→salida; después de escribir al AudioTrack el bucle aún
 * alimenta visualizadores, analizadores y medidores, y todo eso corre ANTES de la siguiente lectura. Si la vuelta
 * entera supera el periodo del bloque (6,67 ms a 320 frames) la cola de salida se vacía → underrun. Este gobernador
 * mide la vuelta completa y, SOLO bajo presión, pide ejecutar los análisis puramente visuales cada N bloques.
 * El audio (DSP, limitador, escritura) nunca se decima.
 *
 * Histéresis: sube de paso a ≥ [UP_2]/[UP_4] y solo baja cuando cae [HYST] por debajo, para no oscilar.
 */
class LoopLoadGovernor {
    companion object {
        const val UP_2 = 0.55f
        const val UP_4 = 0.75f
        const val HYST = 0.08f
        private const val ALPHA = 0.10f
    }

    /** Carga suavizada de la vuelta completa (1.0 = el periodo entero del bloque). */
    @Volatile var load: Float = 0f
        private set

    /** Cada cuántos bloques corren los análisis visuales: 1, 2 o 4. */
    @Volatile var stride: Int = 1
        private set

    fun update(iterationNs: Long, periodNs: Long) {
        if (periodNs <= 0L || iterationNs < 0L) return
        val inst = (iterationNs.toDouble() / periodNs.toDouble()).toFloat().coerceIn(0f, 1.5f)
        load = if (load == 0f) inst else (1f - ALPHA) * load + ALPHA * inst
        stride = when (stride) {
            1 -> if (load >= UP_4) 4 else if (load >= UP_2) 2 else 1
            2 -> if (load >= UP_4) 4 else if (load < UP_2 - HYST) 1 else 2
            else -> if (load < UP_4 - HYST) (if (load >= UP_2) 2 else 1) else 4
        }
    }

    /** true si en este bloque deben correr los análisis visuales. */
    fun runAnalysis(blockIndex: Int): Boolean = stride <= 1 || (blockIndex % stride) == 0

    fun reset() { load = 0f; stride = 1 }
}
