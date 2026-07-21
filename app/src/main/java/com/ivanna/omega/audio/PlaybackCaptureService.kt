package com.ivanna.omega.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
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
import com.ivanna.omega.MainActivity
import com.ivanna.omega.R
import com.ivanna.omega.VoiceController
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import kotlinx.coroutines.*

/**
 * PlaybackCaptureService — Servicio de captura de audio de reproducción.
 *
 * FIX v3.0: conecta el visualizador al loop de audio REAL. Antes el buffer
 * capturado se descartaba por completo ("// Procesar buffer capturado" sin
 * cuerpo) — el handle nativo del visualizador (IvannaVisualizerBridgeV2)
 * nunca se creaba ni se alimentaba, así que VisualizerRendererV2 siempre
 * leía ceros: la UI del visualizer se abría pero mostraba silencio.
 *
 * Pipeline real por bloque capturado:
 *   AudioRecord (MediaProjection, stereo interleaved)
 *     → IvannaNpeEngine.processInterleavedStereo()  (análisis neuromórfico:
 *       alimenta género/RMS/AGC/confianza que la UI ya lee por polling)
 *     → SpatialAudioEngineV2.feedCapturedBlock()  (motor binaural: actualiza
 *       estado espacial nativo de los 32 objetos con audio REAL de las apps,
 *       no con el micrófono — ver fix en SpatialAudioEngineV2)
 *     → downmix a mono
 *     → IvannaVisualizerBridgeV2.processBlockFromNPE() (bandas espectrales
 *       para el shader GLSL del fondo Aurora)
 *
 * FIX (telemetría desconectada / captura solo-visualizer): este servicio
 * antes sólo se arrancaba al abrir la UI del visualizer, así que NPE y el
 * motor espacial nunca recibían audio real salvo que el usuario abriera esa
 * pantalla. Ahora MainActivity lo arranca al iniciar la app (junto al
 * permiso RECORD_AUDIO), independientemente de si el visualizer está
 * visible, para que NPE y el motor espacial siempre tengan señal real que
 * analizar.
 *
 * Nota: esta captura es sólo de LECTURA/ANÁLISIS — no se reinyecta a la
 * salida de audio (evitaría eco/duplicado). El procesamiento audible real
 * ocurre vía IvannaGlobalEffectManager (AudioEffect en la sesión de la app
 * fuente) o, para IvannaBridgePlayer, vía AudioEngine/DSPBridge.
 */
class PlaybackCaptureService : Service() {

    companion object {
        private const val TAG = "PlaybackCaptureService"
        const val CHANNEL_ID = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2
        const val INPUT_SAMPLES = 128

        // FIX (VoiceController huérfano — cableado real de audio):
        // VoiceController.processAudio() delega en YamnetClassifier, que
        // exige un buffer de 15600 muestras @ 16kHz mono (0.975s, ver
        // YamnetClassifier.INPUT_LENGTH). Este servicio captura @ 48kHz,
        // así que hace falta decimar 3:1 antes de acumular la ventana.
        private const val VOICE_DECIMATION = 3          // 48000 / 16000
        private const val VOICE_WINDOW_SAMPLES = 15600  // = YamnetClassifier.INPUT_LENGTH
    }

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var audioRecord: AudioRecord? = null
    private var isRunning = false
    private var mediaProjection: MediaProjection? = null
    private var projectionCallback: MediaProjection.Callback? = null

    // FIX (VoiceController huérfano, ver README "Qué NO está terminado
    // hoy"): la clase ya routeaba comandos/perfiles correctamente, solo
    // nunca recibía audio real. Se instancia perezosamente (necesita
    // Context) y se alimenta desde el mismo bloque capturado que ya usa
    // IvannaNpeEngine/SpatialAudioEngineV2 más abajo — sin permisos ni
    // AudioRecord nuevos.
    private var voiceController: VoiceController? = null
    private val voiceWindow = FloatArray(VOICE_WINDOW_SAMPLES)
    private var voiceWindowFill = 0
    private var voiceDecimAcc = 0f
    private var voiceDecimCount = 0

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {

        val mediaProjection = getMediaProjection(intent)

        if (mediaProjection == null) {
            Log.w(TAG, "MediaProjection no autorizada. Servicio detenido.")
            stopSelf()
            return START_NOT_STICKY
        }

        val notification = createNotification()
        startForeground(NOTIFICATION_ID, notification)

        startCapture(mediaProjection)

        return START_STICKY
    }

    override fun onDestroy() {
        stopCapture()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun getMediaProjection(intent: Intent?): MediaProjection? {
        val resultCode = intent?.getIntExtra("resultCode", -1) ?: return null
        val data = intent.getParcelableExtra<Intent>("data") ?: return null
        val projectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        return projectionManager.getMediaProjection(resultCode, data)
    }

    private fun startCapture(mediaProjection: MediaProjection) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return

        try {
            this.mediaProjection = mediaProjection

            // FIX (sesión de MediaProjection sin callback — obligatorio desde
            // Android 14): sin esto la app no se entera cuando el usuario
            // corta la captura desde el chip del sistema, o el sistema la
            // revoca por otro motivo — el AudioRecord se queda leyendo sobre
            // una sesión muerta en vez de limpiar.
            val callback = object : MediaProjection.Callback() {
                override fun onStop() {
                    Log.i("PlaybackCaptureService", "MediaProjection.onStop: sesión terminada — limpiando captura")
                    stopCapture()
                    stopSelf()
                }
            }
            projectionCallback = callback
            mediaProjection.registerCallback(callback, null)

            val sampleRate = 48000

            // FIX (auto-captura — "el problema del micrófono" que en realidad
            // nunca fue el mic físico): por defecto, AudioPlaybackCaptureConfiguration
            // captura audio de OTRAS apps *y de la propia* (documentado por
            // Android: "capturing audio signals played by other apps (and
            // yours)"). NoRootAudioProcessor/IvannaBridgePlayer sacan el
            // audio ya procesado con USAGE_MEDIA — el mismo usage que este
            // servicio está capturando. Sin excludeUid(), la app se
            // recaptura a sí misma: salida procesada → recapturada →
            // reprocesada → vuelta a sacar, un loop 100% digital que suena
            // igual que feedback acústico de mic pero no depende de él en
            // absoluto. Excluir el propio UID rompe el ciclo.
            val config = AudioPlaybackCaptureConfiguration.Builder(mediaProjection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .excludeUid(android.os.Process.myUid())
                .build()

            val bufferSize = AudioRecord.getMinBufferSize(
                sampleRate,
                AudioFormat.CHANNEL_IN_STEREO,
                AudioFormat.ENCODING_PCM_FLOAT
            )

            audioRecord = AudioRecord.Builder()
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setSampleRate(sampleRate)
                        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(bufferSize)
                .setAudioPlaybackCaptureConfig(config)
                .build()

            // FIX (telemetría muerta en silencio): antes no se verificaba el
            // estado de AudioRecord tras construirlo. Si RECORD_AUDIO no estaba
            // concedido (u otra falla de plataforma), build() dejaba el
            // AudioRecord en STATE_UNINITIALIZED y startRecording() lanzaba
            // IllegalStateException sin log claro — o, peor, el loop de lectura
            // corría igual leyendo siempre 0 muestras: NPE/motor espacial/
            // visualizer nunca recibían nada y no había ningún indicio del porqué.
            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e("PlaybackCaptureService", "AudioRecord de captura no inicializó (state=${audioRecord?.state}) — " +
                    "¿falta permiso RECORD_AUDIO? Cancelando captura.")
                audioRecord?.release()
                audioRecord = null
                stopSelf()
                return
            }

            // FIX: handle nativo del visualizador — maxBlockFrames en FRAMES por
            // canal (INPUT_SAMPLES es el tamaño del array intercalado L,R,L,R,
            // así que el máximo de frames por canal es la mitad).
            IvannaVisualizerBridgeV2.init(sampleRate, INPUT_SAMPLES / 2)
            // bufferSize está en bytes; frame estéreo float = 2 canales * 4 bytes
            val estimatedLatencyMs = (bufferSize.toFloat() / 8f) / sampleRate * 1000f
            IvannaVisualizerBridgeV2.setDeviceLatencyMs(estimatedLatencyMs)

            if (voiceController == null) voiceController = VoiceController(applicationContext)

            audioRecord?.startRecording()
            isRunning = true

            scope.launch {
                val buffer = FloatArray(INPUT_SAMPLES)
                val mono = FloatArray(INPUT_SAMPLES / 2)
                while (isRunning && isActive) {
                    val read = audioRecord?.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING) ?: 0
                    if (read > 0) {
                        // read son muestras intercaladas L,R; frames por canal = read/2
                        val numFrames = read / 2
                        if (numFrames > 0) {
                            // Análisis neuromórfico (género/RMS/AGC/confianza) —
                            // in-place, no altera el audio real de salida.
                            if (IvannaNpeEngine.isReady) {
                                IvannaNpeEngine.processInterleavedStereo(buffer, numFrames)
                            }
                            // FIX: motor binaural alimentado con audio REAL capturado
                            // (antes nunca recibía nada — capturaba el mic físico).
                            SpatialAudioEngineV2.feedCapturedBlock(buffer, numFrames)
                            // Downmix a mono para el visualizador
                            for (i in 0 until numFrames) {
                                mono[i] = 0.5f * (buffer[i * 2] + buffer[i * 2 + 1])
                            }
                            // FIX (VoiceController huérfano): decima 48kHz→16kHz,
                            // acumula la ventana de 0.975s que pide YamnetClassifier
                            // y, al llenarse, clasifica y ejecuta el perfil sugerido.
                            // Envuelto en try/catch — mismo criterio que applyEQ/
                            // persistState en AdaptiveBackend: un fallo acá (TFLite
                            // no cargado, etc.) no debe tumbar la captura real.
                            try {
                                feedVoiceController(mono, numFrames)
                            } catch (e: Throwable) {
                                Log.w("PlaybackCaptureService", "VoiceController: motor no disponible todavía")
                            }
                            IvannaVisualizerBridgeV2.processBlockFromNPE(mono, numFrames)
                        }
                    }
                }
            }
        } catch (e: SecurityException) {
            Log.e("PlaybackCaptureService", "SecurityException al iniciar captura — falta RECORD_AUDIO", e)
            audioRecord?.release()
            audioRecord = null
            stopSelf()
        } catch (e: Exception) {
            Log.e("PlaybackCaptureService", "Error iniciando captura de reproducción", e)
            audioRecord?.release()
            audioRecord = null
            stopSelf()
        }
    }

    // FIX (VoiceController huérfano): decima el mono @48kHz a 16kHz (factor
    // 3:1, promedio simple de cada grupo de 3 muestras — suficiente para un
    // clasificador de contexto, no para HiFi) y acumula en voiceWindow hasta
    // completar VOICE_WINDOW_SAMPLES (0.975s). Al llenarse: clasifica con
    // YamnetClassifier (vía VoiceController.processAudio) y, si sugiere un
    // perfil, lo ejecuta (VoiceController.executeCommand) — mismo flujo que
    // ya usaban los comandos de texto, ahora disparado por audio real en vez
    // de simulación.
    private fun feedVoiceController(mono: FloatArray, numFrames: Int) {
        val vc = voiceController ?: return
        for (i in 0 until numFrames) {
            voiceDecimAcc += mono[i]
            voiceDecimCount++
            if (voiceDecimCount >= VOICE_DECIMATION) {
                if (voiceWindowFill < voiceWindow.size) {
                    voiceWindow[voiceWindowFill] = voiceDecimAcc / voiceDecimCount
                    voiceWindowFill++
                }
                voiceDecimAcc = 0f
                voiceDecimCount = 0
            }
        }
        if (voiceWindowFill >= voiceWindow.size) {
            val hint = vc.processAudio(voiceWindow)
            if (hint != "none") vc.executeCommand(hint)
            voiceWindowFill = 0
        }
    }

    private fun stopCapture() {
        if (!isRunning && audioRecord == null && mediaProjection == null) return // idempotente
        isRunning = false
        scope.cancel()
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
        IvannaVisualizerBridgeV2.release()

        projectionCallback?.let { mediaProjection?.unregisterCallback(it) }
        projectionCallback = null
        mediaProjection = null

        // Reset del acumulador de VoiceController — evita mezclar audio de
        // una sesión de captura vieja con la siguiente.
        voiceWindowFill = 0
        voiceDecimAcc = 0f
        voiceDecimCount = 0
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "IVANNA Playback Capture",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Canal para captura de audio de reproducción"
                setSound(null, null)
                enableVibration(false)
            }

            val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA OMEGA SUPREME")
            .setContentText("Captura de audio activa")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
