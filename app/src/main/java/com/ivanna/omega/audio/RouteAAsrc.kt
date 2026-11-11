package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.core.NativeLibraryLoader

/**
 * ASRC MAGISTRAL para Ruta A — envoltorio Kotlin del nativo
 * `ivanna_route_a_asrc` (FIR 319 taps + Catmull-Rom + FIR Ψ 64 taps/oído).
 *
 * REGLA #1 DE ESSE (2026-11-11): nada básico, magistral, omnipotente,
 * riguroso, literal. Esto reemplaza al remuestreador Kotlin de 64 taps
 * (StereoAudioResampler) como actuador de corrección de reloj: el FIR
 * 319 taps del v6 es ~20 dB mejor en transitorios y la FIR Ψ añade el
 * campo acústico. El remuestreador Kotlin QUEDA como fallback (fail-closed).
 *
 * Arquitectura:
 *  - El nativo vive en libivanna_omega.so (mismo target que DSPBridge).
 *  - Una instancia por reproducción (estado FIR + posición fraccionaria
 *    + historia Ψ son locales, sin compartir entre pistas).
 *  - processInto(): el nativo produce un número variable de frames por
 *    bloque (ratio ≠ 1.0); un FIFO interno absorbe la diferencia y
 *    devuelve siempre la cadencia del bloque de entrada, para que
 *    track.write() vea escrituras regulares y el contrato de bloque del
 *    DSP no se rompa. Sin allocs en el camino de audio (buffers
 *    reutilizados; solo crece el scratch si llega un bloque mayor).
 *  - setClockCorrectionPpm(): alimenta el ratio del nativo desde el PLL
 *    (AudioTrackBufferPll). Si el nativo NO está disponible, es no-op y
 *    el llamador debe usar resampler.setClockCorrectionPpm (fallback).
 *  - FIR Ψ: VIVA POR DEFECTO (2026-11-11, directiva de Esse: Ψ funciona
 *    sí o sí). La app calcula los taps ella misma con PsiCore (Ec. 2 ILD +
 *    Ec. 4 η + designFir 64 taps/oído) y alimenta η con la coherencia de
 *    la envolvente 1–8 Hz medida en los propios bloques: NO depende del
 *    daemon ni de ninguna SHM. setPsiFir + setPsiActive quedan para una
 *    fuente externa futura (fusión/daemon); mientras haya taps externos,
 *    mandan ellos. setPsiAuto(true) devuelve la propiedad a la app.
 *
 * Fail-closed: si la librería nativa no carga o la creación falla,
 * isAvailable=false y processInto() es passthrough (copia entrada→salida).
 * El audio sigue sonando con el remuestreador Kotlin; nunca se rompe.
 */
class RouteAAsrc {

    companion object {
        private const val TAG = "IVANNA_ROUTE_A_ASRC"
        private const val FIFO_CAP_FRAMES = 16384       // ~341 ms a 48 kHz
        private const val FIFO_HIGH_WATER_FRAMES = 8192 // válvula: drenar extra
        private const val FIFO_DRAIN_EXTRA_FRAMES = 512
        private const val PSI_TAPS = 64
    }

    private val loaded = NativeLibraryLoader.ensureLoaded()

    private var handle: Long = 0L

    /**
     * true si el nativo está cargado Y la instancia se creó. false =
     * fallback Kotlin (el remuestreador lleva la corrección de reloj).
     */
    val isAvailable: Boolean
        get() = loaded && handle != 0L

    // FIFO circular (frames estéreo). fifoRead/fifoWrite en frames.
    private val fifo = FloatArray(FIFO_CAP_FRAMES * 2)
    private var fifoRead = 0
    private var fifoWrite = 0
    private var fifoCount = 0

    // Scratch de salida del nativo (reutilizado; crece si hace falta).
    private var scratchOut = FloatArray(4096 * 2)

    init {
        if (loaded) {
            handle = runCatching { nativeCreate() }.getOrDefault(0L)
            if (handle == 0L) {
                Log.w(TAG, "ASRC nativo no se pudo crear; fallback Kotlin activo")
            }
        }
    }

    /**
     * Procesa `inFrames` (estéreo intercalado) de `input` y escribe la
     * salida en `out`. Devuelve los frames escritos (<= out.size/2).
     * Sin nativo: passthrough (copia) y devuelve inFrames.
     * El llamador debe dimensionar `out` con al menos inFrames frames
     * (más margen si usa el high-water drain).
     */
    fun processInto(input: FloatArray, inFrames: Int, out: FloatArray): Int {
        if (inFrames <= 0) return 0
        val need = inFrames * 2
        if (input.size < need) return 0
        if (out.size < need) return 0
        if (!isAvailable) {
            // Fallback: passthrough. La corrección de reloj la lleva el
            // remuestreador Kotlin (el llamador lo sabe por isAvailable).
            input.copyInto(out, 0, 0, need)
            return inFrames
        }
        // Capacidad de salida del nativo: ratio máx 1.001 + carry ±1.
        val outCapFrames = (inFrames * 1002 / 1000) + 32
        if (scratchOut.size < outCapFrames * 2) {
            scratchOut = FloatArray(outCapFrames * 2)
        }
        val produced = runCatching {
            nativeProcess(handle, input, inFrames, scratchOut, outCapFrames)
        }.getOrDefault(0)
        if (produced <= 0) {
            // El nativo no produjo nada (bloque vacío o error): devolver
            // lo que haya en el FIFO para no dejar el track sin datos.
            return drainFifo(out, inFrames)
        }
        appendFifo(scratchOut, produced)
        return drainFifo(out, inFrames)
    }

    /** Alimenta el ratio del nativo desde el PLL (ppm). No-op sin nativo. */
    fun setClockCorrectionPpm(ppm: Double) {
        if (!isAvailable) return
        runCatching { nativeSetClockCorrectionPpm(handle, ppm) }
    }

    /** Tasa de muestreo del track (Hz) para el diseño del FIR Ψ. */
    fun setSampleRate(sampleRateHz: Float) {
        if (!isAvailable) return
        runCatching { nativeSetSampleRate(handle, sampleRateHz) }
    }

    /** Geometría del campo Ψ: radio de cabeza (m), distancia (m), azimut (rad). */
    fun setPsiGeometry(headRadiusM: Float, distanceM: Float, azimuthRad: Float) {
        if (!isAvailable) return
        runCatching { nativeSetPsiGeometry(handle, headRadiusM, distanceM, azimuthRad) }
    }

    /** Auto-diseño de Ψ por la app (default true). false = Ψ apagada. */
    fun setPsiAuto(autoOn: Boolean) {
        if (!isAvailable) return
        runCatching { nativeSetPsiAuto(handle, autoOn) }
    }

    /** Congela el diseño de los FIR Ψ (taps fijos, puerta como esté). */
    fun setPsiDesignHold(hold: Boolean) {
        if (!isAvailable) return
        runCatching { nativeSetPsiDesignHold(handle, hold) }
    }

    /** η actual del campo [0,1] (diagnóstico). 0.5 sin nativo. */
    val psiEta: Double
        get() = if (!isAvailable) 0.5 else runCatching { nativePsiEta(handle) }.getOrDefault(0.5)

    /** Última coherencia de envolvente medida [0,1] (diagnóstico). */
    val psiCoherence: Double
        get() = if (!isAvailable) 0.5 else runCatching { nativePsiCoherence(handle) }.getOrDefault(0.5)

    /** Puerta Ψ: true = el campo convoluciona (telemetría en vivo v2.5.407). */
    val psiActive: Boolean
        get() = if (!isAvailable) false else runCatching { nativePsiActive(handle) }.getOrDefault(false)

    /** Corrección de reloj APLICADA por el nativo (ppm, telemetría v2.5.407). */
    val clockCorrectionPpm: Double
        get() = if (!isAvailable) 0.0 else runCatching { nativeClockCorrectionPpm(handle) }.getOrDefault(0.0)

    /** FIR Ψ del cerebro (64 taps por oído). No-op sin nativo. */
    fun setPsiFir(firL: FloatArray, firR: FloatArray) {
        if (!isAvailable) return
        if (firL.size < PSI_TAPS || firR.size < PSI_TAPS) return
        runCatching { nativeSetPsiFir(handle, firL, firR) }
    }

    /** Puerta Ψ: true = el campo convoluciona; false = bypass. */
    fun setPsiActive(active: Boolean) {
        if (!isAvailable) return
        runCatching { nativeSetPsiActive(handle, active) }
    }

    /** Reinicia el estado (seek, reroute, cambio de pista). */
    fun reset() {
        fifoRead = 0
        fifoWrite = 0
        fifoCount = 0
        if (isAvailable) runCatching { nativeReset(handle) }
    }

    fun close() {
        if (handle != 0L) {
            runCatching { nativeDestroy(handle) }
            handle = 0L
        }
        fifoRead = 0
        fifoWrite = 0
        fifoCount = 0
    }

    // ── FIFO interno (anillo, sin allocs) ────────────────────────────────

    private fun appendFifo(src: FloatArray, frames: Int) {
        var remaining = frames
        var srcOff = 0
        while (remaining > 0) {
            // Si el FIFO se llenaría, descartar lo más viejo (fail-safe:
            // mejor perder ~ms de audio que bloquear el hilo o crecer sin
            // límite; el PLL mantiene la tasa y esto no debería ocurrir).
            if (fifoCount >= FIFO_CAP_FRAMES) {
                val drop = minOf(remaining, fifoCount)
                fifoRead = (fifoRead + drop) % FIFO_CAP_FRAMES
                fifoCount -= drop
                Log.w(TAG, "FIFO ASRC lleno; descartados $drop frames viejos")
            }
            val space = FIFO_CAP_FRAMES - fifoCount
            val chunk = minOf(remaining, space)
            val first = minOf(chunk, FIFO_CAP_FRAMES - fifoWrite)
            src.copyInto(fifo, fifoWrite * 2, srcOff * 2, srcOff * 2 + first * 2)
            if (first < chunk) {
                src.copyInto(fifo, 0, (srcOff + first) * 2, (srcOff + first) * 2 + (chunk - first) * 2)
            }
            fifoWrite = (fifoWrite + chunk) % FIFO_CAP_FRAMES
            fifoCount += chunk
            srcOff += chunk
            remaining -= chunk
        }
    }

    private fun drainFifo(out: FloatArray, wantFrames: Int): Int {
        if (fifoCount <= 0) return 0
        // Válvula high-water: si el FIFO acumuló demasiado (deriva
        // sostenida), drenar un poco extra para bajar el backlog.
        var take = minOf(wantFrames, fifoCount)
        if (fifoCount > FIFO_HIGH_WATER_FRAMES) {
            take = minOf(fifoCount, wantFrames + FIFO_DRAIN_EXTRA_FRAMES)
        }
        take = minOf(take, out.size / 2)
        val first = minOf(take, FIFO_CAP_FRAMES - fifoRead)
        fifo.copyInto(out, 0, fifoRead * 2, fifoRead * 2 + first * 2)
        if (first < take) {
            fifo.copyInto(out, first * 2, 0, (take - first) * 2)
        }
        fifoRead = (fifoRead + take) % FIFO_CAP_FRAMES
        fifoCount -= take
        return take
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeProcess(
        handle: Long, input: FloatArray, inFrames: Int,
        out: FloatArray, outCap: Int
    ): Int
    private external fun nativeSetClockCorrectionPpm(handle: Long, ppm: Double)
    private external fun nativeSetSampleRate(handle: Long, sampleRateHz: Float)
    private external fun nativeSetPsiGeometry(handle: Long, headRadiusM: Float, distanceM: Float, azimuthRad: Float)
    private external fun nativeSetPsiAuto(handle: Long, autoOn: Boolean)
    private external fun nativeSetPsiDesignHold(handle: Long, hold: Boolean)
    private external fun nativePsiEta(handle: Long): Double
    private external fun nativePsiCoherence(handle: Long): Double
    private external fun nativePsiActive(handle: Long): Boolean
    private external fun nativeClockCorrectionPpm(handle: Long): Double
    private external fun nativeSetPsiActive(handle: Long, active: Boolean)
    private external fun nativeSetPsiFir(handle: Long, firL: FloatArray, firR: FloatArray)
    private external fun nativeReset(handle: Long)
}
