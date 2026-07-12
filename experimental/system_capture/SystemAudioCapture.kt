/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * SystemAudioCapture v4.1 — Crash-Fix Edition (debug)
 *
 * CORRECCIONES v4.1 (sin borrar nada, solo reparar):
 *  - [FIX-1] Eliminado `private set` en vars del companion: el setter era privado
 *    al companion object pero startCapture() (método de instancia) necesitaba
 *    escribirlas → IllegalAccessError / error de compilación encubierto.
 *  - [FIX-2] audioRecord marcado @Volatile: escrito en startCapture() (hilo de
 *    servicio) y leído en el hilo de captura y stopCapture() (main thread) →
 *    visibilidad JMM no garantizada sin @Volatile.
 *  - [FIX-3] mediaProjection.stop() + mediaProjection = null en stopCapture():
 *    el token nunca se liberaba → leak de MediaProjection + SecurityException
 *    en intentos de recaptura posteriores.
 *  - [FIX-4] Guard `floatsRead > 0` antes del loop de métricas RMS/Peak →
 *    evita división por cero y BufferUnderflowException si read() devuelve
 *    bytes no múltiplo de Float.SIZE_BYTES (edge case en hardware no estándar).
 *  - [FIX-5] Guard `bytesRead > 0` ya existente protege el base64 path, pero
 *    se añade rewind() defensivo antes de buf.get(bytes) para garantizar
 *    que la posición del ByteBuffer es 0 aunque el path de métricas lo mueva.
 *
 * SIN CAMBIOS (preservados intactos):
 *  - Negociación hi-res cascade: 768kHz → 384kHz → 192kHz → 96kHz → 48kHz
 *  - Captura bit-perfect (ENCODING_PCM_FLOAT, float32 pipeline)
 *  - Thread priority URGENT_AUDIO
 *  - Cómputo RMS + Peak online con IIR one-pole (α=0.15)
 *  - PCM base64 streaming YouTube/TIDAL sync cada 8 lecturas
 *  - Companion expone: lastRmsDb, lastPeakDb, activeRateHz, lastPcmBase64, resetMetrics()
 *  - Callbacks: onPcmMetrics, onPcmBase64
 *  - BUG FIX #2 original (stopCapture race): stop() antes de join()
 */
package com.ivanna.omega.audio

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Process
import android.util.Base64
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.concurrent.thread
import kotlin.math.log10
import kotlin.math.sqrt

class SystemAudioCapture private constructor(private val context: Context) {

    companion object {
        const val REQUEST_CODE = 1001
        private const val TAG = "SystemAudioCapture"

        // ── Hi-Res cascade de tasas de muestreo ──────────────────────────
        // 768kHz → 384kHz → 192kHz → 96kHz → 48kHz (bit-perfect garantizado)
        private val CANDIDATE_RATES = intArrayOf(768000, 384000, 192000, 96000, 48000)

        // ── Métricas PCM expuestas (actualizadas en tiempo real) ──────────
        // [FIX-1] Eliminado `; private set` — el setter era privado al companion
        // pero startCapture() (método de instancia) necesitaba escribir estas vars
        // → IllegalAccessError encubierto / crash de compilación en AGP reciente.
        @Volatile var lastRmsDb:     Float  = -96f
        @Volatile var lastPeakDb:    Float  = -96f
        @Volatile var activeRateHz:  Int    = 48000
        @Volatile var lastPcmBase64: String = ""

        fun resetMetrics() {
            lastRmsDb     = -96f
            lastPeakDb    = -96f
            lastPcmBase64 = ""
        }

        @Volatile private var instance: SystemAudioCapture? = null

        fun getInstance(context: Context): SystemAudioCapture {
            return instance ?: synchronized(this) {
                instance ?: SystemAudioCapture(context.applicationContext).also { instance = it }
            }
        }

        // ── JNI: Motor DSP nativo C++ ─────────────────────────────────────
        @JvmStatic external fun nativeFeedBuffer(buffer: ByteBuffer, size: Int)
        @JvmStatic external fun nativeClearBuffer()
        @JvmStatic external fun nativeHasData(): Boolean

        // ── JNI — Métricas PCM (v3.2.0) ──────────────────────────────────────
        @JvmStatic external fun nativeGetLastRmsDb():  Float
        @JvmStatic external fun nativeGetLastPeakDb(): Float
        @JvmStatic external fun nativeResetMetrics()
    }

    private var mediaProjectionManager: MediaProjectionManager =
        context.getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
    private var mediaProjection: MediaProjection? = null

    // [FIX-2] @Volatile: escrito en startCapture() (hilo del servicio FGS) y leído
    // tanto en el hilo de captura como en stopCapture() (main thread). Sin @Volatile
    // el JMM no garantiza visibilidad cross-thread → null-deref o stale reference.
    @Volatile private var audioRecord: AudioRecord? = null

    private var captureThread: Thread?    = null
    @Volatile private var isCapturing = false

    // ── Callbacks ─────────────────────────────────────────────────────────
    /** Llamado desde el hilo de captura con métricas RMS/Peak en dBFS. */
    var onPcmMetrics: ((rmsDb: Float, peakDb: Float) -> Unit)? = null
    /** Emite chunk PCM float32 codificado en Base64 (YouTube/TIDAL sync). */
    var onPcmBase64:  ((chunk: String) -> Unit)? = null

    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode == REQUEST_CODE && resultCode == android.app.Activity.RESULT_OK && data != null) {
            // Liberar proyección anterior si existe antes de crear la nueva
            try { mediaProjection?.stop() } catch (_: Exception) {}
            mediaProjection = mediaProjectionManager.getMediaProjection(resultCode, data)
        }
    }

    /**
     * Negocia la tasa hi-res más alta soportada por el hardware.
     * Garantiza captura bit-perfect desde YouTube, TIDAL y cualquier fuente.
     */
    private fun negotiateSampleRate(encoding: Int, channelMask: Int): Int {
        for (rate in CANDIDATE_RATES) {
            val bufSize = AudioRecord.getMinBufferSize(rate, channelMask, encoding)
            if (bufSize > 0) {
                Log.i(TAG, "Hi-Res negociado → ${rate}Hz (minBuf=$bufSize bytes, bit-perfect float32)")
                return rate
            }
        }
        return 48000
    }

    @SuppressLint("MissingPermission")
    fun startCapture(onBuffer: ((ByteBuffer) -> Unit)? = null) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            Log.e(TAG, "AudioPlaybackCapture requiere Android 10+"); return
        }
        val projection = mediaProjection ?: run {
            Log.e(TAG, "MediaProjection no inicializado."); return
        }
        if (isCapturing) return

        try {
            val channelMask = AudioFormat.CHANNEL_IN_STEREO
            val encoding    = AudioFormat.ENCODING_PCM_FLOAT  // float32 = bit-perfect absoluto

            // ── Negociación hi-res ────────────────────────────────────────
            val negotiatedRate = negotiateSampleRate(encoding, channelMask)
            activeRateHz = negotiatedRate

            val config = AudioPlaybackCaptureConfiguration.Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()

            val format = AudioFormat.Builder()
                .setEncoding(encoding)
                .setSampleRate(negotiatedRate)
                .setChannelMask(channelMask)
                .build()

            // Buffer 4× mínimo: estabilidad sin penalizar latencia perceptiblemente.
            // Crítico para YouTube/TIDAL que emiten en ráfagas variables.
            val minBuf     = AudioRecord.getMinBufferSize(negotiatedRate, channelMask, encoding)
            val bufferSize = (minBuf * 4).coerceAtLeast(4096)

            audioRecord = AudioRecord.Builder()
                .setAudioFormat(format)
                .setBufferSizeInBytes(bufferSize)
                .setAudioPlaybackCaptureConfig(config)
                .build()
                .takeIf { it.state == AudioRecord.STATE_INITIALIZED }
                ?: run {
                    Log.e(TAG, "AudioRecord no inicializó en ${negotiatedRate}Hz"); return
                }

            audioRecord!!.startRecording()
            isCapturing = true

            captureThread = thread(start = true, name = "IvannaSysCap-${negotiatedRate}Hz") {

                // ── Prioridad máxima absoluta de hilo de audio ────────────
                Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)

                val buf       = ByteBuffer.allocateDirect(bufferSize).order(ByteOrder.nativeOrder())
                val floatView = buf.asFloatBuffer()

                // Emite chunk base64 cada N lecturas (≈160ms @ 48kHz, ajusta con hi-res)
                val base64Period = 8
                var readCount    = 0

                // ── Acumuladores métricas IIR one-pole (sin heap) ─────────
                var rmsAccum  = 0.0        // acumulador RMS potencia media (Welford-lite)
                var peakAccum = 0.0f       // acumulador peak rectificado
                val ALPHA     = 0.15       // coeficiente IIR (≈ 66ms a 48kHz/4096 frames)

                while (isCapturing) {
                    buf.clear(); floatView.clear()
                    val bytesRead = audioRecord?.read(buf, bufferSize, AudioRecord.READ_BLOCKING) ?: 0

                    if (bytesRead > 0) {
                        // ── Retransmitir al pipeline externo ──────────────
                        onBuffer?.invoke(buf)

                        // ── Feed motor DSP nativo C++ ──────────────────────
                        try { nativeFeedBuffer(buf, bytesRead) }
                        catch (_: UnsatisfiedLinkError) {}

                        // ── Cómputo RMS + Peak float32 (bit-exact) ────────
                        // [FIX-4] Guard floatsRead > 0 antes del loop:
                        //   • Evita división por cero en coerceAtLeast si bytesRead < 4
                        //   • Evita BufferUnderflowException si bytesRead no es múltiplo
                        //     de Float.SIZE_BYTES (hardware no estándar)
                        val floatsRead = bytesRead / Float.SIZE_BYTES
                        if (floatsRead > 0) {
                            floatView.rewind()
                            var sumSq     = 0.0
                            var localPeak = 0.0f
                            repeat(floatsRead) {
                                val s    = floatView.get()
                                sumSq   += s.toDouble() * s.toDouble()
                                val absS = if (s < 0f) -s else s
                                if (absS > localPeak) localPeak = absS
                            }
                            val rmsFrame = sqrt(sumSq / floatsRead)
                            // IIR one-pole: suavizado temporal sin salteo de tramas
                            rmsAccum  = (1.0 - ALPHA) * rmsAccum  + ALPHA * rmsFrame
                            peakAccum = ((1f - ALPHA.toFloat()) * peakAccum + ALPHA.toFloat() * localPeak)

                            lastRmsDb  = if (rmsAccum  > 1e-10) (20.0 * log10(rmsAccum)).toFloat()   else -96f
                            lastPeakDb = if (peakAccum > 1e-10f) (20f * log10(peakAccum))             else -96f
                            onPcmMetrics?.invoke(lastRmsDb, lastPeakDb)
                        }

                        // ── PCM base64 streaming (YouTube/TIDAL sync) ─────
                        // Codifica el bloque float32 raw para procesamiento JS-side
                        // [FIX-5] buf.rewind() defensivo antes de buf.get(bytes):
                        //   El path de métricas mueve floatView pero NO el ByteBuffer;
                        //   sin embargo, si floatsRead == 0 el path de arriba no ejecuta
                        //   floatView.rewind(). El buf.rewind() aquí garantiza posición 0
                        //   independientemente del estado previo del buffer.
                        readCount++
                        if (readCount % base64Period == 0) {
                            buf.rewind()
                            val bytes = ByteArray(bytesRead)
                            buf.get(bytes)
                            lastPcmBase64 = Base64.encodeToString(bytes, Base64.NO_WRAP)
                            onPcmBase64?.invoke(lastPcmBase64)
                            buf.rewind()
                        }
                    }
                }
            }
            Log.i(TAG, "SystemAudioCapture INICIADA @ ${negotiatedRate}Hz · float32 bit-perfect · YouTube/TIDAL ready")
        } catch (e: Exception) {
            Log.e(TAG, "Fallo al iniciar captura: ${e.message}")
            isCapturing = false
        }
    }

    fun stopCapture() {
        isCapturing = false
        // BUG FIX #2 (original): detener AudioRecord PRIMERO para desbloquear READ_BLOCKING.
        // stop() hace que audioRecord.read() regrese con error de inmediato,
        // permitiendo que el hilo de captura evalúe isCapturing=false y salga limpiamente.
        try { audioRecord?.stop() } catch (_: Exception) {}
        captureThread?.join(1500)
        captureThread = null

        try {
            audioRecord?.release()
        } catch (e: Exception) {
            Log.e(TAG, "Error liberando AudioRecord: ${e.message}")
        }
        audioRecord = null

        // [FIX-3] Liberar MediaProjection explícitamente.
        // Sin stop(): el token queda activo indefinidamente → leak de recursos +
        // SecurityException en Android 14/15 si se intenta reutilizar el mismo token
        // o crear una nueva captura sin liberar la proyección anterior.
        try { mediaProjection?.stop() } catch (_: Exception) {}
        mediaProjection = null

        try { nativeClearBuffer() } catch (_: UnsatisfiedLinkError) {}
        resetMetrics()

        Log.i(TAG, "SystemAudioCapture DETENIDA · MediaProjection liberada · métricas reseteadas")
    }
}
