package com.ivanna.omega.audio

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.*
import android.util.Log
import androidx.core.app.NotificationCompat
import com.ivanna.omega.R
import com.ivanna.omega.VoiceController
import com.ivanna.omega.ai.PerceptualState
import com.ivanna.omega.ai.PerceptualCortex
import com.ivanna.omega.ai.PerceptualStateListener
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.audio.IvannaLabMonitor
import com.ivanna.omega.visualizer.IvannaVisualizerBridge
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import com.ivanna.omega.visualizer.IvannaVisualizerBark64Bridge
import com.ivanna.omega.audio.engine.AdaptiveLatencyController
import com.ivanna.omega.audio.engine.MasterTimingController
import com.ivanna.omega.audio.engine.LoopLoadGovernor
import com.ivanna.omega.audio.engine.OutputCushionPolicy
import com.ivanna.omega.audio.engine.ProfessionalAntiPopEngine
import com.ivanna.omega.audio.engine.AudioThreadBudgetGuard
import com.ivanna.omega.audio.engine.CaptureBlockRing
import com.ivanna.omega.audio.engine.PredictiveLoadGovernor
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

// ── AUDIT FIX PR 2: PlaybackCaptureService ahora recibe cambios de PerceptualCortex ──
class PlaybackCaptureService : Service(), PerceptualStateListener {

    companion object {
        private const val TAG = "PlaybackCaptureService"

        private const val SAMPLE_RATE    = 48_000
        private const val CHANNEL_COUNT  = 2
        // MISIÓN HAAS MASTER LATENCY TUNING (2026-09-17): 512 → 384 → 320.
        // 384 frames (8.0 ms/bloque) reducía el desfase ~25% frente a 512
        // pero, verificado en dispositivo real por el propietario (no en
        // teoría): el eco seguía siendo audible con ambos sliders al 100%.
        // 320 frames @ 48 kHz = 6.67 ms/bloque (vs 8.0 ms a 384, vs 10.67 ms
        // a 512) — reduce el desfase percibido otro ~17% adicional sobre 384
        // (~37.5% total sobre el valor original de 512). Sigue siendo
        // múltiplo de 64 (alineación SIMD/NEON intacta, igual que 384 y 512)
        // y los FFT de 512pt del pipeline lo toleran por zero-padding, igual
        // que antes. Todos los buffers derivados (BLOCK_SAMPLES, rtSpatialIn/
        // Out, mono, visualizadores V2/Bark64, Haas alignment) escalan de
        // esta constante. Si persiste algo de desfase perceptible, el
        // siguiente escalón a evaluar (por indicación explícita: no saltar a
        // valores extremos sin medir) es 256 — no bajar directo ahí sin antes
        // medir underruns/CPU reales a 320 en el dispositivo.
        // Sin cambios: HAAS_SAFE_GAIN=0.40, rampa per-sample, captura, DSP,
        // HRTF/SOFA/RIR, SAF, upmixing.
        private const val BLOCK_FRAMES   = 320
        private const val BLOCK_SAMPLES  = BLOCK_FRAMES * CHANNEL_COUNT

        // Anillo de AudioRecord en BLOQUES. El valor anterior (BLOCK_SAMPLES * 4 BYTES)
        // eran exactamente 1 bloque = 6.67 ms: cualquier jitter del hilo de captura
        // (GC, planificador, un bloque lento del DSP) > 6.67 ms desbordaba el anillo y
        // perdía muestras (micro-corte / tronido). Con 4 bloques (26.7 ms, igual que la
        // pista de salida) el jitter se absorbe y NO añade latencia en régimen: read()
        // devuelve en cuanto hay un bloque y el DSP (<1 ms) drena el atraso enseguida.
        // v2.5.374 — 4 → 8 bloques (53.3 ms): el logcat del Moto G85 muestra
        // "RecordThread: buffer overflow" persistente (14:05:15, 14:14:14,
        // 14:14:23). El bucle hace NPE-analyze + DSP nativo (ORBE+HOLOS+Φ+
        // Volterra, todos ACTIVOS) + etapa de salida + escritura a AudioTrack
        // en el mismo hilo que lee; un bloque lento o un write() que bloquea
        // retrasa la siguiente lectura y el RecordThread de AudioFlinger se
        // desborda. 8 bloques absorben esos transitorios sin añadir latencia
        // en régimen (read() bloqueante devuelve en cuanto hay un bloque).
        // El fix estructural (desacoplar captura y escritura en hilos
        // distintos con anillo intermedio) queda documentado para una
        // versión posterior; 8 bloques + NPE decimado bajo carga (abajo)
        // eliminan los desbordamientos por jitter sin tocar el DSP.
        private const val REC_BUFFER_BLOCKS = 8

        /** Bytes del anillo de AudioRecord (PCM_FLOAT = 4 B/muestra). */
        internal fun recordBufferBytes(minRecBytes: Int): Int =
            maxOf(minRecBytes, BLOCK_SAMPLES * Float.SIZE_BYTES * REC_BUFFER_BLOCKS)

        const val CHANNEL_ID    = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2
        // Notificación de "me rindo" — ID distinto para no reemplazar la
        // notificación foreground en curso mientras el servicio aún se
        // está deteniendo.
        private const val GIVEUP_NOTIFICATION_ID = 3

        // FIX (bucle infinito de reintentos condenados a fallar): scheduleRestart()
        // reintentaba startEngine(savedProj) para siempre, con backoff creciente,
        // pero savedProj es la MISMA MediaProjection que Android acaba de invalidar
        // (eso es literalmente lo que dispara onStop()/onProjLost() en primer
        // lugar). Una vez que el sistema invalida una MediaProjection, ese objeto
        // específico queda inservible para siempre — hace falta un Intent NUEVO
        // con consentimiento del usuario, no el mismo objeto reciclado. Sin este
        // límite, el servicio consumía batería/CPU indefinidamente en un ciclo
        // que matemáticamente nunca podía tener éxito, sin avisar nunca al
        // usuario que necesitaba volver a conceder el permiso manualmente.
        // 6 intentos: coincide con el último escalón ya existente de la propia
        // escalera de backoff (retryAttempts < 6 -> 5_000L) — tras agotar esa
        // escalera, en vez de caer en un "else -> 30_000L" para siempre, se
        // rinde honestamente.
        private const val MAX_RESTART_ATTEMPTS = 6

        private const val VOICE_DECIMATION    = 3
        private const val VOICE_WINDOW_SAMPLES = 15600

        private val _isCapturing = MutableStateFlow(false)
        val isCapturing: StateFlow<Boolean> = _isCapturing.asStateFlow()

        // Ganancia de mezcla de captura AJUSTABLE (v2.5.412): cuánto del
        // procesado se reinyecta frente al original de Tidal, que suena al
        // 100% (no hay API pública para silenciarlo). La mueve el usuario
        // con el slider "MEZCLA CAPTURA" del panel (DSP CORE). Rango
        // 0.20–1.00. Default 0.60 = DELTA_BLEND_SOLO, el valor vivo actual:
        // NO cambia el comportamiento de arranque. (OJO: el 0.40 del
        // comentario Haas es el punto dulce LEGACY; en el modelo
        // diferencial actual el target vivo es 0.60, no 0.40. Poner 0.40
        // de default BAJARÍA la presencia, lo contrario de lo que pide
        // Esse. El 0.40 sigue disponible en el slider.) Por encima de
        // ~0.50 el eco / comb filtering vuelve (aviso en el slider).
        // Con módulo activo el target sigue siendo DELTA_BLEND_WITH_MODULE
        // (0.45): el módulo ya procesa in-place. El engine re-lee el
        // valor throttled (250 ms) desde el hilo de audio: el slider tarda
        // ≤250 ms en aplicarse y la rampa per-sample (MIX_GAIN_STEP) lo
        // lleva sin zipper noise.
        const val CAPTURE_MIX_PREFS = "ivanna_capture"
        const val CAPTURE_MIX_KEY = "capture_mix_gain"
        const val CAPTURE_MIX_MIN = 0.20f
        const val CAPTURE_MIX_MAX = 1.00f
        const val CAPTURE_MIX_DEFAULT = 0.60f

        /** Clamp puro (testeable en JVM sin Android). No-finito → default. */
        fun clampCaptureMixGain(value: Float): Float =
            if (!value.isFinite()) CAPTURE_MIX_DEFAULT
            else value.coerceIn(CAPTURE_MIX_MIN, CAPTURE_MIX_MAX)

        fun getCaptureMixGain(context: Context): Float = runCatching {
            context.getSharedPreferences(CAPTURE_MIX_PREFS, Context.MODE_PRIVATE)
                .getFloat(CAPTURE_MIX_KEY, CAPTURE_MIX_DEFAULT)
        }.getOrDefault(CAPTURE_MIX_DEFAULT).let { clampCaptureMixGain(it) }

        fun setCaptureMixGain(context: Context, value: Float) {
            val v = clampCaptureMixGain(value)
            runCatching {
                context.getSharedPreferences(CAPTURE_MIX_PREFS, Context.MODE_PRIVATE)
                    .edit().putFloat(CAPTURE_MIX_KEY, v).apply()
            }
        }
    }

    private val lock        = ReentrantLock()
    private val engineRef   = AtomicReference<CaptureEngine?>(null)
    private val projRef     = AtomicReference<MediaProjection?>(null)
    private val running     = AtomicBoolean(false)

    @Volatile private var wakeLock: PowerManager.WakeLock? = null
    private var retryAttempts = 0
    private var retryHandler: Handler? = null
    private var retryThread: HandlerThread? = null

    // Telemetría en vivo 1 Hz (v2.5.410, IVANNA.Telemetry): la 407 solo la
    // arrancaba en IvannaBridgePlayer, pero la ruta en vivo es ESTA
    // (captura por MediaProjection, Tidal) — sin poller aquí, el logcat
    // de telemetría quedaba en silencio mientras suena la música.
    // routeAAsrc=null: la captura no usa ASRC ni Ψ (viven solo en el
    // BridgePlayer); el shed/amounts/niveles sí se leen en ambas rutas.
    // Scope propio del servicio: muere en onDestroy, nunca en el hilo RT.
    private val telemetryScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var telemetryJob: Job? = null

    // FIX (causa raíz del eco/desface reproducción original + procesada):
    // este servicio capturaba el audio de Tidal por software y reproducía su
    // propia copia procesada, pero nunca solicitaba audio focus — Tidal
    // seguía sonando a volumen pleno en paralelo, sin desfase controlado, y
    // por eso el usuario tenía que "encontrar a tanteo" un mix ratio manual
    // en los sliders para evitar el eco (Haas effect improvisado). Con
    // AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK, Android le pide a Tidal que baje su
    // propio volumen mientras IVANNA reproduce el resultado procesado, en vez
    // de sumar dos señales casi-idénticas a volumen pleno.
    private val audioCallbackManager: AudioCallbackManager by lazy {
        AudioCallbackManager(getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager)
    }

    private val perceptualCortex: PerceptualCortex
        get() = (application as IVANNAApplication).perceptualCortex


    override fun onCreate() {
        super.onCreate()

        perceptualCortex.addStateListener(this)
        createNotificationChannel()
        acquireWakeLock()
        retryThread = HandlerThread("RetryHandler", Process.THREAD_PRIORITY_BACKGROUND)
            .also { it.start(); retryHandler = Handler(it.looper) }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent == null) { stopSelf(); return START_NOT_STICKY }
        startForeground(
            NOTIFICATION_ID, buildNotification(),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
        )
        val projection = getMediaProjection(intent)
        if (projection == null) {
            Log.w(TAG, "MediaProjection no autorizada")
            stopForeground(STOP_FOREGROUND_REMOVE); stopSelf()
            return START_NOT_STICKY
        }
        // v2.5.374 — NO pedir duck en el modelo DELTA (LEGACY_HAAS_MIX=false):
        // CAUSA RAÍZ de "al abrir IVANNA el volumen BAJA, al cerrarla SUBE"
        // (Moto G85, 2026-11-09). AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK le pide
        // a YouTube Music/TIDAL que baje su propio volumen (~-14 dB, el
        // duck estándar del sistema). En la mezcla Haas LEGACY eso tenía
        // sentido: el original bajaba y la copia procesada al 0.40 lo
        // reemplazaba. Pero en el modelo DELTA actual el seco suena por su
        // vía al 100% y solo se reinyecta g*(procesado-seco): si la fuente
        // está ducked, lo capturado YA está bajo, el delta se calcula sobre
        // lo bajo y el total queda bajo — el delta NO puede restaurar el
        // nivel. Por eso YouTube Music (que sí hace duck) sonaba muy bajo
        // y TIDAL (que apenas hace duck) sonaba fuerte. Sin duck, la fuente
        // suena a volumen pleno y el delta solo añade la diferencia.
        // Solo se pide duck en el modelo Haas legacy (si alguien lo
        // reactiva), donde la copia procesada reemplaza al original.
        if (CaptureEngine.LEGACY_HAAS_MIX) {
            runCatching { audioCallbackManager.requestAudioFocus(duck = true) }
        }
        startEngine(projection)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        // FIX PR-2: desregistrarse del listener
        runCatching { perceptualCortex.removeStateListener(this) }
        runCatching { audioCallbackManager.abandonAudioFocus() }
        stopEngine()
        releaseWakeLock()
        telemetryJob?.cancel()
        telemetryJob = null
        telemetryScope.cancel()
        retryHandler?.removeCallbacksAndMessages(null)
        retryThread?.quitSafely()
        _isCapturing.value = false

        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    // ── AUDIT FIX PR 2: Implementación de PerceptualStateListener ──────────────
    /**
     * Recibir cambios de estado perceptual de PerceptualCortex.
     * Ajusta dinámicamente buffer, sensibilidad de captura, etc.
     *
     * @param state Nuevo PerceptualState calculado
     * @param deltaMs Tiempo desde última actualización
     */
    override fun onPerceptualStateChanged(state: PerceptualState, deltaMs: Long) {
        try {
            // Ajustar tamaño de buffer basado en fatiga auditiva
            // Si hay fatiga alta, aumentar buffer para suavizar ruido
            // Si hay fatiga baja, mantener buffer mínimo para latencia baja
            val fatigueLevel = state.fatigue.cumulativeSessionFatigueScore ?: 0.5f
            val adaptiveBufferSize = if (fatigueLevel > 0.6f) 4096 else 2048

            // Log del cambio
            if (com.ivanna.omega.BuildConfig.DEBUG) Log.d(TAG, "Perceptual update: fatigue=${String.format("%.2f", fatigueLevel)}, " +
                "adaptiveBuffer=$adaptiveBufferSize, emotion=${state.emotion}")

            if (com.ivanna.omega.core.IvannaNativeLib.isLoaded) {
                runCatching {
                    com.ivanna.omega.core.IvannaNativeLib.nativeSetFatigueProtection(
                        state.dsp.iso226Compensation,
                        state.dsp.fatigueProtection
                    )
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error aplicando estado perceptual: ${e.message}")
        }
    }

    private fun startEngine(projection: MediaProjection) {
        lock.withLock {
            stopEngineLocked()
            projRef.set(projection)
            val engine = CaptureEngine(
                context    = applicationContext,
                projection = projection,
                onError    = { msg ->
                    Log.e(TAG, "CaptureEngine error: $msg")
                    scheduleRestart()
                },
                onProjLost = {
                    Log.w(TAG, "Proyección perdida – programando reinicio")
                    scheduleRestart()
                }
            )
            engineRef.set(engine)
            engine.start()
            running.set(true)
            _isCapturing.value = true
            retryAttempts = 0
            // Telemetría en vivo 1 Hz (v2.5.410): arranca con la captura.
            // ASRC + Ψ EN CAPTURA (v2.5.412): el poller recibe el ASRC de
            // esta sesión (antes null: asrc=0, psi en defaults). Ahora
            // asrc=1 y psi_eta/psi_coh/psi_active son vivos en captura.
            // Mezcla AJUSTABLE (v2.5.412): el poller recibe la ganancia
            // aplicada (rampa) para exponerla como `mix=` en la línea.
            telemetryJob?.cancel()
            telemetryJob = telemetryScope.startTelemetryPoller(
                routeAAsrc = engine.asrc,
                appContext = this,
                mixGainProvider = { engine.currentMixGain },
                // v2.5.415, bandera 1: el poller expone los internos del
                // servo (deriva medida, ocupación, fase, demanda, riel).
                pllProvider = { engine.pll }
            )
        }
    }

    private fun stopEngine() = lock.withLock { stopEngineLocked() }

    private fun stopEngineLocked() {
        telemetryJob?.cancel()
        telemetryJob = null
        engineRef.getAndSet(null)?.let { engine ->
            engine.stop()
            engine.cleanup()
        }
        running.set(false)
        _isCapturing.value = false
    }

    private fun scheduleRestart() {
        lock.withLock { stopEngineLocked() }
        val savedProj = projRef.get() ?: run {
            Log.e(TAG, "Sin proyección — imposible reiniciar"); stopSelf(); return
        }
        // FIX: más allá de MAX_RESTART_ATTEMPTS, savedProj sigue siendo el
        // mismo objeto ya invalidado por el sistema — seguir reintentando
        // es un ciclo garantizado a fallar para siempre. Rendirse aquí,
        // avisar al usuario, y detener el servicio limpio en vez de seguir
        // consumiendo batería sin poder recapturar audio jamás.
        if (retryAttempts >= MAX_RESTART_ATTEMPTS) {
            Log.w(TAG, "Reintentos agotados ($retryAttempts) — MediaProjection " +
                "invalidada de forma permanente, requiere nuevo consentimiento " +
                "del usuario. Deteniendo el servicio.")
            notifyCaptureGaveUp()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return
        }
        val delayMs = when {
            retryAttempts < 3 -> 1_000L
            retryAttempts < 6 -> 5_000L
            else              -> 30_000L
        }
        retryAttempts++
        Log.i(TAG, "Reinicio #$retryAttempts en ${delayMs}ms")
        retryHandler?.postDelayed({ startEngine(savedProj) }, delayMs)
    }

    /**
     * Notificación honesta cuando el servicio se rinde: la captura de audio
     * del sistema (Tidal/Qobuz/YouTube → DSP) dejó de funcionar y necesita
     * que el usuario vuelva a abrir IVANNA y conceda el permiso de nuevo —
     * MediaProjection nunca puede auto-otorgarse por diseño de seguridad
     * de Android, así que esto no es algo que el propio servicio pueda
     * resolver solo. Notificación aparte (GIVEUP_NOTIFICATION_ID) — no
     * reemplaza a la del foreground mientras este termina de detenerse.
     */
    private fun notifyCaptureGaveUp() {
        runCatching {
            val pi = PendingIntent.getActivity(
                this, 0,
                Intent(this, Class.forName("com.ivanna.omega.MainActivity")),
                PendingIntent.FLAG_IMMUTABLE
            )
            val n = NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("IVANNA dejó de procesar el audio")
                .setContentText("Toca para volver a activar la captura del sistema.")
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .setContentIntent(pi)
                .setOngoing(false)
                .setAutoCancel(true)
                .build()
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .notify(GIVEUP_NOTIFICATION_ID, n)
        }.onFailure { Log.w(TAG, "notifyCaptureGaveUp: ${it.message}") }
    }

    private fun acquireWakeLock() {
        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(
            PowerManager.PARTIAL_WAKE_LOCK, "ivanna:playback_capture"
        ).apply {
            setReferenceCounted(false)
            acquire(10 * 60 * 60 * 1000L)
        }
    }

    private fun releaseWakeLock() {
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
    }

    private fun getMediaProjection(intent: Intent): MediaProjection? {
        val code = intent.getIntExtra("resultCode", -1)
        val data = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra("data", Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra("data")
        } ?: return null
        return (getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager)
            .getMediaProjection(code, data)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID, "IVANNA Playback Capture", NotificationManager.IMPORTANCE_LOW
            ).apply { setSound(null, null); enableVibration(false) }
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(ch)
        }
    }

    private fun buildNotification(): Notification {
        val pi = PendingIntent.getActivity(
            this, 0,
            Intent(this, Class.forName("com.ivanna.omega.MainActivity")),
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA OMEGA SUPREME")
            .setContentText("DSP activo — Tidal / Qobuz / YouTube procesados")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }

    private class CaptureEngine(
        private val context:    Context,
        private val projection: MediaProjection,
        private val onError:    (String) -> Unit,
        private val onProjLost: () -> Unit
    ) {
        private var audioRecord:  AudioRecord? = null
        private var audioTrack:   AudioTrack?  = null
        private var audioSessionId = 0

        private var workerThread:  HandlerThread? = null
        private var workerHandler: Handler?       = null
        @Volatile private var active = false

        // DESACOPLE LECTOR/DSP (v2.5.375, sysreport 2026-11-09 17:25 CST:
        // 173 SOBRECARGAs, load avg ~7-8/8, TIDAL+YTM a la vez): el hilo
        // lector (CaptureReader, URGENT_AUDIO) SOLO lee de AudioRecord y
        // publica en el anillo; el hilo DSP (CaptureWorker) consume del
        // anillo. Un DSP lento o una escritura bloqueante ya no retrasan
        // la siguiente lectura ni desbordan el RecordThread. 16 bloques
        // = ~107 ms de colchon, que se suman a los 53 ms del anillo de
        // AudioRecord. Sin el desacople, bajo saturacion del sistema el
        // shed por si solo no bastaba: el lector seguia atado al ritmo
        // del DSP. Ver CaptureBlockRing (logica pura, test JVM).
        private var readerThread:  HandlerThread? = null
        private var readerHandler: Handler?       = null
        private val blockRing = CaptureBlockRing(slots = 16, blockSamples = BLOCK_SAMPLES)

        private val vibratoryProcessor = OmegaVibratoryProcessor(1.2f, 0.92f)
        private val spatialEngine      = SpatialAudioEngineV2()
        private var voiceProtection:   VoiceProtectionController? = null
        private var voiceController:   VoiceController?           = null

        private val voiceWindow = FloatArray(VOICE_WINDOW_SAMPLES)
        // CAUSA REAL DE TRONIDOS/MICROCORTES PERIODICOS (~1 Hz): la clasificacion
        // YAMNet (TFLite sobre 15600 muestras) + pushYamnetScores() (socket al
        // daemon con soTimeout de 2 s) + executeCommand() corrian SINCRONAS en
        // el hilo URGENT_AUDIO, una vez por cada ventana llena (~0.98 s). Mientras
        // duraban, el hilo no leia AudioRecord (overrun -> hueco en la captura)
        // ni escribia al AudioTrack (underrun -> silencio), y con un decoder de
        // video compitiendo por CPU la pausa crecia. Ahora la ventana se COPIA
        // (memcpy de ~60 KB) a un snapshot y la inferencia corre en un hilo de
        // fondo; si aun esta ocupado se descarta esa ventana (nunca se encola).
        private val voiceSnapshot = FloatArray(VOICE_WINDOW_SAMPLES)
        private val voiceBusy = AtomicBoolean(false)
        private var voiceThread:  HandlerThread? = null
        private var voiceHandler: Handler?       = null
        private val voiceTask = Runnable { runVoiceClassification() }
        private var voiceFill  = 0
        private var voiceAcc   = 0f
        private var voiceCount = 0

        // AUDIT FIX (realtime allocation): buffers deinterleaved L/R
        // preasignados una vez para el bloque spatial. Antes se creaban 4
        // FloatArray(frames) por iteracion del loop (~93 iteraciones/seg
        // a 48 kHz / 512 frames) -> presion continua sobre el GC en el
        // hilo THREAD_PRIORITY_URGENT_AUDIO -> jitter y XRun.
        // Tamanyo fijo BLOCK_FRAMES: AudioRecord nunca entrega mas de
        // BLOCK_SAMPLES bytes por read() por como esta configurado.
        // FIX (eco/desface — determinístico): la captura reproduce el audio
        // procesado por su PROPIO AudioTrack y el original de Tidal sigue
        // sonando a nivel pleno (no existe API pública para silenciarlo).
        // Dos copias de la misma señal con latencias distintas y nivel
        // comparable = comb filtering + eco discreto (el "desface" a 100/100).
        //
        // Punto dulce empírico validado en dispositivo: original 100% +
        // procesado 40% (−8.0 dB). A ese nivel relativo el efecto Haas
        // (precedence effect) funde la copia atenuada con la principal: se
        // percibe como cuerpo/densidad, NO como eco, y ambas rutas son
        // estéreo completo — no se pierde la imagen estéreo. Este es ahora
        // el comportamiento por defecto: la ganancia del stream procesado
        // arranca en 0 y sube con rampa per-sample hasta HAAS_SAFE_GAIN,
        // sin que el usuario tenga que buscar el punto a mano.
        //
        // La rampa interpola la ganancia muestra a muestra dentro del bloque
        // (no por bloque) → cero zipper noise y cero tronido al conmutar.
        // @Volatile: la telemetría (hilo Default) lee mixGain vía
        // currentMixGain mientras el hilo de audio lo escribe.
        @Volatile private var mixGain = 0f                 // nivel actual del stream procesado
        private var mixGainTarget = HAAS_SAFE_GAIN     // objetivo mientras la captura está activa
        // Mezcla AJUSTABLE (v2.5.412): objetivo del usuario (slider). Se
        // carga en start() y se re-lee throttled (250 ms) en el bucle; el
        // hilo de audio solo hace getFloat (mapa en memoria, sin I/O).
        @Volatile var userMixGain: Float = CAPTURE_MIX_DEFAULT
        private var mixPrefs: SharedPreferences? = null

        /** ASRC de esta sesión de captura (para la telemetría 1 Hz). Null antes de start(). */
        val asrc: RouteAAsrc?
            get() = captureAsrc

        /** Ganancia de mezcla aplicada ahora mismo (rampa), para la telemetría `mix=`. */
        val currentMixGain: Float
            get() = mixGain

        // MISION HAAS — atacar latencia, no ganancia (2026-09-18):
        // histeresis del resync anti-deriva. Sin ella, mientras la deriva
        // persiste, el pause/flush/play podia dispararse en cada chequeo de
        // 250 ms: cada flush vacia el AudioTrack = HUECO de salida audible
        // (los "micro cortes" reportados). Minimo 750 ms entre resyncs da
        // tiempo a que la cola se rellene tras el flush y el proximo chequeo
        // mida el estado REAL, no la cola vacia recien flusheada.
        private var resyncCount = 0
        // Cooldown (ms, reloj monotonico) del UNICO resync que queda, el de
        // publishHaasTelemetry(). d419777a lo uso sin declararlo -> "Unresolved
        // reference" en compileDebugKotlin: ese era el CI rojo.
        private var lastResyncMs = 0L
        // true => forzar el delta simple (diagnostico A/B). Normalmente false.
        @Volatile private var deltaFallback = false

        // REFINAMIENTO MAGISTRAL: Controladores de Latencia, Timing, Anti-Pop, Presupuesto y Gobernador
        private val adaptiveLatency    = AdaptiveLatencyController(SAMPLE_RATE)
        private val masterTiming       = MasterTimingController(SAMPLE_RATE)
        private val antiPopEngine      = ProfessionalAntiPopEngine(SAMPLE_RATE)
        private val budgetGuard        = AudioThreadBudgetGuard()
        @Volatile private var trackMuted = false
        // FIX (micro-cortes/crujido): RouteArbiter.systemWideEffectProcessing() lee el
        // beacon del efecto (JNI + open/mmap del archivo beacon). Se ejecutaba en el hilo
        // de audio EN CADA bloque (~150/s); cualquier demora de I/O con CPU cargada
        // vaciaba el AudioTrack -> underrun audible. Ahora se cachea y se refresca cada 2 s
        // en un hilo aparte; el hilo de audio solo lee un booleano.
        @Volatile private var systemWideCached = false
        @Volatile private var systemWideCheckedAtMs = 0L
        private val systemWideRefreshing = java.util.concurrent.atomic.AtomicBoolean(false)
        private val predictiveGovernor = PredictiveLoadGovernor(SAMPLE_RATE)

        // ASRC MAGISTRAL + Ψ EN CAPTURA (v2.5.412): el ASRC nativo v6
        // (RouteAAsrc: FIR 319 taps + FIR Ψ 64 taps/oído) como corrector
        // de tasa en el write-stage, espejo de IvannaBridgePlayer. Sin
        // él, la captura corregía la deriva solo con micro-slips y Ψ no
        // corría (telemetría asrc=0, psi_active=0). Se crea en start() y
        // se cierra en stop()/cleanup(); si el nativo no carga,
        // processInto() es passthrough y la captura sigue como hoy.
        private var captureAsrc: RouteAAsrc? = null
        // Servo de reloj propio de la captura (instancia independiente de
        // la del BridgePlayer: cada uno disciplina su propio AudioTrack).
        private var capturePll: AudioTrackBufferPll? = null
        // Acceso de solo lectura para el poller de telemetría (el servicio
        // lo lee vía engine.pll; el campo sigue siendo privado y mutable
        // solo dentro del engine). Fix v2.5.415: `capturePll` no existe en
        // el scope del servicio, solo dentro de CaptureEngine.
        val pll: AudioTrackBufferPll? get() = capturePll
        // Frames entregados al track a 48 kHz (dominio del PLL). Se cuentan
        // en el bucle, no en writeAllToTrack (que también escribe silencio
        // de mute — esos frames del bloque sí se cuentan como submitted).
        // v2.5.415, bandera 1 de la 414: el silencio de PRIMING del colchón
        // y los REPRIMES tras underrun se escribían al track SIN contarlos
        // (writeSilence fuera del bucle): el PLL veía la cola más vacía de
        // lo real (sesgo positivo de hasta ~colchón/capacidad en
        // phaseError) y su estima de cola era sistemáticamente falsa.
        // Ahora se cuentan convertidos al dominio 48 kHz (ver abajo).
        private var captureSubmittedFrames48k = 0L
        private var lastPllPollNs = 0L
        // Throttle del aviso de riel del PLL (v2.5.415): 1/s, no por poll.
        private var lastPllRailWarnMs = 0L
        // Salida del ASRC: capacidad inFrames*1.002+32 del wrapper + margen.
        private val asrcOutBuf = FloatArray((BLOCK_FRAMES + 64) * CHANNEL_COUNT)
        // Primer fallo del ASRC: se loguea una sola vez (nunca por bloque).
        @Volatile private var asrcFirstFailureLogged = false

        private val rtSpatialInL  = FloatArray(BLOCK_FRAMES)
        private val rtSpatialInR  = FloatArray(BLOCK_FRAMES)
        private val rtSpatialOutL = FloatArray(BLOCK_FRAMES)
        private val rtSpatialOutR = FloatArray(BLOCK_FRAMES)

        private val projCallback = object : MediaProjection.Callback() {
            override fun onStop() = onProjLost()
        }

        init {
            projection.registerCallback(projCallback, null)
        }

        fun start() {
            // GUARDA REAL (2026-09-17, revisada 2026-11-09): antes de duplicar el
            // stream con captura+reproceso propio, comprobar si el efecto de
            // sistema "omega_effect" (libomega_effect.so, instalado vía
            // magisk_module/system/etc/audio_effects_ivanna_omega.xml como
            // postprocess del stream "music") está REALMENTE PROCESANDO en el
            // dispositivo. Si AudioFlinger ya lo aplica en sitio sobre TODO el
            // stream music (Tidal incluido), esta captura estaría reprocesando
            // una señal que YA fue procesada y reproduciéndola encima del
            // original — eco digital garantizado, e independiente de
            // bocina/audífonos (coincide exactamente con el síntoma reportado).
            // La evidencia es el beacon del propio efecto
            // (RouteArbiter.systemWideEffectProcessing() ->
            // DSPBridge.effectState()): ENABLED_IDLE o PROCESSING. El mero
            // registro en AudioEffect.queryEffects() YA NO BASTA: en el Moto G85
            // (2026-11-09) el efecto estaba registrado pero mudo (nunca
            // inicializó su beacon porque no pudo crear
            // /data/local/tmp/omega_effect_beacon) y abortar solo por registro
            // dejaba a IVANNA sin procesar en ninguna ruta.
            val omegaProcessing = RouteArbiter.systemWideEffectProcessing()
            if (omegaProcessing) {
                val reason = "omega_effect procesando como postprocess global (beacon: efecto habilitado/procesando)"
                Log.w(TAG, "$reason — se aborta la captura+reproceso propia para evitar doble procesamiento.")
                onError("$reason; se mantiene la salida segura sin reinyección por software.")
                return
            }
            // Ruta B registrada pero sin evidencia de procesamiento (o no
            // registrada): Ruta A procede. Se deja constancia para diagnóstico;
            // el refresco periódico de systemWideCached reevalúa cada 2 s y
            // Ruta A retoma/silencia sola si Ruta B cambia de estado.
            when (RouteArbiter.systemWideEffectPresence()) {
                true -> Log.i(TAG, "omega_effect registrado pero sin evidencia de procesamiento (beacon mudo) — Ruta A procede; el tick periódico reevaluará Ruta B.")
                null -> Log.i(TAG, "no se pudo verificar la presencia de omega_effect; sin evidencia de procesamiento — Ruta A procede.")
                false -> { /* efecto no registrado: nada que reportar */ }
            }
            if (!setupHardware()) {
                cleanupHardwareOnly()
                val sysCap: SystemAudioCapture = SystemAudioCapture.getInstance(context)
                runCatching { sysCap.startCapture() }
                onError("Hardware de audio no inicializado")
                return
            }
            IvannaVisualizerBridge.init(SAMPLE_RATE, BLOCK_FRAMES)
            IvannaVisualizerBridgeV2.init(SAMPLE_RATE, BLOCK_FRAMES)
            IvannaVisualizerBark64Bridge.init(SAMPLE_RATE, BLOCK_FRAMES)
            val estDeviceLatencyMs = (BLOCK_FRAMES * 1000f) / SAMPLE_RATE.toFloat()
            IvannaVisualizerBridge.setDeviceLatencyMs(estDeviceLatencyMs)
            IvannaVisualizerBridgeV2.setDeviceLatencyMs(estDeviceLatencyMs)
            // FIX: arrancar medición automática Lab (THD/LUFS/SNR cada 30s)
            IvannaLabMonitor.startAutoMeasure()
            voiceController = VoiceController(context)
            voiceThread = HandlerThread("IvannaVoiceClassifier", Process.THREAD_PRIORITY_BACKGROUND).also { it.start() }
            voiceHandler = Handler(voiceThread!!.looper)
            voiceBusy.set(false)
            voiceProtection = VoiceProtectionController(context)
            spatialEngine.start()
            workerThread = HandlerThread("CaptureWorker", Process.THREAD_PRIORITY_URGENT_AUDIO)
            workerThread?.start()
            workerHandler = Handler(workerThread!!.looper)
            active = true
            // Anillo lector->DSP limpio en cada (re)arranque; el lector
            // arranca antes de postear el bucle DSP para que haya datos.
            blockRing.reset()
            readerThread = HandlerThread("CaptureReader", Process.THREAD_PRIORITY_URGENT_AUDIO)
            readerThread?.start()
            readerHandler = Handler(readerThread!!.looper)
            // Rampa limpia en cada (re)arranque del motor: el procesado entra
            // desde 0 hasta el nivel de fusión Haas en ~0.5 s.
            resetMixRamp()
            // v2.5.0: motor de reinyeccion diferencial (nativo). prepare() reserva memoria: aqui,
            // fuera del hilo de audio. Si falla/no carga, el bucle cae al delta simple.
            runCatching {
                DSPBridge.reinjectPrepare(SAMPLE_RATE, BLOCK_FRAMES)
                // Topes por banda: graves conservadores (estabilidad/sin cancelaciones), medios con cuerpo, agudos libres.
                DSPBridge.reinjectSetBandCaps(REINJECT_CAP_LOW, REINJECT_CAP_MID, REINJECT_CAP_HIGH)
            }
            // FIX (desface acumulado, p.ej. al pausar/reanudar video): al
            // (re)arrancar el motor se purga la cola del AudioTrack — si se
            // reusa un track con frames viejos en cola, suenan DESPUÉS del
            // audio nuevo y el desface crece en cada pausa/seek del video.
            runCatching { audioTrack?.pause(); audioTrack?.flush(); audioTrack?.play() }
            adaptiveLatency.reset()
            masterTiming.reset()
            antiPopEngine.reset()
            budgetGuard.reset()
            predictiveGovernor.reset()
            // ASRC MAGISTRAL + Ψ EN CAPTURA (v2.5.412): una instancia por
            // sesión de captura (estado FIR + Ψ locales, sin compartir con
            // el BridgePlayer). Si el nativo no carga, isAvailable=false y
            // processInto() es passthrough: la captura sigue como hoy.
            captureAsrc = RouteAAsrc().also { it.setSampleRate(SAMPLE_RATE.toFloat()) }
            capturePll = AudioTrackBufferPll(SAMPLE_RATE)
            captureSubmittedFrames48k = 0L
            lastPllPollNs = 0L
            asrcFirstFailureLogged = false
            Log.i(TAG, "ASRC captura: disponible=${captureAsrc?.isAvailable == true} (Ψ viva por defecto)")
            // Mezcla AJUSTABLE (v2.5.412): objetivo del usuario (slider),
            // persistido en ivanna_capture. Default 0.60 = DELTA_BLEND_SOLO.
            mixPrefs = context.getSharedPreferences(CAPTURE_MIX_PREFS, Context.MODE_PRIVATE)
            userMixGain = clampCaptureMixGain(
                mixPrefs?.getFloat(CAPTURE_MIX_KEY, CAPTURE_MIX_DEFAULT) ?: CAPTURE_MIX_DEFAULT
            )
            workerHandler?.post(processingLoop)
            readerHandler?.post(readerLoop)
        }

        fun stop() {
            active = false
            Log.i(TAG, "Anillo captura->DSP: ofrecidos=${blockRing.offeredBlocks} descartados=${blockRing.droppedBlocks} consumidos=${blockRing.polledBlocks} pendientes=${blockRing.pending()}")
            // ASRC MAGISTRAL + Ψ EN CAPTURA (v2.5.412): cerrar siempre
            // (espejo del finally de BridgePlayer); close() es idempotente.
            runCatching { captureAsrc?.close() }
            captureAsrc = null
            capturePll = null
            masterTiming.reset()
            antiPopEngine.reset()
            val sysCap: SystemAudioCapture = SystemAudioCapture.getInstance(context)
            runCatching { sysCap.stopCapture() }
            IvannaVisualizerBridge.reset()
            IvannaVisualizerBridgeV2.reset()
            IvannaVisualizerBark64Bridge.reset()
            readerHandler?.removeCallbacksAndMessages(null)
            readerThread?.quitSafely()
            readerHandler = null
            readerThread  = null
            workerHandler?.removeCallbacksAndMessages(null)
            workerThread?.quitSafely()
            workerHandler = null
            workerThread  = null
            voiceHandler?.removeCallbacksAndMessages(null)
            voiceThread?.quitSafely()
            voiceHandler = null
            voiceThread  = null
        }

        fun cleanup() {
            adaptiveLatency.reset()
            masterTiming.reset()
            antiPopEngine.reset()
            budgetGuard.reset()
            predictiveGovernor.reset()
            // ASRC + Ψ EN CAPTURA (v2.5.412): cerrar si stop() no lo hizo
            // (close() es idempotente).
            runCatching { captureAsrc?.close() }
            captureAsrc = null
            capturePll = null
            (context.applicationContext as? IVANNAApplication)?.let { app ->
                if (audioSessionId > 0) {
                    app.globalEffectManager.closeSession(audioSessionId)
                    audioSessionId = 0
                }
            }
            audioTrack?.stop();  audioTrack?.release();  audioTrack  = null
            audioRecord?.stop(); audioRecord?.release(); audioRecord = null
            spatialEngine.stop()
            voiceProtection?.release(); voiceProtection = null
            IvannaVisualizerBridge.release()
            IvannaVisualizerBridgeV2.release()
            IvannaVisualizerBark64Bridge.release()
            IvannaLabMonitor.stopAutoMeasure()
            runCatching { projection.unregisterCallback(projCallback) }
            voiceFill = 0; voiceAcc = 0f; voiceCount = 0
        }

        private fun cleanupHardwareOnly() {
            audioTrack?.release();  audioTrack  = null
            audioRecord?.release(); audioRecord = null
            // HI-RES 192 kHz: soltar el resampleador y volver a 48 kHz; setupHardware()
            // decide de nuevo la tasa de pista en el próximo arranque del motor.
            hiresResampler = null
            trackSampleRate = SAMPLE_RATE
            trackBlockFrames = BLOCK_FRAMES
        }

        /**
         * Decide la tasa de la pista de reinyección (HI-RES 192 kHz, v2.5.376).
         * Default OFF: 48 kHz (comportamiento actual, cero regresión).
         * ON + DAC USB: la mayor tasa anunciada por el DAC en [48k, 192k];
         * fallback 48 kHz si no hay DAC USB o no anuncia > 48 kHz.
         * Devuelve (tasa, dispositivo USB o null).
         */
        private fun selectReinjectTrack(): Pair<Int, AudioDeviceInfo?> {
            if (!HiResAudioManager.isReinject192kEnabled(context)) {
                return SAMPLE_RATE to null
            }
            val am = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
            val outputs = runCatching { am?.getDevices(AudioManager.GET_DEVICES_OUTPUTS) }
                .getOrNull().orEmpty()
            val usb = outputs.firstOrNull {
                it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                it.type == AudioDeviceInfo.TYPE_USB_HEADSET ||
                it.type == AudioDeviceInfo.TYPE_USB_ACCESSORY
            } ?: return SAMPLE_RATE to null
            val rate = HiResAudioManager.selectReinjectRate(usb.sampleRates, true)
            return rate to usb
        }

        private fun setupHardware(): Boolean {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
            val captureBuilder = AudioPlaybackCaptureConfiguration.Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .excludeUid(Process.myUid())

            // FIX CINEMA / AMAZON PRIME VIDEO: Excluir UIDs de apps de video/cine instaladas dinámicamente.
            // Esas apps reciben procesamiento in-place de latencia cero vía IvannaGlobalEffectManager (AudioEffect)
            // o libomega_effect en AudioFlinger. Capturarlas por loopback duplicaría su audio con 25-40ms de desfase.
            // FIX (Atlas/4D en 0% con música, 2026-11-09): NO excluir apps de MÚSICA del loopback.
            // El keyword "youtube" capturaba YouTube Music (com.google.android.apps.youtube.music) y "amazon"
            // capturaba Amazon Music (com.amazon.mp3). Al excluirlas, el AudioRecord leía silencio aunque
            // sonara música: el DSP se veía "ACTIVO" (procesando silencio) pero el Atlas mostraba
            // NEUTRAL/SILENCIO 0% y la 4D 0% siempre, porque el IME y la cadena 4D analizaban ceros.
            // Las apps de música NO reciben procesamiento in-place de latencia cero (eso es solo para
            // video), así que DEBEN capturarse por loopback.
            val musicPackages = setOf("com.amazon.mp3") // Amazon Music no lleva "music" en el paquete
            val videoKeywords = listOf(
                "amazon", "prime", "aiv", "avod", "firebat",
                "netflix", "disney", "hbo", "wbd", "max",
                "youtube", "twitch", "plex", "vlc", "videoplayer",
                "kodi", "cinema", "movie", "film"
            )
            runCatching {
                val installed = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    context.packageManager.getInstalledPackages(android.content.pm.PackageManager.PackageInfoFlags.of(0))
                } else {
                    @Suppress("DEPRECATION")
                    context.packageManager.getInstalledPackages(0)
                }
                for (pi in installed) {
                    val pn = pi.packageName.lowercase(java.util.Locale.ROOT)
                    if (pn.contains("music") || pn in musicPackages) continue
                    if (videoKeywords.any { pn.contains(it) }) {
                        pi.applicationInfo?.uid?.let { uid ->
                            captureBuilder.excludeUid(uid)
                            Log.i(TAG, "Excluido de loopback para A/V sync cero-desfase: ${pi.packageName} (uid=$uid)")
                        }
                    }
                }
            }
            val captureConfig = captureBuilder.build()
            val minRec = AudioRecord.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            ).coerceAtLeast(BLOCK_SAMPLES * 4)
            audioRecord = AudioRecord.Builder()
                .setAudioFormat(AudioFormat.Builder()
                    .setSampleRate(SAMPLE_RATE)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                    .build())
                .setBufferSizeInBytes(recordBufferBytes(minRec)) // >= 8 bloques: absorbe jitter sin overrun (ver REC_BUFFER_BLOCKS)
                .setAudioPlaybackCaptureConfig(captureConfig)
                .build()
            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                // v2.5.374 — Rastro del AudioRecord -22: cuando createRecord_l
                // falla ("could not create record track", BAD_VALUE -22 por
                // recurso del HAL ocupado o demasiados record tracks), el
                // constructor deja el estado en UNINITIALIZED sin lanzar.
                // Antes se retornaba false en silencio y el reintento con
                // backoff (scheduleRestart) no dejaba ver la causa. Ahora
                // queda registrado para distinguir un -22 transitorio de un
                // fallo de configuración permanente.
                Log.w(TAG, "AudioRecord no inicializado (posible -22 createRecord_l: HAL ocupado o demasiados record tracks) — reintento con backoff")
                audioRecord?.release(); audioRecord = null; return false
            }
            // HI-RES 192 kHz (v2.5.376): decidir la tasa de la pista ANTES de
            // crear el AudioTrack. La CAPTURA y el DSP siguen a 48 kHz; solo
            // la pista de reinyección sube de tasa. El resampleador se crea
            // una vez por sesión (input 48 kHz) y se aplica en writeAllToTrack().
            val (reinjRate, usbDac) = selectReinjectTrack()
            trackSampleRate = reinjRate
            trackBlockFrames = (BLOCK_FRAMES.toLong() * trackSampleRate.toLong() / SAMPLE_RATE.toLong()).toInt()
            hiresResampler = if (trackSampleRate != SAMPLE_RATE) {
                StereoAudioResampler(trackSampleRate).also { it.setInputSampleRate(SAMPLE_RATE) }
            } else null
            Log.i(TAG, "Reinyección Hi-Res: trackRate=${trackSampleRate} Hz (DSP 48 kHz), resampler=${if (hiresResampler != null) "48k->${trackSampleRate}" else "off"}, dac=${usbDac?.productName ?: "ninguno"}")
            val minTrack = AudioTrack.getMinBufferSize(
                trackSampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            ).coerceAtLeast(trackBlockFrames * CHANNEL_COUNT * Float.SIZE_BYTES)
            // LATENCIA Y SINCRONIZACIÓN AUDIO/VIDEO (PRIME VIDEO / STREAMING):
            // El buffer 6x con PERFORMANCE_MODE_NONE agregaba entre 180ms y 256ms de retardo,
            // separando audiblemente el diálogo del video.
            // Con YAMNet, socket daemon IPC y resync fuera del hilo RT, y CinematicEngine
            // operando con buffers preasignados lock-free (cero GC), el hilo de audio procesa
            // en <1ms por bloque.
            // PERFORMANCE_MODE_LOW_LATENCY habilita FastMixer en AudioFlinger (~5ms).
            // Un buffer de minTrack * 2 (doble buffering HAL) o 4 bloques PCM_FLOAT
            // garantiza estabilidad acústica sin underruns y elimina el desfase A/V (<25ms).
            // HI-RES: el segundo término es 4 bloques DE PISTA en bytes
            // (a 48 kHz = 10240 B, igual que antes; a 192 kHz = 40960 B).
            val targetTrackBuf = maxOf(minTrack * 2, trackBlockFrames * 4 * CHANNEL_COUNT * Float.SIZE_BYTES)
            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build())
                .setAudioFormat(AudioFormat.Builder()
                    .setSampleRate(trackSampleRate)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build())
                .setBufferSizeInBytes(targetTrackBuf)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .build()
            if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
                audioTrack?.release();  audioTrack  = null
                audioRecord?.release(); audioRecord = null
                return false
            }
            // HI-RES: anclar la pista al DAC USB.
            // setPreferredDevice es best-effort (AudioPolicy puede rechazarlo).
            // La salida DIRECTA (sin mezcla/remuestreo del mixer) la consigue
            // AudioFlinger solo: la pista ya está a 192 kHz (tasa que el DAC
            // anuncia) y anclada al DAC, así que crea un hilo de salida directo
            // con ese formato. NOTA: `AudioMixerAttributes` (BIT_PERFECT) NO
            // se usa: `AudioTrack.setAudioMixerAttributes` no es API pública
            // (ausente del SDK; el diff de API 34 no añade métodos a AudioTrack),
            // así que llamarlo rompería la compilación.
            usbDac?.let { dac ->
                val track = audioTrack
                if (track != null && !track.setPreferredDevice(dac)) {
                    Log.w(TAG, "Android rechazó la ruta preferida ${dac.productName}; se conserva la ruta elegida por AudioPolicy")
                }
            }
            // This shared fallback processes captured app audio at SAMPLE_RATE (48 kHz).
            // Keep its time-domain spatial delays on the same clock; BridgePlayer has an isolated engine.
            com.ivanna.omega.spatial.IvannaSpatialEngine.shared.configureSampleRate(SAMPLE_RATE)
            com.ivanna.omega.spatial.IvannaSpatialEngine.shared.reset()
            // HI-RES: los controladores de latencia/timing contabilizan en
            // frames DE PISTA (192 kHz cuando hi-res). A 48 kHz es idéntico a antes.
            adaptiveLatency.configure(trackSampleRate, targetTrackBuf)
            masterTiming.setSampleRate(trackSampleRate)
            // COLCHON DE SALIDA: pista nueva => contador a 0 (antes arrastraba el de la sesion anterior y
            // medía una latencia falsa enorme). Se pre-llena con silencio (señal neutra en la via diferencial)
            // hasta el headroom objetivo, para que un retraso del hilo > 1 bloque no vacie la cola (underrun).
            framesWrittenToTrack = 0L
            lastSeenUnderruns = 0
            loopLoad.reset(); lastLoopStride = 1
            cushionFramesTarget = OutputCushionPolicy.cushionFrames(
                adaptiveLatency.targetHeadroomFrames, audioTrack?.bufferSizeInFrames ?: 0, trackBlockFrames)
            val primed = writeSilence(cushionFramesTarget)
            // v2.5.415: el priming ES audio entregado al track (silencio de
            // colchón): se cuenta en el dominio 48 kHz del PLL, o el servo
            // ve la cola ~colchón más vacía de lo real desde el arranque.
            // primed viene en frames DE PISTA (writeSilence devuelve
            // frames de track); la conversión es la misma que para
            // consumed/capacidad (framePosition × 48k / trackRate).
            if (primed > 0) {
                captureSubmittedFrames48k += primed.toLong() * SAMPLE_RATE / trackSampleRate
            }
            Log.i(TAG, "Colchon de salida: objetivo=%d frames (%.1f ms), escrito=%d, pista=%d frames, trackRate=%d Hz".format(
                cushionFramesTarget, cushionFramesTarget * 1000f / trackSampleRate, primed, audioTrack?.bufferSizeInFrames ?: 0, trackSampleRate))
            audioRecord?.startRecording()
            audioTrack?.play()
            // MISION HAAS (2026-09-18): punto de medicion "antes" — latencia
            // estatica de buffers configurada al arrancar el motor, en ms
            // reales (PCM_FLOAT estereo = 8 B por frame; SAMPLE_RATE Hz).
            // Es el dato base contra el que se compara cualquier tuning
            // posterior de BLOCK_FRAMES o buffers, en dispositivo, via logcat.
            Log.i(TAG, "HaasLatency init: recBuf=%.1f ms, trackBuf=%.1f ms, bloque=%.2f ms, trackRate=%d Hz".format(
                recordBufferBytes(minRec) / 8f / (SAMPLE_RATE / 1000f),
                minTrack / 8f / (trackSampleRate / 1000f),
                BLOCK_FRAMES * 1000f / SAMPLE_RATE,
                trackSampleRate))
            audioSessionId = audioTrack?.audioSessionId ?: 0
            // NO abrir IvannaGlobalEffectManager sobre nuestro propio AudioTrack:
            // PlaybackCaptureService ya ejecuta el pipeline DSP nativo completo
            // (DSPBridge, Cinematic, Spatial, Vibratory) sobre `buffer`. Adjuntar
            // además Equalizer/BassBoost/Virtualizer/LoudnessEnhancer/DynamicsProcessing
            // del sistema duplicaba el procesado y causaba saturación/estática al
            // cambiar de ventana hacia Tidal o YouTube.
            return true
        }

        // HILO LECTOR (v2.5.375): SOLO lee de AudioRecord y publica en el
        // anillo. Nunca espera al DSP, nunca escribe a AudioTrack, nunca
        // toca telemetria: bajo saturacion del sistema (load avg ~7-8/8)
        // es el unico hilo que debe mantener el ritmo de 6.67 ms por
        // bloque, y lo hace porque su trabajo por bloque es un read() +
        // una copia de 640 floats. Si el anillo esta lleno (DSP muy por
        // detras), el bloque se descarta CONTADO en offer(): perder un
        // delta de 6.7 ms es preferible a desbordar el RecordThread (que
        // pierde la captura de origen). Con el gobernador de shed activo
        // los drops deben ser ~0; `stop()` registra el conteo.
        private val readerLoop = Runnable {
            try {
                val scratch = FloatArray(BLOCK_SAMPLES)
                while (active && !Thread.currentThread().isInterrupted) {
                    val rec = audioRecord ?: break
                    val read = rec.read(scratch, 0, BLOCK_SAMPLES, AudioRecord.READ_BLOCKING)
                    if (read < 0) { Log.w(TAG, "AudioRecord error $read — saliendo"); break }
                    if (read == 0) continue
                    blockRing.offer(scratch, read / CHANNEL_COUNT)
                }
            } catch (t: Throwable) {
                Log.e(TAG, "Excepción fatal en loop de lectura: ${t.message}", t)
            } finally {
                // Siempre corre: loop normal (active=false→no-op) o loop muerto (active=true→reinicio)
                if (active) onError("Loop de lectura terminado inesperadamente")
            }
        }

        private val processingLoop = Runnable {
            // try/finally garantiza que onError() se llame incluso si una excepción
            // no capturada salta desde cualquier paso del loop — sin esto la JVM
            // termina el HandlerThread por UncaughtExceptionHandler y el service
            // queda con active=true/_isCapturing=true para siempre (logo encendido,
            // efectos muertos, sin reinicio automático).
            try {
                // AUDIT FIX (realtime allocation): estos dos ya se creaban
                // una sola vez fuera del while — se conservan con margen para micro-slip/extension.
                val buffer = FloatArray(BLOCK_SAMPLES + 8)
                // COMPLEMENTO SIN PEINE: dryRef = captura sin procesar (identica a lo que ya
                // suena por la via original, 0 ms). Solo se reinyecta DELTA = procesado - seco:
                // output = seco(t) + g*(procesado-seco)(t-L). El termino seco(t-L) -fuente
                // del filtro de peine- desaparece; donde el DSP no cambia nada, delta ~ 0 y no
                // se suma nada (cero peine). Es mezcla (1-g)*seco + g*procesado, sin salto de nivel.
                val dryRef   = FloatArray(BLOCK_SAMPLES + 8)
                val deltaBuf = FloatArray(BLOCK_SAMPLES + 8)
                val mono   = FloatArray(BLOCK_FRAMES + 4)
                var blockCounter = 0
                while (active && !Thread.currentThread().isInterrupted) {
                    // DESACOPLE (v2.5.375): este hilo ya NO lee de AudioRecord.
                    // Consume del anillo que alimenta CaptureReader: un DSP
                    // lento o una escritura bloqueante a AudioTrack ya no
                    // retrasan la siguiente lectura ni desbordan el
                    // RecordThread. Anillo vacio (arranque): esperar 1 ms
                    // sin quemar CPU. La copia slot->buffer es el unico
                    // coste anadido (~2.5 KB por bloque) y deja el resto
                    // del cuerpo del bucle intacto.
                    val slot = blockRing.beginRead()
                    if (slot == null) {
                        try { Thread.sleep(1) } catch (_: InterruptedException) { break }
                        continue
                    }
                    val read = slot.frames * CHANNEL_COUNT
                    slot.data.copyInto(buffer, 0, 0, read)
                    blockRing.endRead()
                    if (read <= 0) continue
                    val iterStartNs = System.nanoTime()
                    val frames = read / CHANNEL_COUNT
                    System.arraycopy(buffer, 0, dryRef, 0, read)

                    // OBJETIVO 4 & 5: INICIO MEDICIÓN DE TIEMPO REAL Y GOBERNADOR PREDICTIVO
                    budgetGuard.startBlock(frames, SAMPLE_RATE)
                    predictiveGovernor.updatePrediction(
                        dspActive = true,
                        cinematicModeOrdinal = CinematicEngineHost.activeModeOrdinal,
                        spatialActive = IvannaSpatialEngine.enabled,
                        npeActive = IvannaNpeEngine.isReady,
                        isBluetooth = adaptiveLatency.currentRoute == AdaptiveLatencyController.OutputRouteType.BLUETOOTH,
                        blockFrames = frames
                    )
                    IvannaSpatialEngine.setReducedComplexity(predictiveGovernor.isSpatialComplexityReduced)

                    val nativeMasterActive = DSPBridge.isLoaded

                    // NPE en modo ANÁLISIS: con el master nativo activo el NPE no procesa el audio (ver el
                    // árbitro de abajo), pero su clasificador de género/firma solo avanzaba al procesar:
                    // la UI mostraba "—" / "0 % confidence" para siempre. Se alimenta con la ENTRADA (antes
                    // del DSP) sin modificarla; el coste cuenta en el presupuesto del hilo de audio.
                    // v2.5.374 — Decimado bajo carga (mismo gobernador que los
                    // análisis visuales): el analyze es solo clasificación /
                    // telemetría, no audio, y corre en el hilo de captura. Con
                    // carga normal corre en cada bloque (sin cambios); cuando
                    // la vuelta se acerca al periodo del bloque, corre cada
                    // 2º/4º bloque para no empujar al RecordThread al
                    // desbordamiento ("RecordThread: buffer overflow"). La
                    // clasificación sigue actualizándose cada ~13-27 ms, de
                    // sobra para un clasificador de género/firma.
                    if (nativeMasterActive && IvannaNpeEngine.isReady && loopLoad.runAnalysis(blockCounter)) {
                        budgetGuard.measureStage(AudioThreadBudgetGuard.BudgetStage.VIBRATORY_NPE) {
                            runCatching { IvannaNpeEngine.analyzeInterleavedStereo(buffer, frames) }
                        }
                    }

                    // ETAPA 1: DSP BRIDGE (EQ / Compresor / Exciter / Coclear / 5 Ejes + SafetyLimiter)
                    // Shed de vuelta completa (v2.5.375): si la vuelta ANTERIOR supero el
                    // periodo del bloque (loopOverloaded: carga > 100 %, se libera a <= 95 %),
                    // el DSP no corre en ESTE bloque y `buffer` queda seco (passthrough).
                    // Es el mismo passthrough que ya hace el nativo cuando el mutex esta
                    // tomado: se salta la llamada COMPLETA, nunca media cadena, asi que
                    // ningun IIR/STFT/convolver queda avanzado a medias (el comentario
                    // original —no omitir bloques alternos— se refiere a omitir DENTRO
                    // de la cadena al cambiar de ventana, no a este bypass entero).
                    // Con el buffer seco, dryRef == buffer y la reinyeccion diferencial
                    // aporta ~0: el original sigue sonando por su via, sin silencio y
                    // sin perder muestras; solo se degrada el procesado un bloque para
                    // que la siguiente lectura no se retrase y el RecordThread no se
                    // desborde. El gobernador nativo (DspLoadShedGovernor) hace lo mismo
                    // dentro de DSPBridge.process para la carga del propio DSP; este es
                    // la red de seguridad de la vuelta COMPLETA (cubre tambien la
                    // escritura bloqueante a AudioTrack y la telemetria, que el nativo
                    // no ve). Sin hilos ni buffers nuevos: reutiliza `loopOverloaded`.
                    if (!loopOverloaded) {
                        budgetGuard.measureStage(AudioThreadBudgetGuard.BudgetStage.DSP_BRIDGE) {
                            DSPBridge.process(buffer, frames)
                        }
                    }

                    // Árbitro de Ruta Única: si DSPBridge nativo está activo, ya ejecutó
                    // espacialización C++, Volterra C++, 5 Ejes y SafetyLimiter. Ejecutar además
                    // CinematicEngineHost, IvannaSpatialEngine (Kotlin), IvannaNpeEngine y
                    // vibratoryProcessor sobre la salida limitada causaba triple saturación,
                    // doble HRTF/peine y clipping post-limitador.
                    if (!nativeMasterActive) {
                        // ETAPA 2: CINEMATIC ENGINE (fallback solo cuando el motor C++ no está cargado)
                        budgetGuard.measureStage(AudioThreadBudgetGuard.BudgetStage.CINEMATIC_ENGINE) {
                            runCatching { CinematicEngineHost.processBlock(buffer, read) }
                        }

                        // ETAPA 3: SPATIAL AUDIO (ObjectRenderer + Upmixer C++ o fallback HRTF/WFS)
                        if (IvannaSpatialEngine.enabled) {
                            budgetGuard.measureStage(AudioThreadBudgetGuard.BudgetStage.SPATIAL_AUDIO) {
                                if (com.ivanna.omega.spatial.IvannaSpatialManager.ready) {
                                    runCatching {
                                        com.ivanna.omega.spatial.IvannaSpatialManager.renderBlock(buffer, frames)
                                    }
                                } else if (frames <= rtSpatialInL.size) {
                                    val inL  = rtSpatialInL
                                    val inR  = rtSpatialInR
                                    val outL = rtSpatialOutL
                                    val outR = rtSpatialOutR
                                    for (i in 0 until frames) {
                                        inL[i] = buffer[i * 2]
                                        inR[i] = buffer[i * 2 + 1]
                                    }
                                    runCatching {
                                        IvannaSpatialEngine.shared.processStereoInput(inL, inR, outL, outR, frames)
                                        for (i in 0 until frames) {
                                            buffer[i * 2]     = outL[i]
                                            buffer[i * 2 + 1] = outR[i]
                                        }
                                    }
                                }
                            }
                        }

                        // ETAPA 4: VIBRATORY & NPE (fallback cuando DSPBridge no está cargado)
                        budgetGuard.measureStage(AudioThreadBudgetGuard.BudgetStage.VIBRATORY_NPE) {
                            if (IvannaNpeEngine.isReady) {
                                runCatching { IvannaNpeEngine.processInterleavedStereo(buffer, frames) }
                            }
                            IvannaBridgePlayer.activeInstance?.let { player ->
                                if (player.npeKotlinEnabled) {
                                    runCatching { player.processBlockThroughNpeKotlin(buffer).copyInto(buffer) }
                                }
                            }
                            vibratoryProcessor.process(buffer)
                        }
                    }

                    // Rampa per-sample de la ganancia del stream procesado (Efecto Haas)
                    val gStart = mixGain
                    if (mixGain < mixGainTarget) mixGain = minOf(mixGain + MIX_GAIN_STEP, mixGainTarget)
                    else if (mixGain > mixGainTarget) mixGain = maxOf(mixGain - MIX_GAIN_STEP, mixGainTarget)
                    val gEnd = mixGain
                    if (LEGACY_HAAS_MIX && gEnd < 1f) {
                        if (gStart == gEnd) {
                            for (i in 0 until read) buffer[i] *= gEnd
                        } else {
                            val inv = 1f / read
                            for (i in 0 until read) {
                                buffer[i] *= gStart + (gEnd - gStart) * (i * inv)
                            }
                        }
                    }

                    // OBJETIVO 3: ANTI-POP PROFESIONAL, DC-BLOCKING & SOFT-CEILING FINAL
                    budgetGuard.measureStage(AudioThreadBudgetGuard.BudgetStage.OUTPUT_STAGE) {
                        antiPopEngine.process(buffer, frames)
                        // 4D: háptica de graves sobre el bloque final (sin asignaciones; la vibración corre en otro hilo)
                        BassHaptics.onBlock(buffer, frames)
                        // FIX (clipping post-DSPBridge en Ruta A): CinematicEngineHost,
                        // IvannaSpatialEngine, IvannaNpeEngine y vibratoryProcessor corren
                        // DESPUÉS de DSPBridge.process() (y por tanto después del
                        // SafetyLimiter nativo). Aplicamos la misma curva racional C1
                        // softCeil (-0.1 dBFS = 0.98855f, identidad lineal <= 0.8897f)
                        // para garantizar 0 clips hacia AudioTrack y OmegaMetrics.
                        val ceil = 0.98855f
                        val knee = ceil * 0.9f
                        val range = ceil - knee
                        for (i in 0 until read) {
                            val x = buffer[i]
                            if (!x.isFinite()) {
                                buffer[i] = 0f
                            } else {
                                val ax = kotlin.math.abs(x)
                                if (ax > knee) {
                                    val over = (ax - knee) / range
                                    val y = knee + range * (over / (1f + over))
                                    buffer[i] = if (x < 0f) -y else y
                                }
                            }
                        }
                    }

                    // Cierre de presupuesto del bloque
                    budgetGuard.finishBlock()

                    // Reinyeccion = solo DELTA con rampa per-sample (ver dryRef). `buffer` conserva
                    // el procesado completo para telemetria/visualizadores; `deltaBuf` va al track.
                    val outBuf = if (LEGACY_HAAS_MIX) buffer else deltaBuf
                    if (!LEGACY_HAAS_MIX) {
                        // v2.5.0: reinyeccion DIFERENCIAL nativa. El original suena por su via (0 ms);
                        // aqui solo va g_banda*Delta, alineado con el seco y con la ganancia por banda
                        // limitada por la coherencia medida con el original al retardo real L (anti-peine).
                        // `gEnd` conserva la rampa de arranque/cambio de fuente => es la intensidad.
                        val lat = adaptiveLatency.measuredLatencyMs
                        DSPBridge.reinjectSetLatencyMs(if (lat > 1f) lat else DEFAULT_LISTENER_LATENCY_MS)
                        DSPBridge.reinjectSetIntensity(gEnd)
                        val nativeOk = !deltaFallback &&
                            DSPBridge.reinjectProcess(dryRef, buffer, deltaBuf, frames)
                        if (!nativeOk) {
                            // Respaldo (motor nativo no cargado): delta simple con rampa por muestra.
                            val invN = 1f / read
                            val dg = gEnd - gStart
                            for (i in 0 until read) {
                                val g = gStart + dg * (i * invN)
                                deltaBuf[i] = (buffer[i] - dryRef[i]) * g
                            }
                        }
                    }

                    // OBJETIVOS 1 & 2: SINCRONIZACIÓN A/V Y REGULACIÓN DE DERIVA
                    // Usar la medición cacheada de adaptiveLatency (sin cruzar Binder en cada bloque)
                    val targetHeadroom = adaptiveLatency.targetHeadroomFrames
                    // HI-RES: la cola medida está en frames DE PISTA (192 kHz en hi-res).
                    val currentQueued = (adaptiveLatency.measuredLatencyMs * trackSampleRate / 1000f).toLong().coerceAtLeast(0L)

                    val effectiveFrames = masterTiming.processAndCompensate(
                        inOutBuffer = outBuf,
                        inFrames = frames,
                        targetHeadroomFrames = targetHeadroom,
                        currentQueuedFrames = currentQueued,
                        track = audioTrack
                    ) {
                        val hwPos = audioTrack?.let {
                            runCatching {
                                if (it.getTimestamp(audioTs)) audioTs.framePosition
                                else it.playbackHeadPosition.toLong() and 0xFFFFFFFFL
                            }.getOrDefault(0L)
                        } ?: 0L
                        framesWrittenToTrack = hwPos
                    }

                    val samplesToWrite = effectiveFrames * CHANNEL_COUNT

                    val globalMgr = runCatching { (context.applicationContext as? IVANNAApplication)?.globalEffectManager }.getOrNull()
                    val inPlaceSessionActive = (globalMgr?.activeSessionCount ?: 0) > 0
                    val isVideoActive = globalMgr?.isVideoStreamingActive == true ||
                                        CinematicEngineHost.activeModeOrdinal == 1
                    val nowSwMs = android.os.SystemClock.elapsedRealtime()
                    if (nowSwMs - systemWideCheckedAtMs > 2000L &&
                        systemWideRefreshing.compareAndSet(false, true)) {
                        systemWideCheckedAtMs = nowSwMs
                        Thread({
                            // FIX (Ruta B, Moto G85 2026-11-09): el refresco usa evidencia
                            // REAL de procesamiento (beacon), no el mero registro en
                            // queryEffects(). Si Ruta B queda muda, el beacon deja de
                            // mostrar procesamiento y Ruta A retoma sola la reinyección
                            // en ≤2 s; si Ruta B empieza a procesar, se silencia sola.
                            try { systemWideCached = RouteArbiter.systemWideEffectProcessing() }
                            finally { systemWideRefreshing.set(false) }
                        }, "ivanna-routecheck").apply { isDaemon = true }.start()
                    }
                    val systemWideActive = systemWideCached

                    // FIX CINEMA / AMAZON PRIME VIDEO ZERO-ECHO & ZERO-DESFASE:
                    // Si la sesión ya se procesa in-place a nivel HAL/AudioEffect (IvannaGlobalEffectManager o
                    // libomega_effect en AudioFlinger), o si una app de video/streaming (Amazon Prime, Netflix, etc.)
                    // está activa, mutear la re-inyección por AudioTrack para no duplicar el audio con 25-40ms de retardo.
                    // Esto elimina el efecto Haas (eco de sala y desfase labial) de raíz.
                    // La captura para telemetría (Bark64, NPE, Cortex, Visualizador) sigue 100% activa.
                    // COMPLEMENTO MODULO+APK: con el motor in-place (Magisk/sesion) activo ya no se
                    // silencia la reinyeccion: al ser solo DELTA no duplica el audio ni crea peine.
                    // Solo el video sigue silenciado (desfase labial). Blend algo menor con modulo.
                    // Mezcla AJUSTABLE (v2.5.412): en solo, el objetivo es el
                    // del usuario (slider, default 0.60 = DELTA_BLEND_SOLO);
                    // con módulo sigue DELTA_BLEND_WITH_MODULE (0.45).
                    mixGainTarget = if (inPlaceSessionActive || systemWideActive) DELTA_BLEND_WITH_MODULE else userMixGain
                    if (LEGACY_HAAS_MIX) mixGainTarget = HAAS_SAFE_GAIN
                    val reinject = if (LEGACY_HAAS_MIX) (!inPlaceSessionActive && !systemWideActive && !isVideoActive) else !isVideoActive
                    if (reinject) {
                        // FIX tronido/silencio al cambiar de ventana: antes setVolume(0f)
                        // quedaba pegado para siempre (nadie lo restauraba) y el paso
                        // escritura<->mute era un corte seco. Al reanudar: volumen a 1 y
                        // fade-in lineal sobre el primer bloque (sin salto de onda).
                        if (trackMuted) {
                            audioTrack?.setVolume(1.0f)
                            trackMuted = false
                            val n = samplesToWrite.coerceAtMost(outBuf.size)
                            val inv = 1f / (n / CHANNEL_COUNT).coerceAtLeast(1)
                            var i = 0
                            while (i + 1 < n) {
                                val g = (i / CHANNEL_COUNT) * inv
                                outBuf[i] *= g
                                outBuf[i + 1] *= g
                                i += CHANNEL_COUNT
                            }
                        }
                        // ASRC MAGISTRAL + Ψ EN CAPTURA (v2.5.412): corrector
                        // de tasa en el write-stage, espejo de BridgePlayer.
                        // Consume el delta ya compensado (effectiveFrames a
                        // 48 kHz) y produce asrcFrames a 48 kHz; writeAllToTrack
                        // aplica el resampler hi-res si procede. Sin nativo,
                        // processInto() es passthrough (misma salida que hoy).
                        // Si el ASRC no produce nada (FIFO vacío al arrancar
                        // o error), NO se escribe nada este bloque: el audio
                        // ya está dentro del ASRC y escribir outBuf directo
                        // lo DUPLICARÍA (misma regla que BridgePlayer).
                        val asrc = captureAsrc
                        if (asrc != null && effectiveFrames > 0) {
                            val asrcFrames = runCatching {
                                asrc.processInto(outBuf, effectiveFrames, asrcOutBuf)
                            }.getOrDefault(0)
                            if (asrcFrames > 0) {
                                captureSubmittedFrames48k += asrcFrames.toLong()
                                writeAllToTrack(asrcOutBuf, asrcFrames * CHANNEL_COUNT)
                            } else if (!asrcFirstFailureLogged) {
                                asrcFirstFailureLogged = true
                                Log.w(TAG, "ASRC captura sin salida (0 frames); bloque no escrito (audio dentro del FIFO)")
                            }
                        } else {
                            // Sin ASRC (no creado): comportamiento actual.
                            captureSubmittedFrames48k += effectiveFrames.toLong()
                            writeAllToTrack(outBuf, samplesToWrite)
                        }
                    } else {
                        if (!trackMuted) {
                            trackMuted = true
                            audioTrack?.setVolume(0.0f)
                        }
                        // Antes la pista se quedaba SIN escribir mientras había vídeo: el HAL contaba un underrun
                        // por ciclo de mezcla (cientos/s) y al volver no había colchon. Con volumen 0 se la mantiene
                        // alimentada con ceros: mismo silencio, cero underruns, cola lista al reanudar.
                        // El silencio también es audio escrito: cuenta en el submitted del PLL.
                        captureSubmittedFrames48k += effectiveFrames.toLong()
                        writeAllToTrack(silenceBuf, samplesToWrite.coerceIn(0, BLOCK_SAMPLES))
                    }

                    // Servo de reloj de la captura (v2.5.412): alimenta el ppm
                    // del ASRC desde el PLL (espejo de BridgePlayer). Throttle
                    // 250 ms: getTimestamp() cruza Binder. Sin detección
                    // explícita de reroute: el PLL se auto-resetea ante
                    // discontinuidad de timestamp/posición.
                    val nowPllNs = android.os.SystemClock.elapsedRealtimeNanos()
                    if (lastPllPollNs == 0L || nowPllNs - lastPllPollNs >= 250_000_000L) {
                        lastPllPollNs = nowPllNs
                        // Mezcla AJUSTABLE (v2.5.412): el slider escribe el
                        // pref; el engine lo re-lee aquí (getFloat = mapa en
                        // memoria, sin I/O). La rampa per-sample lleva
                        // mixGain al nuevo objetivo sin zipper noise.
                        mixPrefs?.let { prefs ->
                            userMixGain = clampCaptureMixGain(
                                prefs.getFloat(CAPTURE_MIX_KEY, CAPTURE_MIX_DEFAULT)
                            )
                        }
                        val track = audioTrack
                        val pll = capturePll
                        val asrc = captureAsrc
                        if (track != null && pll != null && asrc != null) {
                            runCatching {
                                if (track.getTimestamp(audioTs)) {
                                    // Dominio 48 kHz: framePosition y capacidad
                                    // vienen en frames DE PISTA (192 kHz en
                                    // hi-res); el PLL exige el mismo dominio
                                    // que su tasa nominal (48 kHz).
                                    val rateRatio = SAMPLE_RATE.toLong()
                                    val trackRate = trackSampleRate.toLong().coerceAtLeast(1L)
                                    val consumed48k = audioTs.framePosition * rateRatio / trackRate
                                    val capacity48k = (track.bufferSizeInFrames.toLong() * rateRatio / trackRate).toInt()
                                    val ppm = pll.observe(
                                        submittedFrames = captureSubmittedFrames48k,
                                        consumedFrames = consumed48k,
                                        bufferCapacityFrames = capacity48k,
                                        timestampNs = audioTs.nanoTime,
                                        // v2.5.419: los underruns invalidan
                                        // la estima de reloj de ese intervalo
                                        // (inanición, no deriva).
                                        underruns = track.underrunCount.toLong()
                                    )
                                    asrc.setClockCorrectionPpm(ppm)
                                    // v2.5.415, bandera 1 de la 414: aviso
                                    // throttled (1/s) cuando el servo está EN
                                    // EL RIEL — la 414 lo descubrió por
                                    // accidente en la telemetría. La línea de
                                    // telemetría ya lleva pll_dem/pll_rail.
                                    if (pll.isAtRail) {
                                        val nowRail = android.os.SystemClock.elapsedRealtime()
                                        if (nowRail - lastPllRailWarnMs >= 1000L) {
                                            lastPllRailWarnMs = nowRail
                                            Log.w(TAG, "PLL captura EN EL RIEL: ppm=%+.1f demanda=%+.1f deriva_medida=%+.1f occ=%d/%d fase=%+.2f"
                                                .format(pll.correctionPpm, pll.demandedPpm, pll.measuredDriftPpm,
                                                    pll.occupancyFrames, pll.targetOccupancyFrames, pll.phaseErrorFraction))
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Tick de latencia adaptativa (throttled cada 8 bloques para evitar Binder IPC por bloque)
                    if ((blockCounter and 7) == 0) {
                        adaptiveLatency.tick(
                            track = audioTrack,
                            framesWrittenToTrack = framesWrittenToTrack,
                            dspLoadRatio = budgetGuard.dspLoadPercent / 100f,
                            // HI-RES: el margen predictivo viene en frames de DSP (48 kHz);
                            // escalarlo a frames de pista (x4 a 192 kHz, x1 a 48 kHz).
                            predictiveExtraMarginFrames = predictiveGovernor.suggestedHeadroomMarginFrames * trackSampleRate.toLong() / SAMPLE_RATE.toLong()
                        )
                        // Si el HAL se quedó sin datos, repone el colchón (una vez por cooldown, solo lo que falta).
                        val ur = adaptiveLatency.underrunCount
                        val newUr = ur - lastSeenUnderruns
                        lastSeenUnderruns = ur
                        if (newUr > 0 && cushionFramesTarget > 0) {
                            val nowR = android.os.SystemClock.elapsedRealtime()
                            val queuedR = (adaptiveLatency.measuredLatencyMs * trackSampleRate / 1000f).toLong()
                            val n = OutputCushionPolicy.reprimeFrames(newUr, queuedR, cushionFramesTarget, nowR - lastRepriming)
                            if (n > 0) {
                                lastRepriming = nowR
                                cushionReprimes++
                                val w = writeSilence(n)
                                // v2.5.415: el reprime ES audio entregado al
                                // track: se cuenta en el dominio 48 kHz del
                                // PLL (igual que el priming), o cada reprime
                                // falsea la cola del servo hacia vacío.
                                if (w > 0) {
                                    captureSubmittedFrames48k += w.toLong() * SAMPLE_RATE / trackSampleRate
                                }
                                Log.i(TAG, "Colchon repuesto tras %d underrun(s): +%d frames (cola=%d, total reposiciones=%d)"
                                    .format(newUr, w, queuedR, cushionReprimes))
                            }
                        }
                    }

                    runCatching { SpatialAudioEngineV2.feedCapturedBlock(buffer, effectiveFrames) }
                    runCatching { voiceProtection?.feed(buffer, effectiveFrames, SAMPLE_RATE) }
                    val monoFrames = minOf(effectiveFrames, mono.size)
                    for (i in 0 until monoFrames) mono[i] = (buffer[i * 2] + buffer[i * 2 + 1]) * 0.5f
                    runCatching { feedVoiceController(mono, monoFrames) }
                    // Solo los análisis PURAMENTE VISUALES se deciman, y solo si la vuelta completa del bucle se acerca
                    // al periodo del bloque (ver LoopLoadGovernor). Con carga normal corren en cada bloque, como antes.
                    if (loopLoad.runAnalysis(blockCounter)) {
                        runCatching { IvannaVisualizerBridge.processBlock(mono, monoFrames) }
                        runCatching { IvannaVisualizerBridgeV2.processBlockFromNPE(mono, monoFrames) }
                        runCatching { IvannaVisualizerBark64Bridge.processBlock(mono, monoFrames) }
                    }
                    
                    // FIX 3: Publish real capture levels for Route A
                    runCatching {
                        var sumSq = 0f
                        var peak = 0f
                        var clips = 0
                        for (i in 0 until samplesToWrite) {
                            val s = buffer[i]
                            sumSq += s * s
                            val absS = kotlin.math.abs(s)
                            if (absS > peak) peak = absS
                            if (absS >= 0.999f) clips++
                        }
                        val rms = kotlin.math.sqrt(sumSq / samplesToWrite.coerceAtLeast(1).toFloat())
                        
                        blockCounter++
                        if (blockCounter % 4 == 0) {
                            com.ivanna.omega.audio.OmegaMetrics.updateSharedLevels(
                                rms = rms,
                                peak = peak,
                                clips = clips,
                                dspActive = true
                            )
                        }
                    }
                    // FIX: IvannaLabMonitor.feed() nunca se llamaba.
                    // El analizador THD/IMD/LUFS/SNR declara feed() pero ningún
                    // caller lo invocaba — acumulaba 0 frames, measure() devolvía
                    // ceros para siempre. El buffer aquí es estéreo intercalado
                    // (exacto formato de nativeLabFeed), capturado por
                    // MediaProjection — fuente de datos real, no sintética.
                    runCatching { IvannaLabMonitor.feed(buffer, effectiveFrames) }

                    // Carga REAL de la vuelta completa (lectura -> ... -> medidores), no solo del DSP.
                    loopLoad.update(System.nanoTime() - iterStartNs, BLOCK_FRAMES * 1_000_000_000L / SAMPLE_RATE)
                    if (loopLoad.stride != lastLoopStride) {
                        lastLoopStride = loopLoad.stride
                        Log.i(TAG, "Carga de la vuelta del bucle %.0f %% -> analisis visuales cada %d bloque(s)"
                            .format(loopLoad.load * 100f, lastLoopStride))
                    }
                    // v2.5.374 — Aviso de sobrecarga: si la vuelta completa
                    // supera el periodo del bloque (carga > 100%), el hilo
                    // no lee a tiempo y AudioFlinger registra
                    // "RecordThread: buffer overflow". Se avisa al cruzar el
                    // umbral (no en cada bloque) para que la causa quede
                    // visible en el log de la app, no solo en el de
                    // AudioFlinger.
                    if (loopLoad.load > 1.0f && !loopOverloaded) {
                        loopOverloaded = true
                        Log.w(TAG, "SOBRECARGA del bucle de captura: carga %.0f %% > 100 %% — la lectura se retrasa y el RecordThread puede desbordarse (ver REC_BUFFER_BLOCKS y NPE decimado)"
                            .format(loopLoad.load * 100f))
                    } else if (loopLoad.load <= 0.95f) {
                        loopOverloaded = false
                    }
                }
            } catch (t: Throwable) {
                Log.e(TAG, "Excepción fatal en loop de audio: ${t.message}", t)
            } finally {
                // Siempre corre: loop normal (active=false→no-op) o loop muerto (active=true→reinicio)
                if (active) onError("Loop de proceso terminado inesperadamente")
            }
        }

        // Silencio inmutable (nunca se escribe en él): colchon, relleno durante el mute y reposición tras underrun.
        // HI-RES 192 kHz (v2.5.376): 19200 frames = 100 ms a 192 kHz (4800 a 48 kHz),
        // para cubrir el colchón máximo (MAX_HEADROOM_MS = 95 ms) a cualquier tasa de pista.
        private val silenceBuf = FloatArray(19200 * CHANNEL_COUNT)
        private var cushionFramesTarget = 0
        private var lastSeenUnderruns = 0
        private var lastRepriming = 0L
        private var cushionReprimes = 0
        private val loopLoad = LoopLoadGovernor()
        private var lastLoopStride = 1
        // v2.5.374 — true mientras la carga de la vuelta supera el 100%
        // (para avisar de la sobrecarga una sola vez al cruzar el umbral).
        private var loopOverloaded = false

        /** Escribe hasta [frames] de silencio SIN bloquear y los cuenta en la cola. Devuelve los frames aceptados. */
        private fun writeSilence(frames: Int): Int {
            val t = audioTrack ?: return 0
            val n = minOf(frames, silenceBuf.size / CHANNEL_COUNT)
            if (n <= 0) return 0
            val w = t.write(silenceBuf, 0, n * CHANNEL_COUNT, AudioTrack.WRITE_NON_BLOCKING)
            if (w <= 0) return 0
            framesWrittenToTrack += w / CHANNEL_COUNT
            return w / CHANNEL_COUNT
        }

        private fun writeAllToTrack(data: FloatArray, totalSamples: Int) {
            val track = audioTrack ?: return
            // HI-RES 192 kHz (v2.5.376): si hay resampleador, convertir el
            // delta ya procesado (48 kHz) a la tasa de pista ANTES de escribir.
            // El DSP sigue a 48 kHz; solo la salida sube de tasa.
            // process() devuelve un array nuevo por bloque (~10 KB a 192 kHz):
            // asignación aceptada por ahora (ver informe; sin variante sin
            // alocación en esta entrega). El silencio de mute TAMBIÉN pasa por
            // el resampleador (mantiene su estado continuo entre bloques).
            val resampler = hiresResampler
            val writeData: FloatArray
            val writeSamples: Int
            if (resampler != null && totalSamples > 0) {
                writeData = resampler.process(data.copyOf(totalSamples))
                writeSamples = writeData.size
            } else {
                writeData = data
                writeSamples = totalSamples
            }
            var written = 0
            while (written < writeSamples && active) {
                val result = track.write(writeData, written, writeSamples - written, AudioTrack.WRITE_BLOCKING)
                if (result < 0) { Log.e(TAG, "AudioTrack write error: $result"); break }
                written += result
            }
            // MISION HAAS (2026-09-18): contar los frames REALMENTE escritos.
            // HI-RES: son frames DE PISTA (a trackSampleRate), no de DSP.
            framesWrittenToTrack += written / CHANNEL_COUNT
            tickLatencyProbe()
            // Telemetria REAL a la UI: latencia instantanea de cola +
            // pico sostenido + resyncs acumulados.
            OmegaMetrics.updateSharedLevels(
                dspActive = true,
                hrtfActive = IvannaSpatialEngine.enabled
            )
            publishHaasTelemetry()
        }

        // getTimestamp() cruza binder hacia AudioFlinger; llamarlo en CADA bloque
        // (~150/s a 320 frames) metia jitter en el hilo de audio. Cada 16 bloques
        // (~9 Hz, ~107 ms) sobra para medir cola y publicar telemetría refinada.
        private var telemetryTick = 0
        private var lastFourDLatencyMs = -1f
        private val reinjectTel = FloatArray(16)
        private fun publishHaasTelemetry() {
            if ((++telemetryTick and 15) != 0) return
            // ~cada 2 s: telemetria REAL del reinyector diferencial (medida, no constante).
            // ~cada 2 s: DONDE se va el tiempo del bloque (media por etapa) para poder atacar la que domina.
            if ((telemetryTick and 255) == 0) {
                val g = budgetGuard
                val st = AudioThreadBudgetGuard.BudgetStage.values()
                val per = BLOCK_FRAMES * 1_000_000f / SAMPLE_RATE                     // periodo del bloque en us
                Log.i(TAG, "Etapas (us, media): " + st.joinToString(" ") { "%s=%d".format(it.name.lowercase(), g.getStageAvgNs(it) / 1000L) } +
                    " | DSP %.0f %% de %.0f us | vuelta completa %.0f %%".format(g.dspLoadPercent, per, loopLoad.load * 100f))
            }
            if ((telemetryTick and 255) == 0 && DSPBridge.reinjectTelemetry(reinjectTel)) {
                val t = reinjectTel
                Log.i(TAG, "Reinject: L=%.1f ms dspLag=%.2f smp conf=%.2f coh[%.2f %.2f %.2f] g[%.2f %.2f %.2f] riesgoPeine=%.3f inmersion=%.2f techo=%.2f"
                    .format(t[0], t[1], t[2], t[3], t[4], t[5], t[9], t[10], t[11], t[12], t[13], t[14]))
            }
            try {
                OmegaMetrics.updateRefinedTelemetry(
                    latencyMs = adaptiveLatency.measuredLatencyMs,
                    peakLatencyMs = adaptiveLatency.peakLatencyMs,
                    jitterMs = adaptiveLatency.jitterMs,
                    underrunCount = adaptiveLatency.underrunCount,
                    dspLoadPercent = budgetGuard.dspLoadPercent,
                    bufferHealthPercent = adaptiveLatency.bufferHealthPercent,
                    activeCodec = adaptiveLatency.activeCodec,
                    audioRoute = adaptiveLatency.currentRoute.name,
                    budgetBypasses = budgetGuard.bypassEventsTotal,
                    antiPopEvents = antiPopEngine.smoothedEventsTotal,
                    resyncCount = masterTiming.resyncCount
                )
            } catch (_: Throwable) {}
            // Cadena 4D: la háptica anticipada resta la latencia REAL de salida (medida por
            // AdaptiveLatencyController), no una constante. Solo se re-envía si cambió >= 0.5 ms.
            val outLat = adaptiveLatency.measuredLatencyMs
            if (outLat.isFinite() && outLat > 0f &&
                kotlin.math.abs(outLat - lastFourDLatencyMs) >= 0.5f) {
                lastFourDLatencyMs = outLat
                runCatching { com.ivanna.omega.core.Ivanna4DNative.nativeFourDSetOutputLatencyMs(outLat) }
            }
        }
        private var peakQueueMs = 0f

        private fun feedVoiceController(mono: FloatArray, numFrames: Int) {
            val vc = voiceController ?: return
            for (i in 0 until numFrames) {
                voiceAcc += mono[i]; voiceCount++
                if (voiceCount >= VOICE_DECIMATION) {
                    if (voiceFill < voiceWindow.size) voiceWindow[voiceFill++] = voiceAcc / voiceCount
                    voiceAcc = 0f; voiceCount = 0
                }
            }
            if (voiceFill >= voiceWindow.size) {
                voiceFill = 0
                val handler = voiceHandler
                if (handler != null && voiceBusy.compareAndSet(false, true)) {
                    System.arraycopy(voiceWindow, 0, voiceSnapshot, 0, voiceWindow.size)
                    handler.post(voiceTask)
                }
            }
        }

        /** Corre en el hilo de fondo IvannaVoiceClassifier, NUNCA en el hilo de audio. */
        private fun runVoiceClassification() {
            try {
                val vc = voiceController ?: return
                val t0 = System.nanoTime()
                val (hint, scores) = vc.processAudioWithScores(voiceSnapshot)
                val us = (System.nanoTime() - t0) / 1000L
                if (scores.isValid) {
                    AudioPipeline.publishLiveClassification(
                        scores.speech, scores.music, scores.bass, us, "CRNN TFLite")
                }
                OmegaEngineBridge.pushYamnetScores(
                    speech = scores.speech, music = scores.music,
                    classId = 0, confidence = maxOf(scores.speech, scores.music)
                )
                if (hint != "none") vc.executeCommand(hint)
            } catch (t: Throwable) {
                Log.w(TAG, "voice classification: ${t.message}")
            } finally {
                voiceBusy.set(false)
            }
        }

        // Reset de la rampa: al (re)arrancar el motor, el procesado entra
        // desde 0 con rampa limpia hasta el nivel de fusión Haas.

        // ── Haas Phase Alignment (2026-09-17): medir latencia REAL, no ganancia ──
        // Cadena medida: read BLOCKING (1 bloque = 10.7 ms @48k) -> procesado
        // -> write al track. El delay residual que produce el eco es la cola
        // del track (frames escritos - frames reproducidos) + el bloque en
        // vuelo. getTimestamp() da la posicion real de reproduccion.
        private var framesWrittenToTrack = 0L

        // HI-RES 192 kHz (v2.5.376, HIRES_192K §4.1): tasa de la pista de
        // reinyección. 48000 = comportamiento actual (sin upsampling).
        // Cuando el ajuste hires_reinject_192k está ON y hay DAC USB que
        // anuncia >48 kHz, la pista se crea a esa tasa (máx. 192 kHz) y el
        // delta ya procesado (DSP a 48 kHz) se remuestrea 48k -> tasa de
        // pista con `hiresResampler`. El DSP sigue a 48 kHz; solo la
        // salida sube de tasa. `framesWrittenToTrack` SIEMPRE cuenta frames
        // DE PISTA (a `trackSampleRate`), no frames de DSP.
        private var trackSampleRate: Int = SAMPLE_RATE
        private var hiresResampler: StereoAudioResampler? = null
        // Frames de pista por bloque de captura (320 a 48k -> 1280 a 192k).
        // Se usa para el colchón y los buffers (todo en frames de PISTA).
        private var trackBlockFrames: Int = BLOCK_FRAMES

        private var lastLatencyLogNs     = 0L
        private var lastRouteCheckNs     = 0L
        private var btRouteActive        = false
        private val audioTs              = android.media.AudioTimestamp()

        private fun tickLatencyProbe() {
            // framesWrittenToTrack se incrementa en writeAllToTrack con los
            // frames REALMENTE escritos — aqui no se asume nada.
            // MICROCUT_GUARD (2026-09-18): detectar underrun real comparando
            // el avance del cabezal de reproducción contra los frames escritos.
            // Si el cabezal lleva >3 bloques de retraso respecto a lo escrito,
            // el HAL está drenando más lento de lo que alimentamos (BT con
            // buffer grande, o CPU saturada): bajar el ritmo un bloque para
            // dejar respirar la cola en vez de seguir empujando hasta el
            // desbordamiento y el consiguiente clic de resync.
            // BLUETOOTH MASTER PATH (2026-09-18): detección de ruta de salida
            // cacheada cada 500 ms (getDevices por bloque sería demasiado
            // caro en el hot path). Los sinks A2DP/BLE drenan con buffers HAL
            // estructuralmente más grandes que altavoz/USB: un umbral fijo de
            // 3 bloques interpretaba ese lag estructural como underrun y
            // disparaba pacing continuamente. En BT el guard tolera 6 bloques
            // y cede 2 ms; en rutas locales mantiene 3 bloques / 1 ms.
            val t = audioTrack  // ref local fija (el campo real; FIX build: 'track' no resolvia)
            // BLUETOOTH MASTER PATH: ruta leída de la PROPIA pista
            // (AudioTrack.routedDevice, API 23+) — más precisa que sondear
            // los dispositivos del sistema y sin necesidad de Context (el
            // intento anterior con getSystemService rompía el scope de la
            // clase interna: causa de los errores 'appCtx'/'it' del CI).
            val nowRoute = System.nanoTime()
            if (nowRoute - lastRouteCheckNs >= 500_000_000L) {
                lastRouteCheckNs = nowRoute
                btRouteActive = runCatching {
                    val d = t?.routedDevice
                    d != null && (
                        d.type == android.media.AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                        d.type == android.media.AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                        d.type == android.media.AudioDeviceInfo.TYPE_BLE_HEADSET)
                }.getOrDefault(false)
            }
            val now = System.nanoTime()
            // FIX REAL (tronidos/microcortes, 2026-09-21): existía AQUÍ un
            // segundo disparador de resync (pause/flush/play) con umbral de
            // 2 bloques (~13 ms a BLOCK_FRAMES=320) — extremadamente
            // agresivo: cualquier jitter normal de CPU (GC, scheduler, y
            // sobre todo un decodificador de vídeo compitiendo por núcleos)
            // supera 13 ms de forma rutinaria, disparando un pause/flush/
            // play (corte audible real: descarta la cola y reinicia el
            // track) con mucha más frecuencia de la necesaria. Además corría
            // EN PARALELO con el resync de publishHaasTelemetry() (umbral
            // 120 ms, mucho más razonable), con su propia variable de
            // cooldown (lastResyncNs vs lastResyncMs) — dos mecanismos
            // independientes, sin coordinarse, podían encadenar resyncs uno
            // detrás de otro. Se deja UN SOLO mecanismo de resync (el de
            // publishHaasTelemetry, 120 ms / 900 ms de cooldown) — el
            // pacing suave de arriba (Thread.sleep 1-2 ms, no destructivo)
            // sigue intacto sin cambios.
            val shouldLog   = now - lastLatencyLogNs   >= 2_000_000_000L
            if (!shouldLog) return
            lastLatencyLogNs = now
            val track = audioTrack ?: return
            try {
                if (track.getTimestamp(audioTs)) {
                    val queued  = framesWrittenToTrack - audioTs.framePosition
                    // HI-RES: la cola está en frames DE PISTA (192 kHz en hi-res).
                    val queueMs = queued * 1000.0 / trackSampleRate
                    val blockMs = BLOCK_FRAMES * 1000.0 / SAMPLE_RATE
                    Log.i(TAG, "HaasLatency: cola_salida=%.1f ms (bloque=%.1f ms)".format(queueMs, blockMs))
                }
            } catch (e: Throwable) { Log.w(TAG, "latency probe: ${e.message}") }
        }

        private fun resetMixRamp() {
            mixGain = 0f; mixGainTarget = if (LEGACY_HAAS_MIX) HAAS_SAFE_GAIN else DELTA_BLEND_SOLO
            // Historial/alineacion del reinyector: un arranque limpio no debe heredar el retardo de otra fuente.
            runCatching { DSPBridge.reinjectReset() }
        }

        companion object {
            private const val TAG = "CaptureEngine"
            // Paso de rampa por bloque: 1/48 ≈ 0.5 s para llegar al objetivo
            // (48 bloques × 512 frames / 48 kHz). Los const viven aquí — en el
            // cuerpo de una clase normal son ilegales en Kotlin.
            private const val MIX_GAIN_STEP = 1f / 48f
            // true = mezcla Haas antigua (seco 100% + procesado 40% retardado = peine). false = solo delta.
            // internal (no private): PlaybackCaptureService.onStartCommand lo lee
            // para decidir si pide audio-focus con duck (v2.5.374 — el duck solo
            // tiene sentido en el modelo Haas legacy; en el modelo delta baja
            // el volumen de la fuente y el delta no puede restaurarlo).
            internal const val LEGACY_HAAS_MIX = false
            private const val DELTA_BLEND_SOLO = 0.6f        // (1-g)*seco + g*procesado en el oyente
            private const val DELTA_BLEND_WITH_MODULE = 0.45f
            // Latencia asumida hasta que AdaptiveLatencyController tenga una medicion (cola de salida tipica).
            private const val DEFAULT_LISTENER_LATENCY_MS = 30f
            private const val REINJECT_CAP_LOW  = 0.25f
            private const val REINJECT_CAP_MID  = 0.70f
            private const val REINJECT_CAP_HIGH = 1.00f
            // Punto de fusión Haas validado empíricamente en dispositivo:
            // original 100% + procesado al 50% (−6 dB) — sin eco discreto.
            // Ajustes finos 2026-09-17: 0.5 (-6.0 dB) -> 0.425 (-7.4 dB) -> 0.40 (-8.0 dB).
            // Reduccion de amplitud del procesado: 15% exacta (0.5 x 0.85),
            // que es ~15% menos de eco percibido SIN tocar filtros ni etapas:
            // solo cambia la interaccion original/procesada, como pide la mision.
            //
            // Por que 0.40 y no menos (analisis de los puntos evaluados):
            //  - 100/100 (1.0): doble senal a nivel comparable -> comb filtering
            //    severo, eco al 100%. Descartado (ya validado en dispositivo).
            //  - 100/50 (0.5): punto de fusion Haas clasico — el procesado se
            //    integra, pero queda eco residual perceptible en transitorios.
            //  - 0.40 (-8.0 dB): sigue DENTRO de la ventana de precedencia
            //    Haas (< ~10 dB bajo el original fusiona; la localizacion la
            //    manda Tidal, el procesado aporta cuerpo/espacialidad), pero el
            //    residuo de eco cae ~15%. Por debajo de ~0.35 (-9 dB) el aporte
            //    espacial empieza a desaparecer -> presencia perdida. Descartado.
            //
            // Fase: la cancelacion por desfase es proporcional a la amplitud
            // relativa de la copia retrasada; al bajar la copia 15%, la
            // profundidad de los peines de cancelacion baja ~15% en TODAS las
            // frecuencias en conflicto a la vez — correccion global sin
            // ningun filtro selectivo nuevo (determinista, sin degradar DSP).
            //
            // Latencia: no se toca — reducir el delay captura->reproduccion
            // requiere cambios de buffer del sistema, fuera del alcance de la
            // mezcla; con la copia a -8.0 dB el delay residual queda por debajo
            // del umbral de fusion para la mayoria de contenidos.
            //
            // La rampa per-sample (mixGain 0 -> target) se mantiene intacta:
            // cero clicks, cero cambios bruscos.
            private const val HAAS_ECHO_REDUCTION = 0.80f  // PARÁMETRO ÚNICO de calibración Haas: 0.80 => ganancia 0.40 (-8.0 dB);
        // rango seguro 0.75..0.90 (-8.5..-6.9 dB). <0.75 la copia procesada pierde
        // cuerpo/espacialidad; >0.90 el eco residual reaparece en transitorios.
            private const val HAAS_SAFE_GAIN = 0.5f * HAAS_ECHO_REDUCTION  // = 0.40 (-8.0 dB)

            // Mismo UUID exacto que IvannaGlobalEffectManager.kt usa para
            // instanciar el efecto nativo omega_effect en las sesiones
            // propias de la app (effect_uuid_t compilado en omega_effect.cpp:
            // timeLow=0x4956414e "IVAN", timeMid=0x4e41 "NA", timeHi=0x4f4d
            // "OM", clockSeq=0x4547 "EG", node=0x415355505245 "ASUPRE").
        }
    }
}
