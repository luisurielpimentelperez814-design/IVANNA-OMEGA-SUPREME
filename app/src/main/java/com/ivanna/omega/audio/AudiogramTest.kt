package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import kotlin.math.PI
import kotlin.math.sin

/**
 * AudiogramTest — medición del audiograma de Esse con el teléfono.
 *
 * Cierra el hueco 1 del Knowledge Core. El motor que CONSUME el audiograma ya
 * existe (`HearingAdaptationEngine::setAudiogram`, 6 frecuencias: 250/500/1k/
 * 2k/4k/8k Hz + agregados por banda) y la vía Kotlin para aplicarlo también
 * (`NativeBridge.safeSetHearingAdaptationParams`); lo que faltaba era la
 * MEDICIÓN. Este archivo la implementa.
 *
 * Método: Hughson-Westlake (el estándar clínico, adaptado al teléfono).
 * Por frecuencia: se presenta un tono puro de 1 s; si el usuario lo oye se
 * baja 10 dB, si no lo oye se sube 5 dB. El umbral es el nivel más bajo con
 * al menos 2 respuestas positivas en 3 ascensos. El test se detiene por
 * frecuencia tras 3 reversiones o 8 presentaciones.
 *
 * LÍMITE HONESTO (leer antes de interpretar los números):
 * - Los umbrales son RELATIVOS (dB respecto a la salida máxima del teléfono
 *   al volumen de prueba), no dB HL clínicos: el micrófono y la bocina del
 *   teléfono no están calibrados en absoluto. La pérdida se expresa como
 *   diferencia respecto a la mejor frecuencia del propio usuario (su mejor
 *   oído = 0 dB de pérdida). Eso es EXACTAMENTE lo que el motor necesita:
 *   `HearingAdaptationEngine` aplica ganancias RELATIVAS por banda.
 * - Vía preferida: auriculares USB/cable (se salta la sala). Vía alternativa:
 *   bocina a 50 cm en habitación silenciosa, corrigiendo la respuesta de
 *   bocina+sala con los gains de `RoomMeasurement` (misma sesión).
 * - Seguridad: el nivel máximo de presentación es −14 dBFS (amplitud 0.2f,
 *   igual que `RoomMeasurement`), tonos de 1 s, y el perfil del usuario capa
 *   la escucha segura a 85 dB SPL. Nunca se presenta por encima de ese nivel.
 *
 * Flujo de uso (la UI solo aporta dos cosas): un botón "Lo oigo" y un botón
 * "No lo oigo" (o timeout = no oído). El test es una suspend function que
 * pide la respuesta al llamador por frecuencia, así la lógica es pura y
 * testeable sin Android.
 *
 * Verificación: la compilación Kotlin la debe verificar la CI (sin kotlinc
 * en la VM). La lógica sigue los patrones probados de `RoomMeasurement.kt`
 * (AudioTrack PCM_FLOAT 48 kHz, `WRITE_BLOCKING`, `Dispatchers.IO`).
 */
object AudiogramTest {
    private const val TAG = "AudiogramTest"
    const val SAMPLE_RATE = 48_000

    /** Frecuencias del audiograma (las mismas de `AudiogramProfile`). */
    val FREQUENCIES_HZ = floatArrayOf(250f, 500f, 1000f, 2000f, 4000f, 8000f)

    /** Nivel de presentación máximo: −14 dBFS (amplitud 0.2f, como RoomMeasurement). */
    private const val MAX_AMPLITUDE = 0.2f
    /** Nivel inicial de cada frecuencia (claramente audible): −30 dB rel al máximo. */
    private const val START_LEVEL_DB = -30f
    /** Paso descendente (oído) y ascendente (no oído) — Hughson-Westlake. */
    private const val STEP_DOWN_DB = 10f
    private const val STEP_UP_DB = 5f
    private const val MAX_PRESENTATIONS = 8
    private const val TONE_S = 1.0f

    /** Umbral por frecuencia, en dB relativos al nivel máximo de presentación. */
    data class Result(
        val thresholdsDb: FloatArray, // [250, 500, 1000, 2000, 4000, 8000], dB rel
        val lossLowDb: Float,         // media(250, 500) − mejor frecuencia
        val lossMidDb: Float,         // media(1000, 2000) − mejor frecuencia
        val lossHighDb: Float,        // 4000 − mejor frecuencia
        val lossUltraHighDb: Float    // 8000 − mejor frecuencia
    ) {
        /** Aplica el resultado al motor nativo (vía segura, no-op si no cargado). */
        fun applyToEngine() {
            NativeBridge.safeSetHearingAdaptationParams(
                earTipSeal = 1.0f,
                listeningSplDb = 75.0f,
                lossLowDb = lossLowDb,
                lossMidDb = lossMidDb,
                lossHighDb = lossHighDb,
                lossUltraHighDb = lossUltraHighDb
            )
            Log.i(TAG, "audiograma aplicado: low=$lossLowDb mid=$lossMidDb high=$lossHighDb ultra=$lossUltraHighDb")
        }
    }

    /**
     * Escalera Hughson-Westlake pura (sin dependencias Android).
     * `levelDb` es relativo al máximo (0 = nivel máximo, negativo = más bajo).
     */
    class HughsonWestlake(private val startDb: Float = START_LEVEL_DB) {
        private var levelDb: Float = startDb
        private var reversals = 0
        private var lastDirection = 0 // +1 subiendo, −1 bajando
        private var presentations = 0
        private val ascentLevels = ArrayList<Float>() // niveles donde se oyó en ascenso
        private var heardAtCurrent = false

        fun nextLevelDb(): Float = levelDb
        fun presentations(): Int = presentations

        fun recordResponse(heard: Boolean) {
            presentations++
            val direction = if (heard) -1 else +1
            if (lastDirection != 0 && direction != lastDirection) {
                reversals++
                if (direction == +1) ascentLevels.add(levelDb)
            }
            lastDirection = direction
            levelDb = (levelDb + if (heard) -STEP_DOWN_DB else STEP_UP_DB)
                .coerceIn(START_LEVEL_DB - 60f, 0f)
            heardAtCurrent = heard
        }

        fun isDone(): Boolean = reversals >= 3 || presentations >= MAX_PRESENTATIONS

        /** Umbral: el nivel más bajo oído en al menos 2 ascensos; si no, el último nivel oído. */
        fun thresholdDb(): Float {
            if (ascentLevels.size >= 2) return ascentLevels.min()
            if (ascentLevels.size == 1) return ascentLevels[0]
            return levelDb // sin ninguna respuesta positiva: se devuelve el último nivel (no fiable)
        }

        fun reliable(): Boolean = ascentLevels.size >= 2
    }

    /**
     * Ejecuta el audiograma completo.
     *
     * @param context contexto para abrir el AudioTrack.
     * @param awaitResponse suspende hasta que el usuario responda si oyó el
     *   tono (`true`) o no (`false`); un timeout del llamador debe devolver `false`.
     * @param onProgress progreso 0..1 (frecuencia actual / 6).
     * @return [Result] con los umbrales y las pérdidas por banda, o `null` si
     *   el AudioTrack no se pudo abrir.
     */
    suspend fun run(
        context: Context,
        awaitResponse: suspend (freqHz: Float, levelDb: Float) -> Boolean,
        onProgress: (Float) -> Unit = {}
    ): Result? = withContext(Dispatchers.IO) {
        if (!IvannaNativeLib.isLoaded) {
            Log.w(TAG, "motor nativo no cargado; el test puede correr pero no se aplicará")
        }
        val minTrk = AudioTrack.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_FLOAT
        )
        if (minTrk <= 0) return@withContext null
        var track: AudioTrack? = null
        try {
            track = AudioTrack.Builder()
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
            if (track.state != AudioTrack.STATE_INITIALIZED) return@withContext null
            track.play()
            val thresholds = FloatArray(FREQUENCIES_HZ.size)
            for ((fi, freqHz) in FREQUENCIES_HZ.withIndex()) {
                val stair = HughsonWestlake()
                while (!stair.isDone() && isActive) {
                    val levelDb = stair.nextLevelDb()
                    val amp = (MAX_AMPLITUDE * Math.pow(10.0, levelDb.toDouble() / 20.0)).toFloat()
                    playTone(track, freqHz, amp)
                    val heard = awaitResponse(freqHz, levelDb)
                    stair.recordResponse(heard)
                }
                thresholds[fi] = stair.thresholdDb()
                Log.i(TAG, "umbral ${freqHz.toInt()} Hz = ${thresholds[fi]} dB rel (fiable=${stair.reliable()})")
                onProgress((fi + 1).toFloat() / FREQUENCIES_HZ.size)
            }
            buildResult(thresholds)
        } catch (t: Throwable) {
            Log.w(TAG, "audiograma falló: ${t.message}")
            null
        } finally {
            runCatching { track?.stop() }
            runCatching { track?.release() }
        }
    }

    /** Pérdidas relativas por banda a partir de los umbrales por frecuencia. */
    internal fun buildResult(thresholdsDb: FloatArray): Result {
        require(thresholdsDb.size == FREQUENCIES_HZ.size)
        val best = thresholdsDb.min() // la mejor frecuencia del usuario = 0 dB de pérdida
        val loss = FloatArray(thresholdsDb.size) { (thresholdsDb[it] - best).coerceAtLeast(0f) }
        return Result(
            thresholdsDb = thresholdsDb,
            lossLowDb = (loss[0] + loss[1]) / 2f,
            lossMidDb = (loss[2] + loss[3]) / 2f,
            lossHighDb = loss[4],
            lossUltraHighDb = loss[5]
        )
    }

    /** Tono puro de 1 s con rampas de 20 ms (sin clics) a la amplitud dada. */
    private fun playTone(track: AudioTrack, freqHz: Float, amplitude: Float) {
        val n = (SAMPLE_RATE * TONE_S).toInt()
        val buf = FloatArray(n)
        val ramp = (SAMPLE_RATE * 0.02f).toInt()
        for (i in 0 until n) {
            val env = when {
                i < ramp -> i.toFloat() / ramp
                i >= n - ramp -> (n - i).toFloat() / ramp
                else -> 1f
            }
            buf[i] = amplitude * env * sin(2.0 * PI * freqHz * i / SAMPLE_RATE).toFloat()
        }
        var off = 0
        while (off < n) {
            val w = track.write(buf, off, minOf(4800, n - off), AudioTrack.WRITE_BLOCKING)
            if (w <= 0) break
            off += w
        }
    }
}
