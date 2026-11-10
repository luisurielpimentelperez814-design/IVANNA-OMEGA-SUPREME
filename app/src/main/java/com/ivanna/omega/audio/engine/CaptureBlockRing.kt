package com.ivanna.omega.audio.engine

import java.util.concurrent.atomic.AtomicLongArray

/**
 * CaptureBlockRing — anillo SPSC (un productor / un consumidor) de bloques
 * de audio preasignados entre el hilo lector (AudioRecord) y el hilo DSP
 * de la Ruta A. Logica pura, sin Android: probada en JVM.
 *
 * Por que existe (Moto G85, 2026-11-09, sysreport 17:25 CST):
 * - 173 avisos `SOBRECARGA del bucle de captura` en una ventana: la
 *   sobrecarga es SOSTENIDA, no esporadica.
 * - Load average 6.68 / 7.95 / 7.30 en 8 nucleos (CPU esencialmente
 *   saturada), MemAvailable 1.57 GB de 7.59 GB, TIDAL (298 MB) y
 *   YouTube Music (252 MB) a la vez. Bajo esa carga el planificador
 *   preempta el hilo de captura y cualquier jitter retrasa la lectura.
 * - El bucle era sincrono (leer -> NPE -> DSP 3.5-8.9 ms -> salida ->
 *   escritura BLOQUEANTE -> telemetria -> leer): un bloque lento
 *   retrasaba la siguiente lectura y el anillo de AudioRecord (8
 *   bloques = 53 ms) se drenaba en ~1 s -> `RecordThread: buffer
 *   overflow` -> dropouts.
 *
 * El anillo desacopla la lectura del DSP: el lector SOLO lee y publica
 * (nunca se bloquea esperando al DSP) y el hilo DSP consume cuando
 * puede. 16 bloques = ~107 ms de colchon entre ambos, que se suman a
 * los 53 ms del anillo de AudioRecord. Cero asignaciones en el hot path
 * (buffers preasignados en el constructor; el lector copia su bloque a
 * un slot libre — una copia de 640 floats, ~microsegundos, igual que
 * el `System.arraycopy` a dryRef que ya hacia el bucle).
 *
 * Correctitud (SPSC clasico con secuencia por slot, estilo Disruptor):
 * - Cada slot tiene una secuencia volatil (`AtomicLongArray`): el slot i
 *   arranca libre para la escritura nº i. El productor solo escribe un
 *   slot cuya secuencia == su nº de escritura y publica con secuencia+1
 *   (escritura volatil: todo lo copiado antes es visible al lector). El
 *   consumidor solo lee un slot cuya secuencia == su nº de lectura+1 y
 *   lo libera con secuencia = nº de lectura + slots. Un slot en manos
 *   del consumidor JAMAS se reutiliza para escribir: no hay lecturas
 *   rotas ni carreras, sin locks.
 * - Productor y consumidor tocan cada uno SOLO su propio contador
 *   (`writeSeq` / `readSeq`); el otro hilo solo los lee en `pending()`.
 *
 * Fail-closed:
 * - Anillo lleno (DSP muy por detras bajo saturacion del sistema):
 *   `offer()` devuelve false y el bloque se DESCARTA CONTADO
 *   (`droppedBlocks`) — el lector sigue leyendo el audio fresco. Se
 *   pierde un delta de 6.7 ms, no la captura de origen; con el
 *   gobernador de shed (DspLoadShedGovernor, nativo) los drops deben
 *   ser ~0. Jamas bloquea al lector ni silencia nada.
 * - Anillo vacio (arranque): `beginRead()` devuelve null; el consumidor
 *   espera sin quemar CPU. No se inventa audio.
 * - `beginRead()` exige `endRead()` pareado: el slot queda reservado
 *   hasta liberarlo; si el consumidor muere sin liberar, ese slot
 *   simplemente deja de reciclarse (el anillo se estrecha, no se corrompe).
 */
class CaptureBlockRing(
    val slots: Int = 16,
    val blockSamples: Int = 640, // BLOCK_SAMPLES: 320 frames estereo
) {
    init {
        require(slots >= 4 && slots and (slots - 1) == 0) { "slots debe ser potencia de 2 y >= 4" }
        require(blockSamples > 0) { "blockSamples debe ser > 0" }
    }

    private val mask = slots - 1
    private val buffers = Array(slots) { FloatArray(blockSamples) }
    private val framesInSlot = IntArray(slots)
    private val seqs = AtomicLongArray(slots)

    @Volatile private var writeSeq = 0L
    @Volatile private var readSeq = 0L

    @Volatile var offeredBlocks = 0L
        private set
    @Volatile var droppedBlocks = 0L
        private set
    @Volatile var polledBlocks = 0L
        private set

    init {
        reset()
    }

    /** Copia `frames` frames estereo de `src` a un slot libre y lo publica. false = anillo lleno (drop contado). */
    fun offer(src: FloatArray, frames: Int): Boolean {
        if (frames <= 0) return false
        val samples = frames * 2
        if (samples > blockSamples || samples > src.size) return false
        val pos = writeSeq
        val idx = (pos and mask.toLong()).toInt()
        if (seqs.get(idx) != pos) {
            droppedBlocks++
            return false
        }
        src.copyInto(buffers[idx], 0, 0, samples)
        framesInSlot[idx] = frames
        seqs.set(idx, pos + 1) // publicacion: el lector ya puede ver el slot
        writeSeq = pos + 1
        offeredBlocks++
        return true
    }

    /**
     * Slot publicado mas antiguo, o null si el anillo esta vacio.
     * El llamador DEBE llamar [endRead] tras copiar/procesar el slot.
     */
    fun beginRead(): ReadSlot? {
        val pos = readSeq
        val idx = (pos and mask.toLong()).toInt()
        if (seqs.get(idx) != pos + 1) return null
        return ReadSlot(buffers[idx], framesInSlot[idx])
    }

    /** Libera el slot tomado por [beginRead]. */
    fun endRead() {
        val pos = readSeq
        val idx = (pos and mask.toLong()).toInt()
        seqs.set(idx, pos + slots) // libre para la escritura nº pos+slots
        readSeq = pos + 1
        polledBlocks++
    }

    /** Nº de bloques publicados pendientes de leer (0..slots). */
    fun pending(): Int {
        val d = writeSeq - readSeq
        return d.coerceIn(0L, slots.toLong()).toInt()
    }

    fun reset() {
        for (i in 0 until slots) seqs.set(i, i.toLong())
        writeSeq = 0L
        readSeq = 0L
        offeredBlocks = 0L
        droppedBlocks = 0L
        polledBlocks = 0L
    }

    class ReadSlot(val data: FloatArray, val frames: Int)
}
