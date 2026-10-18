package com.ivanna.omega.audio.engine

/**
 * Política del COLCHÓN de salida del AudioTrack de la Ruta A (lógica pura, probada en JVM).
 *
 * Problema que corrige: el AudioTrack arrancaba vacío y se alimentaba 1 bloque por cada bloque capturado, así que
 * la cola real era ~1 bloque (6,7 ms a 320 frames). Cualquier retraso del hilo > 6,7 ms (GC, planificador, un
 * decodificador de vídeo) la vaciaba: underrun del HAL = micro-corte audible. `targetHeadroomFrames` (20 ms) se
 * calculaba pero nada llevaba la cola hasta él, y `MasterTimingController.applyMicroExtension` no se invoca.
 *
 * Con la reinyección DIFERENCIAL (`LEGACY_HAAS_MIX=false`) lo que suena por esta pista es solo `procesado − seco`;
 * el silencio es la señal NEUTRA, así que rellenar con ceros no mete clics ni cambia el timbre: solo añade cola.
 * El reinyector ya alinea con la latencia MEDIDA, por lo que el colchón no crea peine.
 */
object OutputCushionPolicy {
    /** Nunca se llena la pista hasta el tope: así `WRITE_BLOCKING` no frena el bucle ni desborda la captura. */
    private const val SPARE_BLOCKS = 2
    /** Espera mínima entre dos re-llenados (ms): evita que una racha de underruns infle la latencia sin límite. */
    const val REPRIME_COOLDOWN_MS = 750L

    /** Frames de colchón deseados: el headroom objetivo, acotado a lo que cabe en la pista con margen. */
    fun cushionFrames(targetHeadroomFrames: Long, trackBufferFrames: Int, blockFrames: Int): Int {
        if (trackBufferFrames <= 0 || blockFrames <= 0) return 0
        val room = (trackBufferFrames - SPARE_BLOCKS * blockFrames).coerceAtLeast(0)
        return targetHeadroomFrames.coerceIn(0L, room.toLong()).toInt()
    }

    /**
     * Frames de silencio a escribir ahora para reponer el colchón, o 0.
     * Solo actúa si HUBO un underrun nuevo (el HAL ya se quedó sin datos), la cola cayó por debajo de la mitad del
     * colchón y pasó el cooldown. Escribe solo lo que falta, nunca más del colchón.
     */
    fun reprimeFrames(
        newUnderruns: Int,
        queuedFrames: Long,
        cushionFrames: Int,
        msSinceLastReprime: Long,
    ): Int {
        if (newUnderruns <= 0 || cushionFrames <= 0) return 0
        if (msSinceLastReprime < REPRIME_COOLDOWN_MS) return 0
        val q = queuedFrames.coerceAtLeast(0L)
        if (q * 2 >= cushionFrames) return 0
        return (cushionFrames - q).coerceIn(0L, cushionFrames.toLong()).toInt()
    }
}
