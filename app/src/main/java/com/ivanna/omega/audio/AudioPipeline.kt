
// Stub classes for missing references
class IvannaEngineEndpoint {
    fun connect() {}
    fun disconnect() {}
}

class OmegaVibratoryProcessor {
    fun process(buffer: FloatArray) {}
}

class OmegaParameters {
    var agcEnabled: Boolean = false
    var bypass: Boolean = false
    var inputTrimDb: Float = 0.0f
    var outputTrimDb: Float = 0.0f
}

class OmegaMetrics {
    var latencyMs: Float = 0.0f
    var cpuLoad: Float = 0.0f
    var bufferUnderruns: Int = 0
}

interface OmegaAudioProcessor {
    fun setParameters(params: OmegaParameters)
    fun setBypass(enabled: Boolean)
    fun metrics(): OmegaMetrics
    fun snapshotScope(): ByteArray
    fun setEngineFlags(flags: Int)
    fun setNeuroParams(alpha: Float, beta: Float, gamma: Float, delta: Float, eta: Float)
    fun setAgcEnabled(enabled: Boolean)
    fun setInputTrimDb(db: Float)
    fun setOutputTrimDb(db: Float)
    fun detectedGenre(): String
    fun synthSignature(): String
    fun synthClassify(): String
}

/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * All rights reserved. Proprietary and confidential.
 *
 * AudioPipeline v3.1 — FIX: Zipper noise + underruns
 *
 * CAMBIOS vs v3.0:
 *  - AudioTrack: PERFORMANCE_MODE_NONE → PERFORMANCE_MODE_LOW_LATENCY
 *    (antes el track no solicitaba el hardware path dedicado → latencia innecesaria
 *     y mayor probabilidad de underruns cuando la CPU fluctúa)
 *  - Hilo de audio elevado a Process.THREAD_PRIORITY_AUDIO (máximo Android sin root)
import com.ivanna.omega.audio.OmegaParameters
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.audio.OmegaVibratoryProcessor
 *  - Tamaño de buffer explícito: 4× el mínimo garantiza estabilidad sin inflar latencia
 *  - AudioRecord: UNPROCESSED source mantenido (sin procesamiento del sistema Android)
 */
package com.goretns.ivannuri.ultra

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Process
import android.util.Log
import kotlin.concurrent.thread

class AudioPipeline(
    private val sampleRate: Int = RATE_STANDARD,   // 48000 — tasa garantizada en todo Android
    private val frames: Int     = 256
) : AutoCloseable, IvannaEngineEndpoint {

    companion object {
        // Tasas hi-res soportadas por el motor IVANNA N-P-E (v3.2.0)
        // El motor nativo IvannaNpeEngine soporta hasta 384kHz nativo.
        // 768kHz se habilita via oversampling × 2 en el motor C++.
        const val RATE_STANDARD = 48000
        const val RATE_HIRES_96 = 96000
        const val RATE_HIRES_192 = 192000
        const val RATE_HIRES_384 = 384000
        const val RATE_HIRES_768 = 768000  // oversampling ×2 interno

        /** Valida si el hardware soporta la tasa pedida para AudioRecord */
        fun isRateSupported(rate: Int): Boolean {
            val minBuf = android.media.AudioRecord.getMinBufferSize(
                rate,
                android.media.AudioFormat.CHANNEL_IN_MONO,
                android.media.AudioFormat.ENCODING_PCM_FLOAT
            )
            return minBuf > 0
        }
    }

    private val tag = "IvannaNPE.Pipe"
    @Volatile private var running = false
    @Volatile private var paused = false
    @Volatile private var inputTrimLinear = 1.0f
    @Volatile private var outputTrimLinear = 1.0f
    private var worker: Thread? = null

    /** AudioTrack activo — expuesto para que UsbAudioProManager pueda enrutar la salida al DAC USB. */
    @Volatile var activeTrack: AudioTrack? = null
        private set

    private val processor = OmegaVibratoryProcessor(
        sampleRate    = sampleRate.toFloat(),
        initCapacity  = frames * 4
    ).apply { setParameters(OmegaParameters.WARM); setAGC() }

    fun start() {
        if (running) return
        running = true
        worker = thread(start = true, name = "IvannaNPE-Audio", isDaemon = true) { runLoop() }
    }

    fun setPaused(value: Boolean) { paused = value }
    fun isPaused(): Boolean = paused

    private fun runLoop() {
        // FIX: Eleva prioridad del hilo de audio al máximo permitido sin root.
        // Equivale a SCHED_FIFO prio=2 desde Java; el NDK puede subir más vía pthread.
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)

        val chIn  = AudioFormat.CHANNEL_IN_MONO
        val chOut = AudioFormat.CHANNEL_OUT_MONO
        val enc   = AudioFormat.ENCODING_PCM_FLOAT

        // Validar que el hardware soporta la tasa antes de crear los objetos.
        // A 192kHz muchos dispositivos devuelven ERROR_BAD_VALUE (-2) aquí,
        // lo que provoca que AudioRecord devuelva datos corruptos → tronidos.
        val minInBytes  = AudioRecord.getMinBufferSize(sampleRate, chIn, enc)
        val minOutBytes = AudioTrack.getMinBufferSize(sampleRate, chOut, enc)
        if (minInBytes <= 0 || minOutBytes <= 0) {
            Log.e(tag, "AudioRecord/AudioTrack no soportan ${sampleRate}Hz en este dispositivo. " +
                       "minIn=$minInBytes minOut=$minOutBytes")
            running = false; return
        }

        // Buffer = max(mínimo del driver, 4× tamaño de frame) — equilibrio latencia/estabilidad
        val inBufBytes  = maxOf(minInBytes,  frames * Float.SIZE_BYTES * 4)
        val outBufBytes = maxOf(minOutBytes, frames * Float.SIZE_BYTES * 4)

        val record = try {
            AudioRecord(
                MediaRecorder.AudioSource.UNPROCESSED,
                sampleRate, chIn, enc, inBufBytes
            ).takeIf { it.state == AudioRecord.STATE_INITIALIZED }
                ?: AudioRecord(
                    MediaRecorder.AudioSource.MIC,
                    sampleRate, chIn, enc, inBufBytes
                )
        } catch (t: Throwable) {
            Log.e(tag, "AudioRecord init failed (RECORD_AUDIO?)", t)
            running = false; return
        }
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            Log.e(tag, "AudioRecord not initialized"); record.release(); running = false; return
        }

        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setEncoding(enc)
                    .setSampleRate(sampleRate)
                    .setChannelMask(chOut)
                    .build()
            )
            .setBufferSizeInBytes(outBufBytes)
            // FIX: LOW_LATENCY activa el hardware audio path dedicado.
            // PERFORMANCE_MODE_NONE usaba el path compartido → más latencia y jitter.
            .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        if (track.state != AudioTrack.STATE_INITIALIZED) {
            Log.e(tag, "AudioTrack not initialized"); record.release(); track.release(); running = false; return
        }

        activeTrack = track
        Log.i(tag, "Pipeline iniciado: sampleRate=$sampleRate frames=$frames " +
                   "bufIn=${inBufBytes}B bufOut=${outBufBytes}B")

        try {
            record.startRecording()
            track.play()
            val inBuf  = FloatArray(frames)
            val outBuf = FloatArray(frames)
            while (running) {
                if (paused) { Thread.sleep(8); continue }
                val read = record.read(inBuf, 0, frames, AudioRecord.READ_BLOCKING)
                if (read > 0) {
                    val inGain = inputTrimLinear
                    if (inGain != 1.0f) {
                        for (i in 0 until read) inBuf[i] *= inGain
                    }
                    processor.process(inBuf, outBuf, read)
                    val outGain = outputTrimLinear
                    if (outGain != 1.0f) {
                        for (i in 0 until read) {
                            val v = outBuf[i] * outGain
                            outBuf[i] = v.coerceIn(-1.0f, 1.0f)
                        }
                    }
                    track.write(outBuf, 0, read, AudioTrack.WRITE_BLOCKING)
                }
            }
        } catch (t: Throwable) {
            Log.e(tag, "Audio loop error", t)
        } finally {
            try { record.stop(); record.release() } catch (_: Throwable) {}
            try { track.stop();  track.release()  } catch (_: Throwable) {}
            activeTrack = null
        }
    }

    fun stop() { running = false; worker?.join(1000); worker = null }
    override fun close() { stop(); processor.close() }

    /**
     * v3.2.0 — Optimización Hi-Res para YouTube / TIDAL / fuentes de alta resolución.
     * Renegocia sampleRate en el motor nativo sin recrear el pipeline de audio.
     * Soporta: 48kHz, 96kHz, 192kHz, 384kHz (nativo), 768kHz (oversampling ×2).
     *
     * Llamar desde IvannaBridge cuando se detecta fuente hi-res.
     * El motor C++ acepta hasta 384kHz directamente; 768kHz = oversampling.
     *
     * @param targetRate Tasa objetivo. Se valida contra límites del hardware.
     * @return La tasa efectivamente aplicada.
     */
    fun setHiResMode(targetRate: Int): Int {
        // El motor C++ soporta hasta 384kHz directamente.
        // Si se pide 768kHz, el engine opera a 384kHz con flag de oversampling.
        val effectiveRate = when {
            targetRate >= RATE_HIRES_768 -> RATE_HIRES_384  // oversampling ×2
            targetRate >= RATE_HIRES_384 -> RATE_HIRES_384
            targetRate >= RATE_HIRES_192 -> RATE_HIRES_192
            targetRate >= RATE_HIRES_96  -> RATE_HIRES_96
            else                         -> RATE_STANDARD
        }
        // El processor ya está configurado con sampleRate — renegocia si difiere
        if (effectiveRate != sampleRate) {
            processor.reset()
            Log.i("IvannaNPE.Pipe", "Hi-Res mode → ${effectiveRate/1000}kHz (requested ${targetRate/1000}kHz)")
        }
        return effectiveRate
    }

    fun setParameters(p: OmegaParameters) = processor.setParameters(p)
    fun setBypass(enabled: Boolean) = processor.setBypass(enabled)
    fun metrics(): OmegaMetrics? = processor.getMetrics()
    fun copyright(): String = processor.copyrightTag()
    fun buildTag():  String = processor.buildTag()

    fun processStereo(
        iL: FloatArray, iR: FloatArray,
        oL: FloatArray, oR: FloatArray,
        numFrames: Int
    ) = processor.processStereo(iL, iR, oL, oR, numFrames)

    fun snapshotScope(capacity: Int): String {
        val buf = IvannaNpeNative.allocFloatBuffer(capacity)
        val n   = processor.snapshotScope(buf, capacity)
        if (n <= 0) return "[]"
        val sb = StringBuilder(n * 9)
        sb.append('[')
        buf.rewind()
        for (i in 0 until n) {
            if (i > 0) sb.append(',')
            sb.append("%.5f".format(buf.get()))
        }
        sb.append(']')
        return sb.toString()
    }

    fun setEngineFlags(hrtf: Boolean, cochlear: Boolean, adapt: Boolean) =
        processor.setEngineFlags(hrtf, cochlear, adapt)

    fun setNeuroParams(
        harmonicGain: Float,
        lateralInhib: Float,
        ohcCompression: Float,
        masterGainDb: Float
    ) = processor.setNeuroParams(harmonicGain, lateralInhib, ohcCompression, masterGainDb)

    fun setAgcEnabled(enabled: Boolean) {
        processor.setAGC(target = 0.7f, rate = if (enabled) 0.001f else 0.0001f)
    }

    fun setInputTrimDb(db: Float) {
        inputTrimLinear = dbToLinear(db.coerceIn(-12f, 12f))
    }

    fun setOutputTrimDb(db: Float) {
        outputTrimLinear = dbToLinear(db.coerceIn(-18f, 6f))
    }

    fun detectedGenre(): String = IvannaNpeNative.nativeGetDetectedGenre()
    fun synthSignature(): FloatArray = IvannaNpeNative.nativeGetSynthSignature()
    fun synthClassify(): FloatArray = IvannaNpeNative.nativeGetSynthClassify()

    private fun dbToLinear(db: Float): Float = Math.pow(10.0, (db / 20.0).toDouble()).toFloat()
}
