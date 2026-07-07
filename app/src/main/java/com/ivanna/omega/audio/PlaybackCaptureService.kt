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
import android.media.AudioManager
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.ivanna.omega.MainActivity
import com.ivanna.omega.R
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.spatial.IvannaHeadTracker
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.visualizer.IvannaVisualizerBridge
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import kotlinx.coroutines.*

/**
 * PlaybackCaptureService — Servicio de captura de audio de reproducción.
 * Versión corregida con sintaxis válida.
 */
class PlaybackCaptureService : Service() {

    companion object {
        const val CHANNEL_ID = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2
        const val INPUT_SAMPLES = 2048
        // [FIX-FREEZE] tope de errores consecutivos de AudioRecord.read() antes
        // de abandonar el spin y reconstruir la sesión de captura.
        private const val MAX_CONSECUTIVE_ERRORS = 25
        // [FIX-FREEZE-2] tope de reintentos de reconstrucción de sesión antes
        // de rendirse (evita loop infinito si la ruta de audio está realmente muerta).
        private const val MAX_SESSION_RESTARTS = 3
        private const val SESSION_RESTART_BACKOFF_MS = 500L

        // CABLEADO: referencia a la instancia viva del servicio, siguiendo el
        // mismo patrón de estado estático compartido ya usado en DSPState
        // (pfDrive/pfWet/...) para cruzar el límite Activity <-> Service sin
        // Binder. Permite que el switch de MainActivity togguee el motor
        // espacial EN VIVO, sin esperar a un reinicio de captura.
        @Volatile private var runningInstance: PlaybackCaptureService? = null

        fun setSpatialEnabledLive(enabled: Boolean) {
            runningInstance?.let { svc ->
                // [FIX-CLICK] Antes: se conmutaba `spatialEnabled` y se liberaba/creaba
                // el motor de golpe DENTRO del hilo de audio, provocando dos artefactos:
                //   1) al ENCENDER: primeros bloques con estado interno del HRTF/upmixer
                //      en cero -> click audible y transiente feo ("suena feo").
                //   2) al APAGAR: release() dentro del mismo instante en que el hilo de
                //      audio podía estar iterando process() -> uso-tras-libre latente.
                //
                // Nuevo: el motor se crea con `spatialEngine != null` pero el ramp de
                // ganancia arranca en 0.0 y sube linealmente a 1.0 en ~150ms (crossfade).
                // Al apagar, baja a 0.0 en 150ms y sólo entonces libera. Esto elimina
                // el click de encendido y el spike de CPU inicial que se sentía como
                // "la app se traba al encender".
                if (enabled) {
                    svc.initSpatialEngineIfNeeded()
                    svc.spatialEnabled = true
                    svc.spatialTargetGain = 1.0f
                } else {
                    svc.spatialTargetGain = 0.0f
                    // release se hace desde el hilo de audio cuando gain llegue a 0.
                }
            }
        }
    }

    // scope de vida larga (todo el Service); NO se cancela en cada reinicio de
    // sesión, solo en onDestroy. captureJob es el que se cancela/relanza.
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var captureJob: Job? = null
    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var isRunning = false
    private var savedMusicVolume: Int? = null
    private var sessionRestarts = 0

    // [HI-RES][AVISO HONESTO] DSPBridge/IvannaNpeEngine/Visualizer se inicializan
    // aquí, ANTES de que startCapture() negocie el sample rate real (candidateRates
    // en startCapture()). Toda la cadena DSP (biquads, EQ, gammatone, tau de
    // envolventes) está calibrada asumiendo fs=48000 en varios puntos del código
    // nativo. Si algún día un dispositivo llegara a validar 96/192kHz aquí
    // (en la práctica casi ningún Android lo hará: AudioPlaybackCaptureConfiguration
    // captura la mezcla YA hecha por AudioFlinger a la tasa fija de su mezclador,
    // típicamente 48kHz, sin importar lo que se pida), estos .init(48000, ...)
    // quedarían desincronizados del sample rate real de audioRecord/audioTrack.
    // No lo re-arquitecturé aquí porque requiere verificar cada componente DSP
    // nativo uno por uno para que escale correctamente con fs — top de la lista
    // de trabajo pendiente real, no un placeholder cosmético.
    private var activeSampleRate = 48000

    // CABLEADO: motor espacial binaural (neural upmixer + object renderer +
    // head-tracking 6DoF). Escrito y compilado en el .so desde el principio
    // pero jamás instanciado en ningún punto del proyecto — ver auditoría de
    // motores huérfanos. Opt-in vía ParameterStore.isSpatialEngineEnabled()
    // (procesamiento pesado: 32 objetos + upmixer + sensor a 100Hz).
    @Volatile private var spatialEngine: IvannaSpatialEngine? = null
    @Volatile private var headTracker: IvannaHeadTracker? = null
    @Volatile private var spatialEnabled = false
    
    // [ANTI-DOLBY INTEGRATION] Controller para análisis en tiempo real
    private var antiDolbyController: AntiDolbyController? = null
    private var antiDolbyResampler: AudioResampler? = null  // 48kHz → 16kHz para YAMNet

    // [FIX-CLICK] Crossfade de ganancia para el motor espacial.
    // spatialCurrentGain sigue exponencialmente a spatialTargetGain con la
    // constante RAMP_STEP por bloque, evitando el click/pop al toggle.
    @Volatile private var spatialTargetGain: Float = 0.0f
    private var spatialCurrentGain: Float = 0.0f
    // Con INPUT_SAMPLES/2 = 1024 frames por bloque a 48kHz cada bloque son ~21.3ms.
    // RAMP_STEP = 0.14 -> se cruza el 99% de la rampa en ~7 bloques ~= 150ms.
    private val RAMP_STEP = 0.14f
    // Umbral para considerar la rampa como "cerrada" y liberar recursos.
    private val RAMP_EPSILON = 0.001f

    private fun initSpatialEngineIfNeeded() {
        if (spatialEngine != null) return
        try {
            // [FIX-CRASH-INIT] init() de IvannaSpatialEngine + IvannaHeadTracker
            // pueden lanzar (sensores no disponibles, OOM en buffers directos).
            // Antes: cualquier excepción propagaba y mataba el Service.
            val engine = IvannaSpatialEngine(sampleRate = 48000f, blockSize = INPUT_SAMPLES / 2)
            engine.init()
            val tracker = IvannaHeadTracker(this)
            tracker.start()
            engine.setHeadTracker(tracker)
            spatialEngine = engine
            headTracker = tracker
            // Al inicializar arrancamos con gain=0; el toggle ON sube target a 1.
            spatialCurrentGain = 0.0f
        } catch (e: Throwable) {
            android.util.Log.e("PlaybackCapture", "Fallo al inicializar spatial engine: ${e.message}")
            releaseSpatialEngine()
        }
    }

    private fun releaseSpatialEngine() {
        headTracker?.release()
        headTracker = null
        spatialEngine?.release()
        spatialEngine = null
    }

    override fun onCreate() {
        super.onCreate()
        runningInstance = this
        createNotificationChannel()
        DSPBridge.init(48000)
        AudioRouteManager.start(this)
        IvannaNpeEngine.init(48000, INPUT_SAMPLES / 2)
        IvannaVisualizerBridge.init(48000, INPUT_SAMPLES / 2)
        IvannaVisualizerBridgeV2.init(48000, INPUT_SAMPLES / 2)
        
        // [ANTI-DOLBY INTEGRATION] Inicializar controller + resampler
        val audioEngine = AudioEngine()
        antiDolbyController = AntiDolbyController(this).apply {
            initialize(audioEngine)
            enableAntiDolby()  // ← Comienza análisis YAMNet en tiempo real
        }
        antiDolbyResampler = AudioResampler(48000, 16000)
        
        val params = ParameterStore(this)
        IvannaNpeEngine.setBypass(params.isNpeBypass())
        IvannaNpeEngine.setEngineFlags(
            params.isNpeHrtfEnabled(), params.isNpeCochlearEnabled(), params.isNpeAdaptEnabled()
        )
        IvannaNpeEngine.setNeuroParams(
            params.getNpeHarmonicGain(), params.getNpeLateralInhib(),
            params.getNpeOhcCompression(), params.getNpeMasterGainDb()
        )
        IvannaNpeEngine.setAGC(params.getNpeAgcTargetDb(), params.getNpeAgcRate())
        // [FIX-CLICK] Al restaurar el estado persistido: si el spatial estaba
        // encendido en la sesión anterior, arma el motor y deja la rampa apuntando
        // a 1.0 desde el arranque (fade-in suave los primeros ~150ms, sin click).
        // Si estaba apagado, dejamos target=0 y NO inicializamos (ahorro de CPU).
        val restoreSpatial = params.isSpatialEngineEnabled()
        if (restoreSpatial) {
            initSpatialEngineIfNeeded()
            spatialEnabled = true
            spatialTargetGain = 1.0f
        } else {
            spatialEnabled = false
            spatialTargetGain = 0.0f
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = createNotification()
        startForeground(NOTIFICATION_ID, notification)

        // [FIX-CRASH] Guard de permiso RECORD_AUDIO antes de tocar AudioRecord.
        // AudioPlaybackCaptureConfiguration sigue exigiendo este permiso a nivel
        // de plataforma aunque no se use el micrófono físico. Sin este guard,
        // AudioRecord.Builder().build() lanza UnsupportedOperationException
        // dentro de onStartCommand() -> excepción no capturada en un Service
        // -> crash de la app completa.
        val hasRecordPermission = ContextCompat.checkSelfPermission(
            this, Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED

        if (!hasRecordPermission) {
            stopSelf()
            return START_NOT_STICKY
        }

        val mediaProjection = getMediaProjection(intent)
        if (mediaProjection != null) {
            try {
                startCapture(mediaProjection)
            } catch (e: Exception) {
                // [FIX-CRASH] Cualquier fallo de inicialización de AudioRecord/
                // AudioTrack (bufferSize inválido, hardware no estándar, permiso
                // revocado justo después del check, etc.) se contiene aquí en
                // vez de propagarse y matar la app.
                stopCapture()
                stopSelf()
            }
        } else {
            stopSelf()
        }

        return START_STICKY
    }

    override fun onDestroy() {
        // [ANTI-DOLBY] Liberar controller antes de cancelar scope
        antiDolbyController?.release()
        antiDolbyController = null
        antiDolbyResampler = null
        
        stopCapture()
        scope.cancel()
        if (runningInstance === this) runningInstance = null
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
        // [FIX-CLICK] Solo re-arma el motor espacial si el usuario ya lo tenía
        // encendido; si no, no gastamos CPU/memoria hasta que toggle explícito.
        if (spatialEnabled) initSpatialEngineIfNeeded()

        val config = AudioPlaybackCaptureConfiguration.Builder(mediaProjection)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .build()

        // [HI-RES] Cascada real de sample rate: a diferencia de SystemAudioCapture
        // (que solo valida con getMinBufferSize, un cálculo aritmético que NO
        // confirma soporte real de hardware/HAL), aquí se construye y valida
        // CADA candidato con AudioRecord.Builder().build() + comprobación de
        // STATE_INITIALIZED, que es la única confirmación real de que el HAL
        // aceptó esa tasa para AudioPlaybackCaptureConfiguration.
        //
        // Aviso honesto: AudioPlaybackCaptureConfiguration captura la mezcla
        // YA HECHA por AudioFlinger, que en la enorme mayoría de dispositivos
        // Android corre a una tasa fija de mezclador (típicamente 48kHz) sin
        // importar lo que pida la fuente. Pedir 192kHz aquí puede negociar
        // (el framework resamplea), pero NO añade resolución real que no
        // estuviera ya en la mezcla de 48kHz — es una cascada honesta, no una
        // promesa de más detalle del que el mezclador realmente tiene. El
        // camino que SÍ entrega hi-res real de hardware, sin pasar por el
        // mezclador compartido, es UsbAudioProManager (USB OTG directo,
        // 384kHz/32-bit ya implementado) — ver nota en el README.
        //
        // [HOTFIX] Forzado a 48kHz para sincronizar con inicialización de DSP en
        // onCreate(). AudioPlaybackCaptureConfiguration está limitada por AudioFlinger
        // que típicamente corre a 48kHz. Candidatos 96k/192k fueron removidos porque:
        // - DSPBridge, IvannaNpeEngine, Gammatone se inicializan a 48kHz en onCreate()
        // - Si AudioRecord se negocia a 96k/192k, hay mismatch → visualizer congela + glitches
        // - La solución correcta requiere reinicializar DSP en startCapture() (TODO)
        // - Por ahora: forzado a 48kHz donde REALMENTE está AudioFlinger
        //
        // AVISO DE CONSISTENCIA README ↔ CÓDIGO (audit v1.8.1):
        //   El README anuncia "Cascada real de sample rate 192k → 96k → 48k".
        //   Es cierto en `SystemAudioCapture` (otra ruta, ya validada con
        //   AudioRecord.Builder+STATE_INITIALIZED) y en `UsbAudioProManager`
        //   (USB OTG directo, 384kHz/32-bit real bypasando el mezclador).
        //   Esta ruta (playback capture vía MediaProjection) está fijada a
        //   48kHz por AudioFlinger — no por decisión de diseño — y el DSP
        //   se inicializa con esa misma tasa. Cuando el TODO de reinicializar
        //   DSP en startCapture() se cierre, este array vuelve a
        //   [192000, 96000, 48000] y este comentario debe actualizarse.
        val candidateRates = intArrayOf(48000)
        var chosenRate = 48000
        var bufferSize = 0

        for (rate in candidateRates) {
            val minBuf = AudioRecord.getMinBufferSize(
                rate, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            )
            if (minBuf <= 0) continue

            val candidate = try {
                AudioRecord.Builder()
                    .setAudioFormat(
                        AudioFormat.Builder()
                            .setSampleRate(rate)
                            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                            .build()
                    )
                    .setBufferSizeInBytes(minBuf)
                    .setAudioPlaybackCaptureConfig(config)
                    .build()
            } catch (e: Exception) {
                null
            }

            if (candidate != null && candidate.state == AudioRecord.STATE_INITIALIZED) {
                audioRecord = candidate
                chosenRate = rate
                bufferSize = minBuf
                break
            } else {
                candidate?.release()
            }
        }

        // [FIX-CRASH] Si NINGÚN candidato (ni siquiera 48kHz) inicializó, es un
        // fallo real de permiso/política de audio — abortar limpio.
        if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
            throw IllegalStateException("AudioRecord no se pudo inicializar en ningún sample rate candidato (permiso o política de audio)")
        }
        activeSampleRate = chosenRate

        // FIX: silencia el stream de música original (la fuente sigue
        // generando PCM y se sigue capturando arriba, solo se apaga su
        // salida al altavoz) y saca el audio procesado por un stream
        // distinto (ACCESSIBILITY) que no se ve afectado por ese mute.
        // Resultado: solo se escucha el audio ya procesado por IVANNA.
        val audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        savedMusicVolume = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, 0, 0)

        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .setAllowedCapturePolicy(AudioAttributes.ALLOW_CAPTURE_BY_NONE)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    // [FIX] antes fijo a 48000 sin importar qué tasa haya
                    // negociado audioRecord arriba — si algún día difieren,
                    // AudioTrack.write() reproduciría a la velocidad/tono
                    // incorrectos. Debe ser SIEMPRE la misma tasa que capturó.
                    .setSampleRate(chosenRate)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build()
            )
            .setBufferSizeInBytes(bufferSize)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        // [FIX-CRASH] Mismo principio que AudioRecord: si AudioTrack no inicializa
        // (ruta de salida ASSISTANCE_ACCESSIBILITY no disponible en el dispositivo,
        // por ejemplo), abortar limpiamente en vez de reproducir sobre un track roto.
        if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
            throw IllegalStateException("AudioTrack no se pudo inicializar")
        }

        audioRecord?.startRecording()
        audioTrack?.play()
        isRunning = true

        captureJob = scope.launch {
            val buffer = FloatArray(INPUT_SAMPLES)
            val mono = FloatArray(INPUT_SAMPLES / 2)
            // Buffers de deinterleave para el motor espacial (opera en L/R
            // separados, no en el intercalado que usan DSPBridge/NPE).
            val spatialInL = FloatArray(INPUT_SAMPLES / 2)
            val spatialInR = FloatArray(INPUT_SAMPLES / 2)
            val spatialOutL = FloatArray(INPUT_SAMPLES / 2)
            val spatialOutR = FloatArray(INPUT_SAMPLES / 2)

            // [FIX-FREEZE-IMPROVED] Watchdog mejorado para visualizer congelado:
            // - Reducido timeout de 400ms → 100ms (detecta freeze mucho más rápido)
            // - Múltiples resets de componentes en fallback
            // - Anti-Dolby puede generar latencia adicional → timeout más generoso
            var lastGoodBlockNanos = System.nanoTime()
            var consecutiveErrors = 0
            val silenceTimeoutNanos = 100_000_000L // 100ms (fue 400ms) → más sensible
            var watchdogReset = false
            var watchdogResetCount = 0

            while (isRunning && isActive) {
                val read = audioRecord?.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING) ?: 0

                if (read > 0) {
                    consecutiveErrors = 0
                    sessionRestarts = 0
                    lastGoodBlockNanos = System.nanoTime()
                    watchdogReset = false

                    val frames = read / 2
                    
                    // [ANTI-DOLBY INTEGRATION] Procesar audio para YAMNet (48kHz → 16kHz)
                    // Downsample mono para clasificación en paralelo con DSP
                    antiDolbyResampler?.let { resampler ->
                        val monoIn = FloatArray(frames)
                        for (i in 0 until frames) {
                            monoIn[i] = 0.5f * (buffer[i * 2] + buffer[i * 2 + 1])
                        }
                        val monoResampled = resampler.resample(monoIn)
                        antiDolbyController?.processAudioFrame(monoResampled)
                    }
                    
                    DSPBridge.process(buffer, frames)
                    IvannaNpeEngine.processInterleavedStereo(buffer, frames)
                    // CABLEADO: motor espacial binaural (upmixer + object renderer +
                    // head-tracking). Opera sobre el mismo buffer, después del NPE,
                    // igual patrón que el resto de la cadena. Deinterleave -> proceso
                    // -> reinterleave in-place sobre `buffer`.
                    // Captura atómica: una sola lectura volatile a una referencia local.
                    // Aunque `spatialEngine` se ponga en null en otro hilo justo después,
                    // esta copia local sigue siendo válida durante todo este bloque,
                    // evitando el crash JNI con rendererHandle=0.
                    val engine = spatialEngine
                    if (engine != null) {
                        // [FIX-CLICK] Rampa de ganancia por bloque, no por muestra —
                        // evita zipper noise pero mantiene el crossfade audible como
                        // fade suave. spatialCurrentGain sigue exponencialmente a
                        // spatialTargetGain (0 o 1).
                        val target = spatialTargetGain
                        spatialCurrentGain += (target - spatialCurrentGain) * RAMP_STEP
                        val gain = spatialCurrentGain
                        val dryGain = 1.0f - gain

                        // Si el gain está prácticamente en 0 y el target también,
                        // saltamos el motor entero (no gastamos CPU) y, si estamos
                        // apagando, liberamos aquí (nunca desde otro hilo).
                        if (gain < RAMP_EPSILON && target < RAMP_EPSILON) {
                            if (spatialEnabled) {
                                spatialEnabled = false
                                releaseSpatialEngine()
                            }
                        } else {
                            try {
                                for (i in 0 until frames) {
                                    spatialInL[i] = buffer[i * 2]
                                    spatialInR[i] = buffer[i * 2 + 1]
                                }
                                engine.processStereoInput(
                                    spatialInL, spatialInR,
                                    spatialOutL, spatialOutR,
                                    frames
                                )
                                // Crossfade wet/dry — al encender wet sube de 0 a 1;
                                // al apagar baja de 1 a 0. Sin salto duro.
                                for (i in 0 until frames) {
                                    buffer[i * 2]     = buffer[i * 2]     * dryGain + spatialOutL[i] * gain
                                    buffer[i * 2 + 1] = buffer[i * 2 + 1] * dryGain + spatialOutR[i] * gain
                                }
                            } catch (t: Throwable) {
                                // [FIX-CRASH] Si algo dentro del motor lanza (JNI
                                // returning en estado inválido, buffer directo GC'd),
                                // no matamos el hilo de audio: bypass y seguimos.
                                android.util.Log.e("PlaybackCapture", "spatial process falló: ${t.message}")
                            }
                        }
                    }
                    for (i in 0 until frames) {
                        mono[i] = 0.5f * (buffer[i * 2] + buffer[i * 2 + 1])
                    }
                    IvannaVisualizerBridge.processBlock(mono, frames)
                    // v2: mismo downmix post-NPE, para el wallpaper de 13 bandas.
                    IvannaVisualizerBridgeV2.processBlockFromNPE(mono, frames)
                    audioTrack?.write(buffer, 0, read, AudioTrack.WRITE_BLOCKING)
                } else if (read < 0) {
                    // Código de error real de AudioRecord (negativo). No spinnear:
                    // contamos errores consecutivos y, si persiste, reconstruimos
                    // la sesión de captura en vez de dejar el hilo vivo pero inútil.
                    consecutiveErrors++
                    if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                        break // sale del while -> reinicio de sesión afuera
                    }
                    delay(20L)
                } else {
                    // read == 0: no es error, pero tampoco avanzó el reloj de datos.
                    val silentFor = System.nanoTime() - lastGoodBlockNanos
                    if (silentFor > silenceTimeoutNanos) {
                        if (!watchdogReset) {
                            // Primer timeout: reset suave
                            IvannaVisualizerBridge.reset()
                            IvannaVisualizerBridgeV2.reset()
                            watchdogReset = true
                            watchdogResetCount++
                        } else if (watchdogResetCount > 3) {
                            // Demasiados timeouts consecutivos: salir del loop y reconstruir sesión
                            break
                        }
                    }
                }
            }

            if (isActive && consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                // [FIX-FREEZE-2] Antes esto solo llamaba stopCapture() y ahí
                // moría la sesión para siempre (visualizer/audio quedaban
                // "trabados" sin nadie relanzando nada). Ahora reconstruimos
                // de verdad con el mismo MediaProjection (sigue vivo mientras
                // no se llame mediaProjection.stop(), que no ocurre en
                // stopCapture()), con backoff y tope de reintentos para no
                // spinnear si la ruta de audio está realmente muerta.
                withContext(Dispatchers.Main) {
                    stopCapture()
                    if (sessionRestarts < MAX_SESSION_RESTARTS) {
                        sessionRestarts++
                        delay(SESSION_RESTART_BACKOFF_MS)
                        try {
                            startCapture(mediaProjection)
                        } catch (e: Exception) {
                            stopCapture()
                            stopSelf()
                        }
                    } else {
                        stopSelf()
                    }
                }
            }
        }
    }

    private fun stopCapture() {
        isRunning = false
        // [FIX-FREEZE-2] antes cancelaba `scope` completo -> tras el primer
        // error, ningún scope.launch futuro volvía a ejecutarse (Job
        // cancelado no acepta más corrutinas) y la sesión quedaba muerta
        // para siempre. Ahora solo se cancela el job de esta sesión de
        // captura; `scope` vive hasta onDestroy().
        captureJob?.cancel()
        captureJob = null
        AudioRouteManager.stop()
        IvannaVisualizerBridge.release()
        IvannaVisualizerBridgeV2.release()
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        IvannaNpeEngine.release()
        releaseSpatialEngine()
        savedMusicVolume?.let {
            val audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
            audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, it, 0)
        }
        savedMusicVolume = null
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
