package com.ivanna.omega.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.util.Log
import androidx.core.content.ContextCompat
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/**
 * SpatialAudioEngineV2 — Motor espacial legacy basado en captura de MIC.
 *
 * ⚠️ [LEGACY-WARNING] Este motor NO es el flujo principal de la app.
 *    El motor espacial binaural real de v1.7 es IvannaSpatialEngine
 *    (upmixer neural + object renderer + head-tracking 6DoF), invocado
 *    desde PlaybackCaptureService sobre el audio de reproducción interna
 *    (AudioPlaybackCaptureConfiguration). Este archivo se conserva por
 *    compatibilidad binaria (JNI: nativeInitSpatialEngine / nativeRenderSpatialBlock
 *    / nativeReleaseSpatialEngine son las mismas del SpatialState C++),
 *    NO se instancia desde MainActivity ni desde ningún servicio activo.
 *
 * [FIX-CRASH-1] Antes: bufferSize = 64 frames → underruns constantes,
 *   AudioRecord.read() devolvía 0 → sonido choppy + crash por RUNTIME
 *   overruns en el hilo IO. Nuevo: buffer se negocia con getMinBufferSize()
 *   y se toma como mínimo 4× ese valor.
 *
 * [FIX-CRASH-2] Antes: AudioRecord con MediaRecorder.AudioSource.MIC →
 *   recapturaba la salida del altavoz → feedback howling en cuanto se
 *   encendía. Nuevo: si no hay permiso RECORD_AUDIO, aborta limpiamente
 *   y logea el motivo en vez de estrellar la app.
 *
 * [FIX-CRASH-3] Antes: posX.toInt(), posY.toInt(), posZ.toInt(), mu.toInt()
 *   convertían Float→Int perdiendo TODA la precisión (0.5 → 0). Nuevo: se
 *   escala a rango entero y se preserva el signo antes de castear.
 *
 * [FIX-CRASH-4] Antes: si AudioRecord/AudioTrack.build() fallaban (permiso
 *   denegado justo entre check y build, dispositivo con HAL restringido),
 *   la excepción se propagaba fuera del scope.launch y mataba al hilo IO
 *   sin liberar recursos. Nuevo: try/catch envolviendo la construcción,
 *   liberación explícita, y aborto limpio si algo falla.
 *
 * [FIX-CRASH-5] Antes: el bucle while(isRunning) hacía spin infinito si
 *   AudioRecord.read() devolvía < 0 (ERROR_DEAD_OBJECT). Nuevo: se cuenta
 *   errores consecutivos y se sale del bucle tras un umbral.
 */
class SpatialAudioEngineV2(private val context: Context? = null) {
    companion object {
        private const val TAG = "SpatialAudioEngineV2"
        private const val SAMPLE_RATE = 48000
        private const val PREFERRED_BLOCK_FRAMES = 512
        private const val MAX_CONSECUTIVE_ERRORS = 20
    }

    var posX: Float = 0.0f
    var posY: Float = 0.0f
    var posZ: Float = 0.0f
    var mu: Float = 1.0f

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    @Volatile private var isRunning = false
    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var blockFrames: Int = PREFERRED_BLOCK_FRAMES

    fun start(): Boolean {
        if (isRunning) return true

        // [FIX-CRASH-2] Guard de permiso antes de tocar AudioRecord.
        if (context != null) {
            val hasMic = ContextCompat.checkSelfPermission(
                context, Manifest.permission.RECORD_AUDIO
            ) == PackageManager.PERMISSION_GRANTED
            if (!hasMic) {
                Log.w(TAG, "start() abortado: falta permiso RECORD_AUDIO")
                return false
            }
        }

        // [FIX-CRASH-1] Buffer negociado con el HAL, no valor arbitrario 64.
        val minRec = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val minTrk = AudioTrack.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT
        )
        if (minRec <= 0 || minTrk <= 0) {
            Log.e(TAG, "getMinBufferSize inválido: rec=$minRec trk=$minTrk")
            return false
        }
        // Usamos al menos 4× el mínimo para evitar underruns en dispositivos
        // con jitter alto; el bloque de procesamiento nativo sigue siendo
        // blockFrames muestras (512 por defecto).
        val recBufBytes = maxOf(minRec * 4, blockFrames * 2 * 2)
        val trkBufBytes = maxOf(minTrk * 4, blockFrames * 2 * 2 * 2)

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                recBufBytes
            )
            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e(TAG, "AudioRecord no inicializado")
                releaseNodes()
                return false
            }

            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(trkBufBytes)
                .build()
            if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
                Log.e(TAG, "AudioTrack no inicializado")
                releaseNodes()
                return false
            }

            IvannaNativeLib.nativeInitSpatialEngine(SAMPLE_RATE, blockFrames)
            audioRecord?.startRecording()
            audioTrack?.play()
            isRunning = true
            Log.i(TAG, "start(): OK (block=$blockFrames, recBuf=$recBufBytes)")
        } catch (e: Exception) {
            Log.e(TAG, "start() falló: ${e.message}", e)
            releaseNodes()
            return false
        }

        // Bucle de captura -> proceso nativo -> reproducción
        scope.launch {
            val input = ShortArray(blockFrames)
            val inputFloat = FloatArray(blockFrames)
            val outL = FloatArray(blockFrames)
            val outR = FloatArray(blockFrames)
            val mixed = ShortArray(blockFrames * 2)
            var consecutiveErrors = 0

            while (isRunning) {
                val read = audioRecord?.read(input, 0, blockFrames) ?: 0
                if (read > 0) {
                    consecutiveErrors = 0
                    for (i in 0 until read) inputFloat[i] = input[i] / 32767.0f

                    // [FIX-CRASH-3] Preservar precisión: multiplicamos por 1000
                    // antes de castear para no perder los decimales de posX/Y/Z/mu.
                    IvannaNativeLib.nativeRenderSpatialBlock(
                        inputFloat, outL, outR,
                        (posX * 1000f).toInt(),
                        (posY * 1000f).toInt(),
                        (posZ * 1000f).toInt(),
                        (mu * 1000f).toInt()
                    )

                    for (i in 0 until read) {
                        mixed[i * 2]     = (outL[i] * 32767f).toInt().coerceIn(-32768, 32767).toShort()
                        mixed[i * 2 + 1] = (outR[i] * 32767f).toInt().coerceIn(-32768, 32767).toShort()
                    }
                    audioTrack?.write(mixed, 0, read * 2, AudioTrack.WRITE_BLOCKING)
                } else if (read < 0) {
                    consecutiveErrors++
                    if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                        Log.e(TAG, "AudioRecord.read() falla persistente (code=$read), abortando")
                        break
                    }
                }
            }
        }
        return true
    }

    fun stop() {
        isRunning = false
        scope.cancel()
        releaseNodes()
    }

    private fun releaseNodes() {
        try {
            audioRecord?.stop()
        } catch (_: Exception) {}
        audioRecord?.release()
        audioRecord = null
        try {
            audioTrack?.stop()
        } catch (_: Exception) {}
        audioTrack?.release()
        audioTrack = null
        try {
            IvannaNativeLib.nativeReleaseSpatialEngine()
        } catch (_: Exception) {}
    }
}
