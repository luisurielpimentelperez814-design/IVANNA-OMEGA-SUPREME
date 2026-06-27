/*
 * IVANNA-FUSION TRASCENDENTAL
 * © 2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * PlaybackCaptureService — captura real del audio interno del sistema
 * (lo que suena en Spotify/YouTube/etc DENTRO del propio dispositivo)
 * usando AudioPlaybackCaptureConfiguration (Android 10+), verificado
 * contra developer.android.com/media/platform/av-capture.
 *
 * LIMITACIONES REALES, documentadas explícitamente (no se simulan ni
 * se ocultan):
 *
 * 1. NO EVITA EL ECO PORQUE NO REEMPLAZA EL AUDIO ORIGINAL. Esta API
 *    permite ESCUCHAR una copia del audio que el sistema ya está
 *    reproduciendo — no permite interceptar y sustituir lo que sale
 *    por el altavoz/auriculares antes de que el usuario lo escuche.
 *    El audio original sigue sonando normal, sin pasar por el motor
 *    DSP de esta app. Para procesar/modificar el sonido que el
 *    usuario realmente escucha (EQ/compresor real aplicado en
 *    tiempo real) se necesita el módulo Magisk (variante ,
 *    que vive dentro de audioserver) — eso sí puede sustituir el
 *    audio antes del hardware. Aquí solo se usa la captura para
 *    ANÁLISIS (alimentar a YAMNet, medir loudness real), no para
 *    procesar el sonido que sale por el altavoz.
 *
 * 2. NO CAPTURA TODAS LAS APPS DE STREAMING. Según la documentación
 *    oficial de Android, una app solo puede ser capturada si:
 *      - su atributo allowAudioPlaybackCapture es "true", O
 *      - no lo declara Y su targetSdkVersion >= 29 (opt-in implícito)
 *    Muchas apps de streaming con contenido con copyright (Spotify,
 *    Netflix, Amazon Prime Video, etc.) declaran explícitamente
 *    allowAudioPlaybackCapture="false" o usan
 *    AudioAttributes.ALLOW_CAPTURE_BY_NONE para impedir esta captura
 *    como protección de contenido. Esto es una decisión de la app
 *    capturada, no algo que IVANNA-FUSION pueda forzar a evadir.
 *    Apps que normalmente SÍ permiten captura: YouTube, navegadores
 *    web reproduciendo audio, la mayoría de apps de podcasts.
 *
 * 3. REQUIERE UN PERMISO DEL USUARIO CADA VEZ que se inicia la
 *    captura (el diálogo de "Comenzar a grabar/transmitir" del
 *    sistema, vía MediaProjectionManager) — esto viene del sistema
 *    operativo, no es algo que la app pueda omitir u ocultar.
 */

package com.ivannafusion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlin.concurrent.thread

class PlaybackCaptureService : Service() {
    companion object {
        private const val TAG = "PlaybackCaptureService"
        private const val NOTIFICATION_CHANNEL_ID = "ivanna_playback_capture"
        private const val NOTIFICATION_ID = 4242
        const val ACTION_START = "com.ivannafusion.action.START_CAPTURE"
        const val ACTION_STOP = "com.ivannafusion.action.STOP_CAPTURE"
        const val EXTRA_RESULT_CODE = "result_code"
        const val EXTRA_RESULT_DATA = "result_data"

        // YAMNet espera 16kHz mono — se captura directamente a esa
        // tasa para no tener que resamplear (a diferencia de
        // processAudio(), que sí recibe 48kHz estéreo del audio
        // procesado localmente).
        const val CAPTURE_SAMPLE_RATE = 16000
        const val CAPTURE_CHANNEL_MASK = AudioFormat.CHANNEL_IN_MONO

        @Volatile var isCapturing: Boolean = false
            private set

        /**
         * Callback global a nivel de companion object: el servicio y la
         * Activity/AudioEngine viven en el mismo proceso (no se declaró
         * android:process distinto en el Manifest), así que esto es más
         * simple y robusto que bindService() para este caso. Se asigna
         * desde MainActivity antes de iniciar la captura.
         */
        @Volatile var globalAudioCallback: ((FloatArray) -> Unit)? = null
    }

    private var mediaProjection: MediaProjection? = null
    private var audioRecord: AudioRecord? = null
    private var captureThread: Thread? = null
    @Volatile private var shouldRun = false

    /** Callback opcional para que AudioEngine/YamnetClassifier consuman
     *  el audio capturado sin que este servicio dependa directamente
     *  de esas clases (acoplamiento mínimo). */
    var onAudioCaptured: ((FloatArray) -> Unit)? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, -1)
                val resultData = intent.getParcelableExtra<Intent>(EXTRA_RESULT_DATA)
                if (resultCode == -1 || resultData == null) {
                    Log.e(TAG, "Faltan resultCode/resultData del MediaProjection — no se puede capturar")
                    stopSelf()
                    return START_NOT_STICKY
                }
                startForeground(NOTIFICATION_ID, buildNotification())
                startCapture(resultCode, resultData)
            }
            ACTION_STOP -> {
                stopCapture()
                stopSelf()
            }
        }
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun startCapture(resultCode: Int, resultData: Intent) {
        val projectionManager = getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        val projection = projectionManager.getMediaProjection(resultCode, resultData)
        mediaProjection = projection

        // USAGE_MEDIA cubre reproductores de música/video estándar
        // (incluye YouTube, la mayoría de apps de podcasts). USAGE_GAME
        // se agrega porque varias apps de streaming de juegos/lives
        // etiquetan su audio así. USAGE_UNKNOWN cubre apps que no
        // declaran un uso específico.
        val config = AudioPlaybackCaptureConfiguration.Builder(projection)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
            .build()

        val audioFormat = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
            .setSampleRate(CAPTURE_SAMPLE_RATE)
            .setChannelMask(CAPTURE_CHANNEL_MASK)
            .build()

        val minBufferSize = AudioRecord.getMinBufferSize(
            CAPTURE_SAMPLE_RATE, CAPTURE_CHANNEL_MASK, AudioFormat.ENCODING_PCM_FLOAT
        )
        if (minBufferSize <= 0) {
            Log.e(TAG, "AudioRecord.getMinBufferSize devolvió $minBufferSize — configuración no soportada en este dispositivo")
            stopSelf()
            return
        }
        val bufferSizeBytes = minBufferSize * 4  // margen, evita underruns en dispositivos lentos

        val record = try {
            AudioRecord.Builder()
                .setAudioFormat(audioFormat)
                .setAudioPlaybackCaptureConfig(config)
                .setBufferSizeInBytes(bufferSizeBytes)
                .build()
        } catch (e: Exception) {
            // Puede fallar si el dispositivo no soporta esta combinación
            // de formato, o por restricciones de seguridad del OEM.
            Log.e(TAG, "No se pudo crear AudioRecord para captura interna", e)
            stopSelf()
            return
        }

        if (record.state != AudioRecord.STATE_INITIALIZED) {
            Log.e(TAG, "AudioRecord no se inicializó correctamente (state=${record.state})")
            record.release()
            stopSelf()
            return
        }

        audioRecord = record
        shouldRun = true
        record.startRecording()
        isCapturing = true
        Log.i(TAG, "Captura de audio interno iniciada (16kHz mono, USAGE_MEDIA/GAME/UNKNOWN)")

        captureThread = thread(name = "IvannaPlaybackCapture") {
            // Tamaño de bloque alineado con YamnetClassifier.INPUT_SAMPLES
            // para no tener que acumular en otra capa además de esta.
            val blockSize = YamnetClassifier.INPUT_SAMPLES
            val buffer = FloatArray(blockSize)
            while (shouldRun) {
                val read = record.read(buffer, 0, blockSize, AudioRecord.READ_BLOCKING)
                if (read > 0) {
                    val block = if (read == blockSize) buffer else buffer.copyOf(read)
                    globalAudioCallback?.invoke(block)
                    onAudioCaptured?.invoke(block)
                } else if (read < 0) {
                    Log.e(TAG, "AudioRecord.read() devolvió error: $read")
                    break
                }
                // NOTA: si la app objetivo (ej. Spotify) bloquea la
                // captura, record.read() típicamente sigue funcionando
                // pero entrega SILENCIO (ceros), no un error explícito.
                // Esto es una limitación documentada de la API misma,
                // no un bug de esta implementación.
            }
        }
    }

    private fun stopCapture() {
        shouldRun = false
        isCapturing = false
        captureThread?.join(1000)
        captureThread = null
        try {
            audioRecord?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "Error deteniendo AudioRecord (posiblemente ya detenido)", e)
        }
        audioRecord?.release()
        audioRecord = null
        mediaProjection?.stop()
        mediaProjection = null
        Log.i(TAG, "Captura de audio interno detenida")
    }

    override fun onDestroy() {
        stopCapture()
        super.onDestroy()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                "Captura de audio interno",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "IVANNA-FUSION está analizando el audio reproducido en este dispositivo"
            }
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        return NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle("IVANNA-FUSION")
            .setContentText("Analizando audio del dispositivo (Música/Habla/Silencio)")
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setOngoing(true)
            .build()
    }
}
