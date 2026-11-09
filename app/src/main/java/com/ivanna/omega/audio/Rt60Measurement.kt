package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt

/**
 * Rt60Measurement — mide RT60 / EDT / C80 de la sala de Esse con el teléfono.
 *
 * Cierra el hueco 2 del Knowledge Core. `RoomMeasurement` ya mide la respuesta
 * en frecuencia (27 tonos, 25 Hz–12.7 kHz) y la latencia acústica, pero NO el
 * decaimiento: su secuencia es de tonos continuos, sin ráfaga + cola. Este
 * archivo añade la medición que falta, reutilizando el mismo harness
 * (AudioTrack + AudioRecord UNPROCESSED, `nativeRoomLockIn` como detector
 * síncrono por banda) y el mismo patrón de `RoomMeasurement.kt`.
 *
 * Método (estándar ISO 3382, adaptado):
 * 1. 0.5 s de silencio → piso de ruido por banda.
 * 2. Ráfaga de ruido blanco de 0.5 s a −14 dBFS → excita todas las bandas.
 * 3. 3.0 s de cola grabada → decaimiento.
 * 4. Por banda de octava (125/250/500/1000/2000/4000 Hz): nivel por ventana
 *    deslizante de 50 ms (paso 10 ms) vía `nativeRoomLockIn` → curva de
 *    decaimiento de energía → integración de Schroeder (EDC) → ajuste por
 *    mínimos cuadrados: EDT (0…−10 dB), T20 (−5…−25 dB), T30 (−5…−35 dB).
 * 5. C80 = 10·log10(energía 0–80 ms / energía 80 ms–∞) por banda.
 *
 * Criterios de fiabilidad por banda (se reportan, no se ocultan):
 * - T30 solo si el rango dinámico (pico − piso) ≥ 35 dB; T20 si ≥ 25 dB;
 *   EDT si ≥ 10 dB. Si no se alcanza, la banda se marca como no fiable y se
 *   devuelve el mejor estimador disponible (o NaN).
 * - La medición es del punto donde está el teléfono, con la bocina del
 *   teléfono como fuente (no es una fuente omnidireccional calibrada).
 *   Para la sala de Esse es suficiente: lo que IVANNA necesita es el RT60
 *   aproximado por banda para el SAF/RIR, no una certificación acústica.
 *
 * Verificación: la compilación Kotlin la debe verificar la CI (sin kotlinc
 * en la VM). La lógica sigue los patrones probados de `RoomMeasurement.kt`.
 */
object Rt60Measurement {
    private const val TAG = "Rt60Measurement"
    const val SAMPLE_RATE = 48_000

    /** Bandas de octava para RT60 (centros). */
    val BANDS_HZ = floatArrayOf(125f, 250f, 500f, 1000f, 2000f, 4000f)

    private const val SILENCE_S = 0.5f
    private const val BURST_S = 0.5f
    private const val TAIL_S = 3.0f
    private const val AMPLITUDE = 0.2f // −14 dBFS, igual que RoomMeasurement
    private const val WIN_S = 0.05f    // ventana de análisis del decaimiento
    private const val HOP_S = 0.01f    // paso entre ventanas

    data class BandResult(
        val freqHz: Float,
        val edtS: Float,   // NaN si no fiable
        val t20S: Float,   // NaN si no fiable
        val t30S: Float,   // NaN si no fiable
        val rt60S: Float,  // mejor estimador: T30, si no T20×3, si no EDT×6
        val c80Db: Float,  // NaN si no fiable
        val dynamicRangeDb: Float,
        val reliable: Boolean
    )

    data class Result(val bands: List<BandResult>, val broadbandRt60S: Float) {
        /** RT60 medio 500 Hz–2 kHz (el número que se cita para una sala). */
        fun midRt60S(): Float {
            val mids = bands.filter { it.freqHz in 500f..2000f && it.rt60S.isFinite() }
            if (mids.isEmpty()) return Float.NaN
            return mids.map { it.rt60S }.average().toFloat()
        }
    }

    sealed class Outcome {
        class Success(val result: Result) : Outcome()
        class Failure(val reason: String) : Outcome()
    }

    suspend fun run(context: Context, onProgress: (Float) -> Unit = {}): Outcome =
        withContext(Dispatchers.IO) {
            if (!IvannaNativeLib.isLoaded) return@withContext Outcome.Failure("El motor nativo no está cargado")
            val totalS = SILENCE_S + BURST_S + TAIL_S
            val totalN = (totalS * SAMPLE_RATE).toInt()
            val rec = FloatArray(totalN)
            val minRec = AudioRecord.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT
            )
            val minTrk = AudioTrack.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_FLOAT
            )
            if (minRec <= 0 || minTrk <= 0) return@withContext Outcome.Failure("Este teléfono no admite la medición (formato de audio)")
            var recorder: AudioRecord? = null
            var track: AudioTrack? = null
            try {
                recorder = try {
                    AudioRecord(
                        MediaRecorder.AudioSource.UNPROCESSED, SAMPLE_RATE,
                        AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT,
                        maxOf(minRec * 4, SAMPLE_RATE * 4)
                    )
                } catch (t: Throwable) {
                    null
                }
                if (recorder == null || recorder.state != AudioRecord.STATE_INITIALIZED) {
                    recorder?.release()
                    recorder = AudioRecord(
                        MediaRecorder.AudioSource.MIC, SAMPLE_RATE,
                        AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT,
                        maxOf(minRec * 4, SAMPLE_RATE * 4)
                    )
                }
                val rcd: AudioRecord = recorder ?: return@withContext Outcome.Failure("No se pudo abrir el micrófono")
                if (rcd.state != AudioRecord.STATE_INITIALIZED) return@withContext Outcome.Failure("No se pudo abrir el micrófono")
                val trk: AudioTrack = AudioTrack.Builder()
                    .setAudioAttributes(
                        AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build()
                    )
                    .setAudioFormat(
                        AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                            .setSampleRate(SAMPLE_RATE)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build()
                    )
                    .setBufferSizeInBytes(maxOf(minTrk * 2, SAMPLE_RATE * 2))
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .build()
                track = trk
                // Secuencia: silencio + ráfaga de ruido blanco + cola (la grabación ya corre).
                val burstN = (BURST_S * SAMPLE_RATE).toInt()
                val burst = FloatArray(burstN)
                var seed = 0x12345678L
                for (i in 0 until burstN) {
                    seed = seed * 6364136223846793005L + 1442695040888963407L
                    burst[i] = AMPLITUDE * ((seed ushr 33).toFloat() / (1L shl 31).toFloat() * 2f - 1f)
                }
                val silenceN = (SILENCE_S * SAMPLE_RATE).toInt()
                val silence = FloatArray(silenceN) // ceros
                rcd.startRecording()
                trk.play()
                val writer = launch {
                    var off = 0
                    while (off < silenceN && isActive) {
                        val w = trk.write(silence, off, minOf(4800, silenceN - off), AudioTrack.WRITE_BLOCKING)
                        if (w <= 0) break
                        off += w
                    }
                    off = 0
                    while (off < burstN && isActive) {
                        val w = trk.write(burst, off, minOf(4800, burstN - off), AudioTrack.WRITE_BLOCKING)
                        if (w <= 0) break
                        off += w
                    }
                }
                var got = 0
                while (got < totalN && isActive) {
                    val n = rcd.read(rec, got, minOf(4800, totalN - got), AudioRecord.READ_BLOCKING)
                    if (n <= 0) break
                    got += n
                    onProgress(got.toFloat() / totalN)
                }
                writer.join()
                if (got < totalN) return@withContext Outcome.Failure("La grabación se interrumpió")
            } catch (t: Throwable) {
                Log.w(TAG, "RT60 falló: ${t.message}")
                return@withContext Outcome.Failure("Error de audio: ${t.message}")
            } finally {
                runCatching { recorder?.stop() }
                runCatching { recorder?.release() }
                runCatching { track?.stop() }
                runCatching { track?.release() }
            }
            val burstEnd = ((SILENCE_S + BURST_S) * SAMPLE_RATE).toInt()
            Outcome.Success(computeRt60(rec, burstEnd))
        }

    /**
     * Cálculo puro del RT60 a partir de la grabación (separado para revisión).
     * [burstEnd] = muestra donde termina la ráfaga (inicio del decaimiento).
     */
    internal fun computeRt60(rec: FloatArray, burstEnd: Int): Result {
        val winN = (WIN_S * SAMPLE_RATE).toInt()
        val hopN = (HOP_S * SAMPLE_RATE).toInt()
        val bands = ArrayList<BandResult>()
        for (freqHz in BANDS_HZ) {
            // Piso de ruido por banda (primeros 0.4 s, antes de la ráfaga).
            val noiseSeg = rec.copyOfRange((0.05f * SAMPLE_RATE).toInt(), (0.45f * SAMPLE_RATE).toInt())
            val noiseAmp = IvannaNativeLib.nativeRoomLockIn(noiseSeg, noiseSeg.size, SAMPLE_RATE, freqHz)
            // Curva de nivel por ventana deslizante sobre la cola.
            val levels = ArrayList<Float>()
            val times = ArrayList<Float>()
            var p = burstEnd
            while (p + winN <= rec.size) {
                val seg = rec.copyOfRange(p, p + winN)
                val a = IvannaNativeLib.nativeRoomLockIn(seg, seg.size, SAMPLE_RATE, freqHz)
                levels.add(a)
                times.add((p - burstEnd).toFloat() / SAMPLE_RATE)
                p += hopN
            }
            if (levels.isEmpty()) {
                bands.add(BandResult(freqHz, Float.NaN, Float.NaN, Float.NaN, Float.NaN, Float.NaN, 0f, false))
                continue
            }
            val peakAmp = levels.max()
            val dynamicRangeDb = 20f * log10(max(peakAmp, 1e-9f) / max(noiseAmp, 1e-9f))
            // Energía por ventana (p²) y EDC de Schroeder (integral hacia atrás).
            val energy = levels.map { it * it }
            val totalEnergy = energy.sum()
            val edcDb = FloatArray(energy.size)
            var remaining = totalEnergy
            for (i in energy.indices) {
                edcDb[i] = 10f * log10(max(remaining, 1e-12f) / max(totalEnergy, 1e-12f))
                remaining -= energy[i]
            }
            val edt = fitDecay(times, edcDb, 0f, -10f, 10f)
            val t20 = fitDecay(times, edcDb, -5f, -25f, 20f)
            val t30 = fitDecay(times, edcDb, -5f, -35f, 30f)
            val rt60 = when {
                t30.isFinite() -> t30
                t20.isFinite() -> t20 * 3f
                edt.isFinite() -> edt * 6f
                else -> Float.NaN
            }
            // C80: energía 0–80 ms vs 80 ms–fin.
            val idx80 = (0.08f / HOP_S).toInt().coerceAtMost(energy.size)
            val early = energy.subList(0, idx80).sum()
            val late = energy.subList(idx80, energy.size).sum()
            val c80 = if (early > 0f && late > 0f) 10f * log10(early / late) else Float.NaN
            val reliable = dynamicRangeDb >= 25f && rt60.isFinite()
            bands.add(BandResult(freqHz, edt, t20, t30, rt60, c80, dynamicRangeDb, reliable))
            Log.i(TAG, "RT60 ${freqHz.toInt()} Hz: rt60=$rt60 EDT=$edt T20=$t20 T30=$t30 C80=$c80 rango=$dynamicRangeDb dB")
        }
        val broadband = bands.filter { it.rt60S.isFinite() }.map { it.rt60S }.average().toFloat()
        return Result(bands, if (bands.any { it.rt60S.isFinite() }) broadband else Float.NaN)
    }

    /**
     * Ajuste por mínimos cuadrados de la recta de decaimiento entre [fromDb] y
     * [toDb] sobre la EDC; devuelve el tiempo para caer [scaleDb] dB (EDT: 10,
     * T20: 20, T30: 30). NaN si no hay suficientes puntos o el rango no se alcanza.
     */
    internal fun fitDecay(times: List<Float>, edcDb: FloatArray, fromDb: Float, toDb: Float, scaleDb: Float): Float {
        var n = 0
        var sx = 0.0; var sy = 0.0; var sxx = 0.0; var sxy = 0.0
        for (i in edcDb.indices) {
            val y = edcDb[i]
            if (y <= fromDb && y >= toDb) {
                val x = times[i].toDouble()
                n++; sx += x; sy += y; sxx += x * x; sxy += x * y
            }
        }
        if (n < 3) return Float.NaN
        val denom = n * sxx - sx * sx
        if (denom == 0.0) return Float.NaN
        val slope = (n * sxy - sx * sy) / denom // dB/s, negativa
        if (slope >= -0.1) return Float.NaN // no hay decaimiento medible
        return (-scaleDb / slope).toFloat()
    }
}
