package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.safmus.SafmusUniversalKernel
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sin
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * RoomMeasurement — mide la respuesta de sala + bocinas con el micrófono del teléfono y diseña la corrección.
 *
 * Secuencia (una sola reproducción continua, una sola grabación continua):
 *   1.0 s silencio                → piso de ruido por frecuencia
 *   0.3 s ráfaga de 1 kHz         → sincronía: mide la LATENCIA REAL de la salida (cable, USB o Bluetooth), por lo
 *                                   que no hay que adivinarla; los tonos se localizan a partir de ahí
 *   27 tonos de 0.9 s (+0.1 s)    → rejilla de 1/3 de octava, 25 Hz–12.7 kHz, amplitud fija −14 dBFS
 * Para cada tono se analiza la ventana estable (0.3–0.8 s) con detección síncrona nativa (RoomProbe::lockInAmplitude)
 * y el diseño de la corrección también es nativo y probado en el host (RoomProbe::designCorrection).
 *
 * Límites honestos: el micrófono de un teléfono no está calibrado; se corrige el balance tonal amplio entre 50 Hz y
 * 6 kHz, no modos de sala estrechos ni la fase, y se mide el punto donde está el teléfono. Dura ~35 s.
 */
object RoomMeasurement {
    private const val TAG = "RoomMeasurement"
    const val SAMPLE_RATE = 48_000
    const val POINTS = 27
    private const val AMPLITUDE = 0.2f
    private const val SILENCE_S = 1.0f
    private const val BURST_S = 0.3f
    private const val BURST_GAP_S = 0.5f
    private const val TONE_S = 0.9f
    private const val GAP_S = 0.1f
    private const val TAIL_S = 2.0f            // margen para la latencia de la salida (Bluetooth)
    private const val WIN_FROM_S = 0.3f
    private const val WIN_TO_S = 0.8f

    sealed class Outcome {
        class Success(val gainsDb: FloatArray, val levelsDb: FloatArray, val snrDb: FloatArray, val latencyMs: Int) : Outcome()
        class Failure(val reason: String) : Outcome()
    }

    fun probeHz(i: Int): Float = (25.0 * 2.0.pow(i / 3.0)).toFloat()

    private fun s2n(sec: Float): Int = (sec * SAMPLE_RATE).toInt()

    private fun buildSequence(): FloatArray {
        val total = s2n(SILENCE_S) + s2n(BURST_S) + s2n(BURST_GAP_S) + POINTS * (s2n(TONE_S) + s2n(GAP_S))
        val out = FloatArray(total)
        fun tone(start: Int, len: Int, f: Float) {
            val fade = s2n(0.02f)
            for (i in 0 until len) {
                val env = when {
                    i < fade -> i / fade.toFloat()
                    i > len - fade -> (len - i) / fade.toFloat()
                    else -> 1f
                }
                out[start + i] = AMPLITUDE * env * sin(2.0 * PI * f * i / SAMPLE_RATE).toFloat()
            }
        }
        tone(s2n(SILENCE_S), s2n(BURST_S), 1000f)
        var pos = s2n(SILENCE_S) + s2n(BURST_S) + s2n(BURST_GAP_S)
        for (k in 0 until POINTS) { tone(pos, s2n(TONE_S), probeHz(k)); pos += s2n(TONE_S) + s2n(GAP_S) }
        return out
    }

    private fun db(x: Float): Float = 20f * log10(max(x, 1e-9f))

    /** Ejecuta la medición completa. El llamador debe tener RECORD_AUDIO concedido. */
    suspend fun run(context: Context, onProgress: (Float) -> Unit): Outcome = withContext(Dispatchers.IO) {
        if (!IvannaNativeLib.isLoaded) return@withContext Outcome.Failure("El motor nativo no está cargado")
        val seq = buildSequence()
        val rec = FloatArray(seq.size + s2n(TAIL_S))
        val minRec = AudioRecord.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT)
        val minTrk = AudioTrack.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_FLOAT)
        if (minRec <= 0 || minTrk <= 0) return@withContext Outcome.Failure("Este teléfono no admite la medición (formato de audio)")

        var recorder: AudioRecord? = null
        var track: AudioTrack? = null
        try {
            recorder = try {
                AudioRecord(MediaRecorder.AudioSource.UNPROCESSED, SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_FLOAT, max(minRec * 4, SAMPLE_RATE * 4))
            } catch (t: Throwable) { null }
            if (recorder == null || recorder.state != AudioRecord.STATE_INITIALIZED) {
                recorder?.release()
                recorder = AudioRecord(MediaRecorder.AudioSource.MIC, SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_FLOAT, max(minRec * 4, SAMPLE_RATE * 4))
            }
            val rcd: AudioRecord = recorder ?: return@withContext Outcome.Failure("No se pudo abrir el micrófono")
            if (rcd.state != AudioRecord.STATE_INITIALIZED) return@withContext Outcome.Failure("No se pudo abrir el micrófono")

            val trk: AudioTrack = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setSampleRate(SAMPLE_RATE)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(max(minTrk * 2, SAMPLE_RATE * 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
            track = trk
            rcd.startRecording()
            trk.play()
            val writer = launch {
                var off = 0
                while (off < seq.size && isActive) {
                    val n = trk.write(seq, off, min(4800, seq.size - off), AudioTrack.WRITE_BLOCKING)
                    if (n <= 0) break
                    off += n
                }
            }
            var got = 0
            while (got < rec.size && isActive) {
                val n = rcd.read(rec, got, min(4800, rec.size - got), AudioRecord.READ_BLOCKING)
                if (n <= 0) break
                got += n
                onProgress(got.toFloat() / rec.size)
            }
            writer.join()
            if (got < rec.size) return@withContext Outcome.Failure("La grabación se interrumpió")
        } catch (t: Throwable) {
            Log.w(TAG, "medición falló: ${t.message}")
            return@withContext Outcome.Failure("Error de audio: ${t.message}")
        } finally {
            runCatching { recorder?.stop() }; runCatching { recorder?.release() }
            runCatching { track?.stop() }; runCatching { track?.release() }
        }
        analyze(rec)
    }


    sealed class VerifiedOutcome {
        data class Success(val gainsDb: FloatArray, val latencyMs: Int, val beforeErrorDb: Float,
                           val afterErrorDb: Float, val decisionId: Long) : VerifiedOutcome()
        data class Failure(val reason: String, val rolledBack: Boolean, val decisionId: Long) : VerifiedOutcome()
    }

    private var nextDecisionId = 1L

    private fun auditDecision(context: Context, id: Long, decision: String, reason: String,
                              beforeDb: Float? = null, afterDb: Float? = null, rolledBack: Boolean) {
        runCatching {
            val prefs = context.getSharedPreferences("ivanna_agent_audit", Context.MODE_PRIVATE)
            val old = runCatching { org.json.JSONArray(prefs.getString("records", "[]")) }.getOrElse { org.json.JSONArray() }
            val entry = org.json.JSONObject()
                .put("id", id).put("timeUtcMs", System.currentTimeMillis())
                .put("decision", decision).put("reason", reason).put("rolledBack", rolledBack)
            if (beforeDb != null && beforeDb.isFinite()) entry.put("beforeErrorDb", beforeDb.toDouble())
            if (afterDb != null && afterDb.isFinite()) entry.put("afterErrorDb", afterDb.toDouble())
            old.put(entry)
            val bounded = org.json.JSONArray()
            for (i in (old.length() - 50).coerceAtLeast(0) until old.length()) bounded.put(old.getJSONObject(i))
            prefs.edit().putString("records", bounded.toString()).apply()
        }
    }

    /**
     * Ciclo cerrado real: mide sin corrección, aplica el perfil propuesto, vuelve a medir y compara.
     * La aplicación/rollback se delega a la API nativa existente. Nunca persiste el perfil aquí.
     * Ejecutar desde una corrutina de trabajo; cada transacción puede durar hasta ~70 s.
     */
    suspend fun runVerified(
        context: Context,
        previousGains: FloatArray?,
        previousEnabled: Boolean,
        applyCorrection: (FloatArray?, Boolean) -> Unit,
        onProgress: (Float) -> Unit
    ): VerifiedOutcome {
        val id = synchronized(this) { nextDecisionId++ }
        applyCorrection(null, false)
        kotlinx.coroutines.delay(800)
        val baseline = when (val result = run(context) { onProgress(it * 0.45f) }) {
            is Outcome.Success -> result
            is Outcome.Failure -> {
                applyCorrection(previousGains, previousEnabled)
                auditDecision(context, id, "ROLLED_BACK", "baseline:${result.reason}", rolledBack = true)
                return VerifiedOutcome.Failure("baseline:${result.reason}", true, id)
            }
        }
        val beforeError = profileError(baseline.levelsDb, baseline.snrDb)
        if (!beforeError.isFinite()) {
            applyCorrection(previousGains, previousEnabled)
            auditDecision(context, id, "ROLLED_BACK", "baseline_profile_unreliable", beforeDb = beforeError, rolledBack = true)
            return VerifiedOutcome.Failure("baseline_profile_unreliable", true, id)
        }
        val measuredEvidence = baseline.levelsDb.size == POINTS &&
            baseline.snrDb.size == POINTS && beforeError.isFinite() &&
            baseline.latencyMs in -50..(TAIL_S * 1000f).toInt()
        val candidateSafe = baseline.gainsDb.size == 10 && baseline.gainsDb.all {
            it.isFinite() && it in -9.0f..3.0f
        }
        // RoomCorrectionEq now sanitizes the active filter input and applies a linked
        // post-EQ ceiling. This is a guaranteed headroom guard, not a calibrated SPL claim.
        val roomGate = SafmusUniversalKernel.gateRoomCorrection(
            measurementAvailable = measuredEvidence,
            playbackChainIdentified = baseline.latencyMs in -50..(TAIL_S * 1000f).toInt(),
            candidateFiniteAndStable = candidateSafe,
            headroomValid = candidateSafe
        )
        if (!roomGate.allowed) {
            applyCorrection(previousGains, previousEnabled)
            val reason = "safmus_room_gate:${roomGate.reason}"
            auditDecision(context, id, "ROLLED_BACK", reason, beforeDb = beforeError, rolledBack = true)
            return VerifiedOutcome.Failure(reason, true, id)
        }
        applyCorrection(baseline.gainsDb, true)
        kotlinx.coroutines.delay(800)
        val verified = when (val result = run(context) { onProgress(0.45f + it * 0.55f) }) {
            is Outcome.Success -> result
            is Outcome.Failure -> {
                applyCorrection(previousGains, previousEnabled)
                auditDecision(context, id, "ROLLED_BACK", "verification:${result.reason}", beforeDb = beforeError, rolledBack = true)
                return VerifiedOutcome.Failure("verification:${result.reason}", true, id)
            }
        }
        val afterError = profileError(verified.levelsDb, verified.snrDb)
        // Reject unstable/weak measurements and revert if the measured profile did not improve.
        if (!afterError.isFinite() || beforeError - afterError < 0.25f) {
            applyCorrection(previousGains, previousEnabled)
            val reason = if (!afterError.isFinite()) "verification_profile_unreliable"
                else "no_verified_improvement: before=${"%.2f".format(java.util.Locale.ROOT, beforeError)}dB after=${"%.2f".format(java.util.Locale.ROOT, afterError)}dB"
            auditDecision(context, id, "ROLLED_BACK", reason, beforeError, afterError, true)
            return VerifiedOutcome.Failure(reason, true, id)
        }
        auditDecision(context, id, "COMMITTED", "verified_improvement", beforeError, afterError, false)
        return VerifiedOutcome.Success(baseline.gainsDb, baseline.latencyMs, beforeError, afterError, id)
    }

    /** RMS residual from the 250 Hz–2 kHz reference, only reliable points between 50 Hz and 6 kHz. */
    internal fun profileError(levels: FloatArray, snr: FloatArray): Float {
        if (levels.size != POINTS || snr.size != POINTS) return Float.NaN
        var refSum = 0.0; var refN = 0
        for (i in 0 until POINTS) {
            val hz = probeHz(i)
            if (hz in 250f..2000f && levels[i].isFinite() && snr[i].isFinite() && snr[i] >= 10f) {
                refSum += levels[i]; refN++
            }
        }
        if (refN < 3) return Float.NaN
        val reference = (refSum / refN).toFloat()
        var sumSq = 0.0; var n = 0
        for (i in 0 until POINTS) {
            val hz = probeHz(i)
            if (hz in 50f..6000f && levels[i].isFinite() && snr[i].isFinite() && snr[i] >= 10f) {
                val d = levels[i] - reference
                sumSq += d * d; n++
            }
        }
        return if (n >= 5) kotlin.math.sqrt(sumSq / n).toFloat() else Float.NaN
    }

    /** Análisis puro de la grabación (separado para poder revisarlo/probarlo): latencia, ruido, niveles, diseño. */
    internal fun analyze(rec: FloatArray): Outcome {
        var peak = 0f
        for (v in rec) peak = max(peak, abs(v))
        if (peak >= 0.98f) return Outcome.Failure("El micrófono saturó: baja el volumen y repite")

        // 1) Ráfaga de sincronía de 1 kHz: busca su inicio con ventanas de 20 ms (paso 10 ms).
        val win = s2n(0.02f); val hop = s2n(0.01f)
        val noiseWin = rec.copyOfRange(s2n(0.1f), s2n(0.9f))
        val noise1k = IvannaNativeLib.nativeRoomLockIn(noiseWin, noiseWin.size, SAMPLE_RATE, 1000f)
        var maxLevel = 0f
        val env = ArrayList<Float>()
        var p = 0
        while (p + win <= rec.size - s2n(0.3f)) {
            val seg = rec.copyOfRange(p, p + win)
            val a = IvannaNativeLib.nativeRoomLockIn(seg, seg.size, SAMPLE_RATE, 1000f)
            env.add(a); maxLevel = max(maxLevel, a)
            p += hop
        }
        if (maxLevel < 1e-4f || db(maxLevel) - db(noise1k) < 12f)
            return Outcome.Failure("No se oyó la señal de prueba: sube un poco el volumen o acerca el teléfono a las bocinas")
        val thr = max(noise1k * 4f, maxLevel * 0.3f)
        val onsetIdx = env.indexOfFirst { it >= thr }
        if (onsetIdx < 0) return Outcome.Failure("No se detectó el inicio de la señal de prueba")
        val onset = onsetIdx * hop                         // muestra de la grabación donde empieza la ráfaga
        val shift = onset - s2n(SILENCE_S)                 // retardo salida→micrófono (latencia real + sala)
        val latencyMs = (shift * 1000L / SAMPLE_RATE).toInt()
        if (shift < -s2n(0.05f) || shift > s2n(TAIL_S)) return Outcome.Failure("Latencia fuera de rango ($latencyMs ms)")

        // 2) Piso de ruido por frecuencia y nivel de cada tono (ventana estable 0.3–0.8 s)
        val levels = FloatArray(POINTS); val snr = FloatArray(POINTS)
        var pos = s2n(SILENCE_S) + s2n(BURST_S) + s2n(BURST_GAP_S)
        for (k in 0 until POINTS) {
            val f = probeHz(k)
            val noiseK = IvannaNativeLib.nativeRoomLockIn(noiseWin, noiseWin.size, SAMPLE_RATE, f)
            val from = pos + shift + s2n(WIN_FROM_S)
            val to = pos + shift + s2n(WIN_TO_S)
            if (to > rec.size) return Outcome.Failure("La grabación quedó corta")
            val seg = rec.copyOfRange(from, to)
            val a = IvannaNativeLib.nativeRoomLockIn(seg, seg.size, SAMPLE_RATE, f)
            levels[k] = db(a)
            snr[k] = db(a) - db(noiseK)
            pos += s2n(TONE_S) + s2n(GAP_S)
        }
        val gains = IvannaNativeLib.nativeRoomDesign(levels, snr, SAMPLE_RATE)
        if (gains.size != 10) return Outcome.Failure("Sin referencia fiable (¿mucho ruido ambiente o volumen muy bajo?)")
        return Outcome.Success(gains, levels, snr, latencyMs)
    }
}
