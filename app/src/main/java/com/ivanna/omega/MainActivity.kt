package com.ivanna.omega

import com.ivanna.omega.audio.IvannaAudioProfile
import com.ivanna.omega.audio.IvannaAudioEngineParams
import com.ivanna.omega.audio.IvannaAntiDolbyParams
import com.ivanna.omega.audio.IvannaNeuromorphicParams
import com.ivanna.omega.audio.IvannaRouteParams

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.ivanna.omega.ai.LearningBias
import com.ivanna.omega.ai.RealtimeLearningController
import com.ivanna.omega.audio.AudioEngine
import com.ivanna.omega.audio.AudioForegroundService
import com.ivanna.omega.audio.IvannaBridgePlayer
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.NoRootAudioProcessor
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.audio.PlaybackCaptureService
import com.ivanna.omega.audio.SpatialAudioEngineV2
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.OmegaEngine
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.core.UserProfileManager
import com.ivanna.omega.audio.ProfileManager
import com.ivanna.omega.dsp.ConcertMode
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.audio.AppMetadataListener
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.ui.BridgePlayerCard
import com.ivanna.omega.ui.IvannaControlPanel
import com.ivanna.omega.ui.ProfileSelectorScreen
import com.ivanna.omega.ui.MagiskStatusPanel
import com.ivanna.omega.ui.theme.IvannaTheme
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import com.ivanna.omega.visualizer.VisualizerSurface
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * MainActivity v3.0 — UI Compose Material3 · OMNIPOTENTE
 *
 * FIXES DE CONECTIVIDAD (v3.0):
 *   Previamente MainActivity sólo cableaba 4 de los ~25 parámetros que
 *   IvannaControlPanel v3.0 expone (Exciter/EQ/Width/AntiDolby). Todo lo
 *   demás (Compresor, NHO/Espacial, Kernel Evolutivo, Motor NPE completo,
 *   Motor Binaural, Auto IA, Visualizador) llegaba a MainActivity vía
 *   callback pero cada onXxxChange = {} quedaba vacío: la UI respondía
 *   visualmente pero no tocaba ningún motor real.
 *
 *   1. Se cablean los 25 parámetros a sus contrapartes reales:
 *        IvannaNativeLib (DSP/Compresor/NHO/Espacial/Evolutivo/OmegaMode)
 *        IvannaNpeEngine (motor neuromórfico completo)
 *        SpatialAudioEngineV2 (motor binaural 32 objetos)
 *   2. Se inicializa IvannaNpeEngine (handle nativo) — antes nunca se
 *      llamaba a .init(), así que processInterleavedStereo() no hacía nada.
 *   3. Se cablea el flujo real de MediaProjection: permiso → arranque de
 *      PlaybackCaptureService → apertura del VisualizerSurface. Antes
 *      onOpenVisualizer era un {} vacío: el botón no hacía nada.
 *   4. ParameterStore v3.0 restaura y persiste los ~24 parámetros nuevos.
 *   5. Auto IA (autoMode): coroutine liviana que, si está activo, consulta
 *      IvannaNpeEngine.getDetectedGenre() cada pocos segundos y aplica el
 *      preset (IvannaEffectProfile) más afín vía GlobalEffectManager.
 */
class MainActivity : ComponentActivity() {
    companion object {
        private const val TAG = "MainActivity"
        private const val NPE_BLOCK_FRAMES = 2048
    }

    private lateinit var audioEngine: AudioEngine
    private lateinit var dspState: DSPState
    private lateinit var parameterStore: ParameterStore
    private lateinit var audioProfileManager: ProfileManager
    private var noRootProcessor: NoRootAudioProcessor? = null
    private val spatialEngineV2 = SpatialAudioEngineV2()
    private lateinit var profileManager: UserProfileManager
    private val concertMode = ConcertMode()
    private val metadataListener = AppMetadataListener(this)

    // FASE 1: reproductor propio (decodifica archivo -> DSPBridge -> AudioTrack).
    // Vive en MainActivity scope, se libera en onDestroy().
    private lateinit var bridgePlayer: IvannaBridgePlayer
    private val bridgePlayerState = mutableStateOf(IvannaBridgePlayer.State.IDLE)
    private val bridgePlayerUri  = mutableStateOf<Uri?>(null)

    // Picker de archivo (ActivityResultContracts.OpenDocument, filtrado a audio/*).
    private val openAudioLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            try {
                contentResolver.takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (_: Throwable) { /* algunas fuentes no admiten persist */ }
            bridgePlayerUri.value = uri
            Log.i(TAG, "BridgePlayer: archivo seleccionado -> $uri")
        }
    }

    // FASE 2: capturador/aplicador de sesgo aprendido por (contexto, param).
    private lateinit var learningBias: LearningBias
    private lateinit var realtimeLearningController: RealtimeLearningController

    // Mapea género detectado por el motor NPE → preset más afín, para Auto IA.
    private val genreToPreset = mapOf(
        "rock" to "Rock 70s",
        "classic rock" to "Rock 70s",
        "electronic" to "Spatial",
        "edm" to "Spatial",
        "pop" to "Warm",
        "hip hop" to "Punch",
        "hip-hop" to "Punch",
        "rap" to "Punch",
        "classical" to "Flat",
        "acoustic" to "Flat"
    )

    // FIX (independencia del mic): RECORD_AUDIO sólo hace falta para
    // AudioPlaybackCaptureConfiguration (MediaProjection/PlaybackCaptureService).
    // Ya no dispara initAudioEngine() — el núcleo DSP arranca siempre en
    // onCreate(), sin esperar este permiso.
    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            Log.i(TAG, "RECORD_AUDIO concedido — arrancando captura de reproducción")
            requestMediaProjectionAtStartup()
        } else {
            Log.w(TAG, "RECORD_AUDIO denegado — captura de reproducción (MediaProjection) " +
                    "deshabilitada; el resto del motor (DSP/Espacial/NPE local/Evolutivo) sigue activo")
        }
    }

    // FIX: flujo real de MediaProjection — antes esto solo se disparaba al
    // abrir el visualizer, así que PlaybackCaptureService (y con él, NPE y
    // el motor espacial) nunca recibían audio real a menos que el usuario
    // abriera esa pantalla. Ahora se solicita al ARRANCAR la app y la
    // captura corre siempre en segundo plano; el visualizer sólo controla
    // si se MUESTRA la UI, no si la captura está activa.
    private var showVisualizer by mutableStateOf(false)
    private var captureServiceRunning by mutableStateOf(false)
    private var showAdaptive by mutableStateOf(false)
    private var showProfiles by mutableStateOf(false)
    private var showMagisk by mutableStateOf(false)

    // ── Adaptive Control Center (Fase de UI) ────────────────────────────────────
    // adaptiveTelemetry: espejo Compose-observable de nativeGetAdaptiveTelemetry(),
    // actualizado cada 500ms desde startAdaptiveTelemetryLoop() (ver abajo) — antes
    // esa función solo mandaba a Logcat, sin ningún consumidor de UI.
    private var adaptiveTelemetry by mutableStateOf(com.ivanna.omega.ui.AdaptiveTelemetrySnapshot())
    // Estados UI del Adaptive Control Center. Desde esta fase YA no son
    // cosméticos: modulan la fuerza del AdaptiveDecisionEngine real vía
    // IvannaNativeLib.nativeSetAdaptiveControls().
    private var adaptiveMode by mutableStateOf(com.ivanna.omega.ui.AdaptiveMode.NATURAL)
    private var adaptiveIntensity by mutableStateOf(50f)
    // Nota: el control "Spatial Control" del Adaptive Center reutiliza el
    // mismo spatialWidth/onSpatialWidthChange que YA existe en
    // IvannaControlPanel ("ANCHO ESPACIAL") — no se crea un segundo estado
    // paralelo para el mismo parámetro real (evita exactamente el tipo de
    // bug de doble-fuente-de-verdad que se corrigió varias veces en esta
    // sesión). Ver IvannaControlPanel.kt.
    private var voiceProtectionEnabled by mutableStateOf(true)
    
    // ── BACKEND ADAPTATIVO MAGISTRAL ───────────────────────────────────────
    // Estados para los parámetros adaptativos calculados automáticamente
    private var adaptiveParams by mutableStateOf<FloatArray>(FloatArray(12))
    private var audioCharacteristics by mutableStateOf<FloatArray>(FloatArray(8))
    private var adaptiveEngineReady by mutableStateOf(false)
    private var dspPushJob: Job? = null
    private var metadataHooksStarted = false

    private val mediaProjectionLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK && result.data != null) {
            Log.i(TAG, "MediaProjection concedido — arrancando PlaybackCaptureService (siempre activo)")
            val intent = Intent(this, PlaybackCaptureService::class.java).apply {
                putExtra("resultCode", result.resultCode)
                putExtra("data", result.data)
            }
            ContextCompat.startForegroundService(this, intent)
            captureServiceRunning = true
        } else {
            Log.w(TAG, "MediaProjection denegado — NPE/motor espacial/visualizer sin señal real")
        }
        if (pendingVisualizerOpen) {
            showVisualizer = true
            pendingVisualizerOpen = false
        }
    }

    // FIX: solicitar MediaProjection al arrancar la app, no al abrir el visualizer.
    private fun requestMediaProjectionAtStartup() {
        val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        mediaProjectionLauncher.launch(mgr.createScreenCaptureIntent())
    }

    // El botón de visualizer ya no dispara el permiso (la captura ya corre
    // desde el arranque); si por algún motivo aún no está activa (usuario
    // denegó al inicio), se reintenta aquí antes de mostrar la UI.
    private var pendingVisualizerOpen = false

    private fun requestVisualizer() {
        if (captureServiceRunning) {
            showVisualizer = true
        } else {
            pendingVisualizerOpen = true
            val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
            mediaProjectionLauncher.launch(mgr.createScreenCaptureIntent())
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        profileManager = UserProfileManager(applicationContext)
        

        // DEFENSIVE: Capturar cualquier crash no capturado
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            Log.e(TAG, "CRASH NO CAPTURADO en thread=${thread.name}", throwable)
            // Forzar spatial_enabled = false si crash ocurre
            try {
                parameterStore.setSpatialEnabled(false)
                parameterStore.setSpatialInitPending(false)
            } catch (e: Exception) {
                Log.e(TAG, "Error limpiando spatial en handler", e)
            }
            // Re-lanzar para que Android lo reporte
            throw throwable
        }

        parameterStore = ParameterStore(this)
        audioEngine = AudioEngine()

        audioProfileManager = ProfileManager(
            applicationContext,
            audioEngine
        )
        // FIX (recableo a DSP real — ver hallazgo de auditoría): audioEngine
        // (AudioEngine/audio_orchestrator.cpp) es un motor huérfano — su
        // nativeProcessAudio() nunca se invoca con audio real en ningún lado
        // del código; mover sus sliders no cambiaba el sonido de nadie. El
        // motor real es DSPBridge (el que IvannaBridgePlayer efectivamente
        // reproduce), alimentado por DSPState.pushToNative() — que además ya
        // espeja al daemon Magisk system-wide. dspState reemplaza a
        // audioEngine como destino real de Exciter/EQ/Width (ver callbacks
        // más abajo). No se borra ni se toca la clase AudioEngine.
        dspState = DSPState(
            drive = parameterStore.getExciter(),
            low = parameterStore.getEqGain(), mid = parameterStore.getEqGain(),
            high = parameterStore.getEqGain(), presence = parameterStore.getEqGain(),
            stereoWidth = parameterStore.getWidth(),
            // alpha/beta son los que Compressor::setParams() lee de verdad
            // (threshold_ = -24+alpha*24, ratio_ = 1+beta*19) — son los MISMOS
            // sliders 0..1 que compThresholdSliderToDb()/compRatioSliderToRatio()
            // convierten a dB/ratio para mostrar en la UI. Sin esto, el primer
            // pushToNative() de initCoreAudioEngine() resetea el compresor a
            // alpha=beta=0.5 (defaults de DSPState) hasta que el usuario
            // tocara un slider de compresor.
            alpha = parameterStore.getCompThreshold(),
            beta = parameterStore.getCompRatio(),
            compThreshold = compThresholdSliderToDb(parameterStore.getCompThreshold()),
            compRatio = compRatioSliderToRatio(parameterStore.getCompRatio())
        )
        bridgePlayer = IvannaBridgePlayer(applicationContext)
        learningBias = LearningBias(applicationContext).also { it.load() }
        realtimeLearningController = RealtimeLearningController(
            applicationContext,
            learningBias = learningBias
        )
        adaptiveMode = com.ivanna.omega.ui.AdaptiveMode.values()[parameterStore.getAdaptiveModeOrdinal().coerceIn(0, 3)]
        adaptiveIntensity = parameterStore.getAdaptiveIntensity()
        voiceProtectionEnabled = parameterStore.isVoiceProtectionEnabled()
        bridgePlayer.setVoiceProtectionEnabled(voiceProtectionEnabled)

        // FIX (independencia del mic — pedido explícito de GORE): antes TODO
        // el núcleo (AudioEngine/Compresor/EQ/Exciter/Widener/SpatialAudioEngineV2/
        // modo no-root) esperaba a que RECORD_AUDIO estuviera concedido, porque
        // initAudioEngine() cargaba de un tirón cosas que sí lo necesitan
        // (MediaProjection/PlaybackCaptureService) junto con cosas que NUNCA
        // lo necesitaron. SpatialAudioEngineV2 es un procesador de bloques puro
        // (sin AudioRecord/AudioTrack propios — ver su cabecera); sólo recibe
        // bloques de PlaybackCaptureService cuando ese permiso está disponible,
        // pero puede inicializarse y correr sin él. AudioEngine (nativeInit,
        // exciter/EQ/width) tampoco toca el mic en ningún punto. El ÚNICO
        // requisito real de RECORD_AUDIO es AudioPlaybackCaptureConfiguration
        // (restricción del propio Android, no de esta app), usado sólo por
        // PlaybackCaptureService. Ya no existe "spatial_init_pending": el
        // motor espacial ya no depende del mic, así que se limpia el flag.
        parameterStore.setSpatialInitPending(false)
        initCoreAudioEngine()
        ensureMetadataHooksStarted()

        // FIX (retroalimentación acústica): AudioForegroundService arranca
        // AudioPipeline, que es un loopback físico real -- AudioRecord(MIC/
        // UNPROCESSED) -> DSPBridge.process() -> AudioTrack a la bocina. El
        // micrófono capta acústicamente lo que la propia bocina reproduce y
        // lo vuelve a meter al DSP: retroalimentación audible (choques,
        // silbidos colándose). No se borra AudioForegroundService/
        // AudioPipeline (regla de oro: no borramos, solo mejoramos) -- queda
        // aquí documentado y desactivado por defecto. La ruta audible activa
        // ahora es IvannaGlobalEffectManager (ya cableado desde
        // IVANNAApplication + AudioSessionReceiver, sin mic, sin captura, sin
        // riesgo de eco: engancha AudioEffect directo en la sesión de la app
        // fuente). Si más adelante se implementa la ruta con root para
        // interceptar el audio real de las apps con la cadena DSP completa
        // (NHO/Evolutivo/PDEngine) antes de la bocina, este es el punto para
        // reactivar un pipeline equivalente -- pero alimentado por esa
        // intercepción, nunca por el mic.
        // val serviceIntent = Intent(this, AudioForegroundService::class.java)
        // ContextCompat.startForegroundService(this, serviceIntent)

        // RECORD_AUDIO se pide en paralelo, sólo para habilitar la captura de
        // reproducción (MediaProjection). Si se deniega, el núcleo de arriba
        // ya está corriendo — no se pierde nada más que esa fuente de señal
        // real para telemetría NPE/espacial vía PlaybackCaptureService.
        val hasPermission = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
        if (hasPermission) {
            requestMediaProjectionAtStartup()
        } else {
            requestPermissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
        }

        // FIX: motor NPE (NHO+LIF+BiquadEnvelopeBank+AutonomousBrain) — sin
        // este init() el handle nativo se queda en 0 y todos los setters de
        // IvannaNpeEngine son no-ops silenciosos.
        IvannaNpeEngine.init(96000, NPE_BLOCK_FRAMES)
        IvannaNpeEngine.setBypass(parameterStore.isNpeBypass())
        // TUNING v3.1: Optimización de motor neuromorphic
        IvannaNpeEngine.setNeuroParams(
            parameterStore.getNpeHarmonic(),       // TUNED: 0.2→0.5 (harmonics)
            parameterStore.getNpeLateralInhib(),   // TUNED: 0.2→0.45 (detail)
            parameterStore.getNpeOhcCompression(), // TUNED: 0.3→0.55 (naturality)
            parameterStore.getNpeMasterGain()      // TUNED: 0.0→2.0 dB
        )
        IvannaNpeEngine.setAGC(parameterStore.getNpeAgcTarget(), parameterStore.getNpeAgcRate())
        // TUNED: agcTarget -18→-16dB (less aggressive), agcRate 0.3→0.5 (faster response)
        IvannaNpeEngine.setEngineFlags(
            parameterStore.getNpeHrtf(),
            parameterStore.getNpeCochlear(),
            parameterStore.getNpeAdapt()
        )
        IvannaNpeEngine.isManifoldEnabled = parameterStore.isNpeManifoldEnabled()

        // FIX (crash #2): Ahora IvannaNativeLib expone isLoaded; se guarda con él
        // antes de llamar cualquier external fun desde onCreate(). Sin este guard,
        // si libivanna_omega.so falla al cargar la llamada lanzaba UnsatisfiedLinkError
        // y crasheaba la app (el try-catch del init solo evita el crash del init,
        // no el de las llamadas posteriores a external fun).
        if (IvannaNativeLib.isLoaded) {
            // FIX (colisión con dspState.pushToNative(), ver constructor de
            // dspState arriba): antes acá se llamaba nativeSetCompressorParams
            // directo con dB/ratio reales, escribiendo el mismo g_comp que
            // dspState.pushToNative() (ya ejecutado en initCoreAudioEngine())
            // sobreescribe vía alpha/beta en cada cambio de Exciter/EQ/Width/
            // Comp. Dos rutas al mismo objeto compartido. dspState ya se
            // construyó con alpha/beta = los sliders persistidos, así que el
            // compresor ya quedó bien seteado — no se repite acá.
            IvannaNativeLib.nativeSetHarmonicGain(parameterStore.getNhoHarmonic())
            IvannaNativeLib.nativeSetSpatialAngleRad(parameterStore.getSpatialAngle())
            IvannaNativeLib.nativeSetSpatialWidthDirect(parameterStore.getSpatialWidth())
            applyAdaptiveUiControls()
            if (parameterStore.isEvoEnabled()) {
                IvannaNativeLib.nativeStartEvoThread()
            } else {
                IvannaNativeLib.nativeStopEvoThread()
            }
        } else {
            Log.w(TAG, "libivanna_omega.so no disponible — parámetros DSP nativos omitidos")
        }
        OmegaEngine.setMode(parameterStore.getOmegaMode().coerceIn(0, 2)) // ya tiene guard interno
        // spatialEngineV2.start() ya se ejecuta dentro de initCoreAudioEngine(),
        // Iniciar escucha de apps (Spotify/YouTube)
        metadataListener.startListening()
        // Aplicar perfil inteligente
        profileManager.applySmartProfile(application as IVANNAApplication)
        // sin esperar RECORD_AUDIO (ver comentario ahí).

        setContent {
            // FIX (branding): MaterialTheme genérico → IvannaTheme.
            // MaterialTheme usaba el esquema de colores y tipografía de Material3
            // por defecto, así que Surface.background era el gris/blanco de M3
            // (no ObsidianVoid) y MaterialTheme.typography.* resolvía a la
            // fuente estándar (no IvannaTypography con Mono/Sans y letterSpacing
            // calibrados). El branding solo sobrevivía en el logo porque los
            // colores Aurora/Obsidian se usan hardcodeados en los composables,
            // pero toda la tipografía y el fondo del Surface quedaban sin tema.
            IvannaTheme {
                // ──── INICIALIZAR ADAPTIVE ENGINE (Motor B — sensor) ─────────
                //
                // ARQUITECTURA MAESTRO-SENSOR (fix del bug de doble motor):
                //
                //   - Motor A = AdaptiveDecisionEngine (experimental/adaptive_engine/):
                //     hilo de control real, análisis genuino sobre el audio del
                //     bloque (RMS/peak/bandas/GR real vía bus lock-free), y su
                //     AdaptiveState se aplica al DSP dentro de nativeProcess.
                //     Es lo que alimenta al dashboard (nativeGetAdaptiveTelemetry
                //     + nativeGetBandEnergies). Es EL MAESTRO. No se toca.
                //
                //   - Motor B = AdaptiveEngineCore (adaptive_engine_core.hpp):
                //     estructura correcta, computeAdaptiveParameters() sólo se
                //     ejecuta cuando alguien invoca nativeAnalyzeAudio(buffer)
                //     — cosa que hoy NO ocurre en ningún caller Kotlin (grep
                //     confirmado). Sin analyze() previo, get() devuelve los
                //     defaults del struct (threshold=-20dB, ratio=2.0, exciter=
                //     0.5, width=1.0, gain=1.0), constantes.
                //
                // Bug previo: este loop llamaba nativeSetCompressorParams /
                // nativeSetHarmonicGain / nativeSetSpatialWidthDirect cada 100ms
                // con esos defaults de Motor B, sobreescribiendo lo que Motor A
                // ya decidía en base a la señal REAL. Colisión silenciosa (Motor
                // B pisa a Motor A cada 100 ms; el dashboard sigue mostrando A
                // porque nadie muestra los valores de B). Diagnóstico auditado
                // sobre el commit feede3c.
                //
                // Fix (esta rama): Motor B se conserva íntegro, se linkea bien
                // — bug de símbolos JNI corregido en ivanna_adaptive_jni.cpp,
                // los 5 Java_com_ivanna_omega_* pasaron a Java_com_ivanna_omega
                // _core_* para coincidir con com.ivanna.omega.core.IvannaNativeLib
                // —, pero queda como SENSOR: se llama Create + Get para tener
                // telemetría auditable de sus valores (adaptiveParams /
                // audioCharacteristics siguen exponiéndose a la UI), pero NO se
                // aplica al DSP. Motor A queda como único maestro sobre g_comp
                // /g_exciter/widener.
                //
                // Si mañana se decide fusionar B con A: (a) conectar
                // nativeAnalyzeAudio(bloque) desde IvannaBridgePlayer, y (b)
                // fusionar B.get() con A.evaluate() en un único punto de
                // decisión — jamás con dos loops escribiendo al mismo estado.
                // Regla de oro: no se borra el bloque de nativeSetXxx, se deja
                // documentado abajo entre marcadores para reactivación futura
                // supervisada.
                LaunchedEffect(Unit) {
                    try {
                        IvannaNativeLib.nativeCreateAdaptiveEngine()
                        adaptiveEngineReady = true
                        Log.i(TAG, "✨ Adaptive Engine (Motor B / sensor) inicializado — no aplica al DSP")
                    } catch (t: Throwable) {
                        // Antes: catch (e: Exception). UnsatisfiedLinkError es
                        // Error, no Exception — se escapaba silenciosamente.
                        Log.e(TAG, "Error iniciando Adaptive Engine (Motor B)", t)
                    }
                    
                    // Loop de LECTURA (100ms = 10 Hz) — sólo telemetría/sensor,
                    // no aplica nada al DSP. Motor A sigue mandando.
                    while (adaptiveEngineReady) {
                        try {
                            adaptiveParams = IvannaNativeLib.nativeGetAdaptiveParameters()
                            audioCharacteristics = IvannaNativeLib.nativeGetAudioCharacteristics()
                            
                            // === BLOQUE DE APLICACIÓN AL DSP — DESHABILITADO ===
                            // No se borra (regla de oro). Reactivar SÓLO junto
                            // con la conexión de nativeAnalyzeAudio(buffer) y la
                            // fusión con AdaptiveDecisionEngine — ver comentario
                            // maestro-sensor arriba. Mientras nativeAnalyzeAudio
                            // no se invoque, adaptiveParams son defaults
                            // constantes y aplicarlos rompe a Motor A.
                            //
                            // if (adaptiveParams.isNotEmpty()) {
                            //     val threshold = adaptiveParams[0]
                            //     val ratio = adaptiveParams[1]
                            //     val exciterAmount = adaptiveParams[2]
                            //     val stereoWidth = adaptiveParams[3]
                            //     val eqBass = adaptiveParams[4]
                            //     val eqMid = adaptiveParams[5]
                            //     val eqTreble = adaptiveParams[6]
                            //     val masterGain = adaptiveParams[7]
                            //     IvannaNativeLib.nativeSetCompressorParams(
                            //         threshold, ratio, 0.005f * 1000f, 0.1f * 1000f
                            //     )
                            //     IvannaNativeLib.nativeSetHarmonicGain(exciterAmount)
                            //     IvannaNativeLib.nativeSetSpatialWidthDirect(stereoWidth)
                            //     Log.d(TAG, "🎵 Adaptive: Threshold=$threshold, Ratio=$ratio, " +
                            //         "Exciter=$exciterAmount, Stereo=$stereoWidth, Gain=$masterGain")
                            // }
                            // === FIN BLOQUE DESHABILITADO ===
                            
                            kotlinx.coroutines.delay(100)
                        } catch (t: Throwable) {
                            // Idem catch de arriba: Throwable, no Exception.
                            Log.e(TAG, "Error en adaptive loop (sensor)", t)
                            break
                        }
                    }
                }
                
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background // ahora resuelve a ObsidianVoid
                ) {
                    if (showAdaptive) {
                        // Pantalla de telemetría real del AdaptiveDecisionEngine.
                        // Poll a 500 ms — suficiente para visualizar las decisiones
                        // del ADE (que corre a 20 Hz internamente).
                        var adaptiveBandEnergies by remember { mutableStateOf<FloatArray?>(null) }
                        var adaptiveTelemetryRaw by remember { mutableStateOf<FloatArray?>(null) }
                        LaunchedEffect(Unit) {
                            while (true) {
                                // AUDIT FIX (crítico, "backend real" falso):
                                // antes running = rawArray != null — un array
                                // no-nulo pero con menos de 10 elementos (dato
                                // inválido/incompleto) igual reportaba
                                // running=true. fromArray() cae a sus valores
                                // por defecto cuando t.size < 10 (targetGain=1,
                                // spatialWidth=1, safetyMargin=1 → 100%;
                                // compressorAmount/exciterReduction/
                                // voiceProtectionAmount=0 → 0%) — exactamente
                                // los números "sospechosamente redondos" que
                                // se veían en pantalla con el badge en ONLINE
                                // y "Blocks aplicados: 0", sin que la UI
                                // distinguiera "motor corriendo de verdad" de
                                // "datos reales activos". Ahora se exige
                                // el mismo criterio que ya usaba de forma
                                // correcta startAdaptiveTelemetryLoop() más
                                // abajo: nativeIsAdaptiveEngineRunning() como
                                // fuente de running, y solo se escribe la
                                // snapshot si el array es válido (size >= 10).
                                val rawArray = try {
                                    IvannaNativeLib.nativeGetAdaptiveTelemetry()
                                } catch (_: Throwable) { null }
                                val engineRunning = try {
                                    IvannaNativeLib.nativeIsAdaptiveEngineRunning()
                                } catch (_: Throwable) { false }
                                adaptiveTelemetryRaw = rawArray
                                if (rawArray != null && rawArray.size >= 10) {
                                    adaptiveTelemetry = com.ivanna.omega.ui.AdaptiveTelemetrySnapshot.fromArray(
                                        rawArray,
                                        running = engineRunning
                                    )
                                } else {
                                    // Dato inválido/insuficiente: reflejar
                                    // "no corriendo" en vez de fabricar una
                                    // snapshot con valores por defecto.
                                    adaptiveTelemetry = com.ivanna.omega.ui.AdaptiveTelemetrySnapshot.fromArray(
                                        null,
                                        running = false
                                    )
                                }
                                adaptiveBandEnergies = try {
                                    IvannaNativeLib.nativeGetBandEnergies()
                                } catch (_: Throwable) { null }
                                kotlinx.coroutines.delay(500)
                            }
                        }
                        Box(modifier = Modifier.fillMaxSize()) {
                            com.ivanna.omega.ui.AdaptiveDashboard(
                                telemetry    = adaptiveTelemetryRaw,
                                bandEnergies = adaptiveBandEnergies,
                                modifier     = Modifier.fillMaxSize()
                            )
                            IconButtonClose { showAdaptive = false }
                        }
                    } else if (showProfiles) {
                          ProfileSelectorScreen(
                              profiles = audioProfileManager.getAllProfiles().map { profile ->
    IvannaAudioProfile(
        id = profile.id,
        name = profile.name,
        description = profile.description,
        category = profile.category,
        priority = profile.priority,
        audioEngine = IvannaAudioEngineParams(
            gain = profile.audioEngine.gain,
            exciterAmount = profile.audioEngine.exciterAmount,
            eqGain = profile.audioEngine.eqGain,
            widthAmount = profile.audioEngine.widthAmount,
            bypass = profile.audioEngine.bypass
        ),
        antiDolby = IvannaAntiDolbyParams(
            speechThreshold = profile.antiDolby.speechThreshold,
            bassThreshold = profile.antiDolby.bassThreshold,
            eqBoost2k4k = profile.antiDolby.eqBoost2k4k,
            exciterLowOnly = profile.antiDolby.exciterLowOnly,
            widenerMultiplier = profile.antiDolby.widenerMultiplier
        ),
        neuromorphic = IvannaNeuromorphicParams(
            harmonicGain = profile.neuromorphic.harmonicGain,
            lateralInhibition = profile.neuromorphic.lateralInhibition,
            ohcCompression = profile.neuromorphic.ohcCompression,
            masterGainDb = profile.neuromorphic.masterGainDb,
            cochlearBandwidth = profile.neuromorphic.cochlearBandwidth
        ),
        route = IvannaRouteParams(
            bassBoostDb = profile.route.bassBoostDb,
            dialogBoostDb = profile.route.dialogBoostDb,
            widenerMult = profile.route.widenerMult
        ),
        tags = profile.tags,
        recommendedFor = profile.recommendedFor
    )
},
                              metadata = null,
                              currentId = parameterStore.getCurrentPreset(),
                              onApply = { profile ->
                                    audioProfileManager.applyProfile(profile.id)
                                  showProfiles = false
                              },
                              onClose = { showProfiles = false },
                              modifier = Modifier.fillMaxSize()
                          )
                      } else if (showMagisk) {
                          MagiskStatusPanel(
                              omegaBridge = IVANNAApplication.omegaBridge,
                              modifier = Modifier.fillMaxSize()
                          )
                      } else if (showVisualizer) {
                        Box(modifier = Modifier.fillMaxSize()) {
                            VisualizerSurface(modifier = Modifier.fillMaxSize())
                            IconButtonClose { showVisualizer = false }
                        }
                    } else {
                      var omegaMetrics by remember { mutableStateOf(OmegaMetrics()) }
                      LaunchedEffect(Unit) {
                          var lastThreadNs = android.os.Debug.threadCpuTimeNanos()
                          var lastWallNs = System.nanoTime()
                          while (true) {
                              val nowThreadNs = android.os.Debug.threadCpuTimeNanos()
                              val nowWallNs = System.nanoTime()
                              val cpu = if (nowWallNs > lastWallNs) {
                                  ((nowThreadNs - lastThreadNs).toFloat() / (nowWallNs - lastWallNs).toFloat() * 100f).coerceIn(0f, 100f)
                              } else 0f
                              lastThreadNs = nowThreadNs
                              lastWallNs = nowWallNs

                              val npeMetrics = IvannaNpeEngine.getMetrics()
                              val classify = IvannaNpeEngine.getSynthClassify()
                              omegaMetrics = OmegaMetrics(
                                  rmsLevel = npeMetrics.getOrElse(1) { 0f },
                                  peakLevel = npeMetrics.getOrElse(0) { 0f },
                                  clipCount = if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeGetClipCount() else omegaMetrics.clipCount,
                                  cpuPercent = cpu,
                                  latencyMs = 2.8f,
                                  sampleRate = 96000,
                                  yamnetCategory = IvannaNpeEngine.getDetectedGenre(),
                                  yamnetConfidence = classify.getOrElse(1) { 0f },
                                  dspActive = IvannaNativeLib.isLoaded && !dspState.bypass,
                                  hrtfActive = parameterStore.getNpeHrtf() || parameterStore.getOmegaMode() == 3,
                                  spatialWidth = parameterStore.getSpatialWidth()
                              )
                              delay(500L)
                          }
                      }
                      // FIX CRASH (df77763): El Column externo tenía .verticalScroll() que
                      // pasaba constraints de altura INFINITA a IvannaControlPanel, que a su
                      // vez tiene .fillMaxSize().verticalScroll() en su raíz.
                      // fillMaxSize() con altura infinita → IllegalArgumentException en Compose
                      // → app muerta al iniciar.
                      // Solución: Column sin verticalScroll. BridgePlayerCard es wrapContentHeight,
                      // IvannaControlPanel gestiona su propio scroll interno con fillMaxSize().
                      Column(
                        modifier = Modifier.fillMaxSize()
                      ) {
                        BridgePlayerCard(
                            playerState = bridgePlayerState.value,
                            currentUri  = bridgePlayerUri.value,
                            onPickFile  = { openAudioLauncher.launch(arrayOf("audio/*")) },
                            onPlay      = {
                                bridgePlayerUri.value?.let { uri ->
                                    bridgePlayer.play(uri)
                                    bridgePlayerState.value = IvannaBridgePlayer.State.PLAYING
                                    // Poll estado real (el player lo actualiza en su hilo).
                                    lifecycleScope.launch {
                                        while (bridgePlayer.state != IvannaBridgePlayer.State.STOPPED &&
                                               bridgePlayer.state != IvannaBridgePlayer.State.ERROR) {
                                            bridgePlayerState.value = bridgePlayer.state
                                            delay(500)
                                        }
                                        bridgePlayerState.value = bridgePlayer.state
                                    }
                                }
                            },
                            onPause  = { bridgePlayer.pause();  bridgePlayerState.value = bridgePlayer.state },
                            onResume = { bridgePlayer.resume(); bridgePlayerState.value = bridgePlayer.state },
                            onStop   = { bridgePlayer.stop();   bridgePlayerState.value = bridgePlayer.state }
                        )
                        IvannaControlPanel(
                            initialExciter = parameterStore.getExciter(),
                            initialEq = parameterStore.getEqGain(),
                            initialWidth = parameterStore.getWidth(),
                            initialAntiDolby = parameterStore.isAntiDolbyEnabled(),
                            initialPreset = parameterStore.getCurrentPreset(),
                            initialAutoMode = parameterStore.isAutoModeEnabled(),
                            initialOmegaMode = parameterStore.getOmegaMode(),
                            initialCompThreshold = parameterStore.getCompThreshold(),
                            initialCompRatio = parameterStore.getCompRatio(),
                            initialNhoHarmonic = parameterStore.getNhoHarmonic(),
                            initialSpatialAngle = parameterStore.getSpatialAngle(),
                            initialSpatialWidth = parameterStore.getSpatialWidth(),
                            initialEvoEnabled = parameterStore.isEvoEnabled(),
                            initialNpeBypass = parameterStore.isNpeBypass(),
                            initialNpeHarmonic = parameterStore.getNpeHarmonic(),
                            initialNpeLateralInhib = parameterStore.getNpeLateralInhib(),
                            initialNpeOhcCompression = parameterStore.getNpeOhcCompression(),
                            initialNpeMasterGain = parameterStore.getNpeMasterGain(),
                            initialNpeAgcTarget = parameterStore.getNpeAgcTarget(),
                            initialNpeAgcRate = parameterStore.getNpeAgcRate(),
                            initialNpeHrtf = parameterStore.getNpeHrtf(),
                            initialNpeCochlear = parameterStore.getNpeCochlear(),
                            initialNpeAdapt = parameterStore.getNpeAdapt(),
                            initialNpeManifold = parameterStore.isNpeManifoldEnabled(),
                            // CRITICAL: Si spatial_init_pending=true, mostrar false en UI (motor no iniciado aún)
                            initialSpatialEnabled = parameterStore.isSpatialEnabled(),

                            onExciterChange = { value ->
                                captureCorrection("exciter", value)
                                parameterStore.setExciter(value)
                                // FIX (recableo a DSP real): audioEngine
                                // (audio_orchestrator.cpp) es un motor huérfano
                                // — nunca procesa audio real, así que mover este
                                // slider no cambiaba el sonido de nadie. El motor
                                // real es DSPBridge, alimentado por
                                // DSPState.pushToNative() (que también espeja al
                                // daemon Magisk). EXCITER en la UI es 0..1 y
                                // mapea 1:1 a DSPState.drive (HarmonicExciter lee
                                // p.drive directamente).
                                updateDspState { it.copy(drive = value) }
                            },
                            onEqChange = { value ->
                                captureCorrection("eq_gain", value)
                                parameterStore.setEqGain(value)
                                // EQ GAIN es un único knob -18..18 dB; se aplica
                                // parejo a las 4 bandas (low/mid/high/presence)
                                // de ParametricEQ — no hay 4 sliders separados
                                // en esta UI, así que este es el control de tono
                                // global. Documentado acá porque no era obvio.
                                updateDspState {
                                    it.copy(low = value, mid = value, high = value, presence = value)
                                }
                            },
                            onWidthChange = { value ->
                                captureCorrection("width", value)
                                parameterStore.setWidth(value)
                                // STEREO WIDTH en la UI es 0..1.5 y mapea 1:1 a
                                // DSPState.stereoWidth (StereoWidener acepta
                                // 0..2, canal dedicado vía nativeSetStereoWidth,
                                // ya no colisiona con gamma/timing del compresor).
                                updateDspState { it.copy(stereoWidth = value) }
                            },
                            onAntiDolbyChange = { enabled ->
                                parameterStore.setAntiDolbyEnabled(enabled)
                                val app = application as? IVANNAApplication
                                if (enabled) {
                                    app?.globalEffectManager?.applyProfile(IvannaEffectProfile.SPATIAL)
                                } else {
                                    app?.globalEffectManager?.applyProfile(IvannaEffectProfile.FLAT)
                                }
                                Log.i(TAG, "Anti-Dolby: ${if (enabled) "ON" else "OFF"}")
                            },
                            onPresetSelected = { name ->
                                parameterStore.setCurrentPreset(name)
                                val profile = IvannaEffectProfile.byName[name]
                                if (profile != null) {
                                    (application as? IVANNAApplication)?.globalEffectManager?.applyProfile(profile)
                                    Log.i(TAG, "Preset aplicado: $name")
                                }
                            },
                            onAutoModeChange = { enabled ->
                                parameterStore.setAutoModeEnabled(enabled)
                                if (enabled) startAutoPresetLoop() else autoPresetJob?.cancel()
                            },
                            onOmegaModeChange = { mode ->
                                parameterStore.setOmegaMode(mode)
                                OmegaEngine.setMode(mode.coerceIn(0, 2))
                                if (mode == 3 && IvannaNativeLib.isLoaded) {
                                    IvannaNativeLib.nativeSetHRTFEnabled(true)
                                }
                            },
                            onCompThresholdChange = { slider ->
                                captureCorrection("comp_threshold", slider)
                                val ratioSlider = parameterStore.getCompRatio()
                                parameterStore.setCompParams(slider, ratioSlider)
                                // FIX (colisión real, hallazgo de auditoría):
                                // Compressor::setParams(DSPParams) — llamado por
                                // dspState.pushToNative() en CADA cambio de
                                // Exciter/EQ/Width — deriva threshold/ratio de
                                // p.alpha/p.beta (0..1), sobreescribiendo el
                                // mismo g_comp que nativeSetCompressorParams()
                                // tocaba con dB/ratio reales por una ruta
                                // separada. Dos rutas escribiendo el mismo
                                // objeto compartido = el compresor se reseteaba
                                // en silencio cada vez que se tocaba otro
                                // slider. Se consolida: alpha/beta SON los
                                // mismos sliders 0..1 que ya se usaban acá, así
                                // que se pasan directo a dspState y se elimina
                                // la llamada directa a nativeSetCompressorParams.
                                updateDspState { it.copy(alpha = slider, beta = ratioSlider) }
                            },
                            onCompRatioChange = { slider ->
                                captureCorrection("comp_ratio", slider)
                                val threshSlider = parameterStore.getCompThreshold()
                                parameterStore.setCompParams(threshSlider, slider)
                                updateDspState { it.copy(alpha = threshSlider, beta = slider) }
                            },
                            onNhoHarmonicChange = { value ->
                                captureCorrection("nho_harmonic", value)
                                parameterStore.setNhoHarmonic(value)
                                if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetHarmonicGain(value)
                            },
                            onSpatialAngleChange = { rad ->
                                captureCorrection("spatial_angle", rad)
                                parameterStore.setSpatialAngle(rad)
                                if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetSpatialAngleRad(rad)
                            },
                            onSpatialWidthChange = { width ->
                                captureCorrection("spatial_width", width)
                                parameterStore.setSpatialWidth(width)
                                if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetSpatialWidthDirect(width)
                            },
                            onEvoEnabledChange = { enabled ->
                                parameterStore.setEvoEnabled(enabled)
                                if (IvannaNativeLib.isLoaded) {
                                    if (enabled) IvannaNativeLib.nativeStartEvoThread()
                                    else IvannaNativeLib.nativeStopEvoThread()
                                }
                            },
                            onNpeBypassChange = { bypass ->
                                parameterStore.setNpeBypass(bypass)
                                IvannaNpeEngine.setBypass(bypass)
                            },
                            onNpeHarmonicChange = { value ->
                                parameterStore.setNpeNeuroParams(
                                    value, parameterStore.getNpeLateralInhib(),
                                    parameterStore.getNpeOhcCompression(), parameterStore.getNpeMasterGain()
                                )
                                pushNpeNeuroParams()
                            },
                            onNpeLateralInhibChange = { value ->
                                parameterStore.setNpeNeuroParams(
                                    parameterStore.getNpeHarmonic(), value,
                                    parameterStore.getNpeOhcCompression(), parameterStore.getNpeMasterGain()
                                )
                                pushNpeNeuroParams()
                            },
                            onNpeOhcCompressionChange = { value ->
                                parameterStore.setNpeNeuroParams(
                                    parameterStore.getNpeHarmonic(), parameterStore.getNpeLateralInhib(),
                                    value, parameterStore.getNpeMasterGain()
                                )
                                pushNpeNeuroParams()
                            },
                            onNpeMasterGainChange = { value ->
                                parameterStore.setNpeNeuroParams(
                                    parameterStore.getNpeHarmonic(), parameterStore.getNpeLateralInhib(),
                                    parameterStore.getNpeOhcCompression(), value
                                )
                                pushNpeNeuroParams()
                                // FIX (Fase A, gap del README): el slider "MASTER GAIN" (-18..18 dB)
                                // solo llegaba al motor NPE (ganancia de compensación interna
                                // post-neuromorphic), nunca a DSPState.master/GainStage — la
                                // ganancia de salida real de la cadena DSPBridge (que suena en
                                // IvannaBridgePlayer, en las AudioEffect sessions de terceros vía
                                // globalEffectManager, y en el daemon Magisk vía omegaBridge). El
                                // rango del slider coincide exactamente con lo que GainStage espera
                                // en p.master (dB directo, ver dbToLin(p.master) en GainStage.cpp) —
                                // no hace falta reescalar. No se quita el envío al NPE (sigue siendo
                                // válido para su propia compensación interna); se agrega el real.
                                updateDspState { it.copy(master = value) }
                            },
                            onNpeAgcChange = { target, rate ->
                                parameterStore.setNpeAgc(target, rate)
                                IvannaNpeEngine.setAGC(target, rate)
                            },
                            onNpeFlagsChange = { hrtf, cochlear, adapt ->
                                parameterStore.setNpeFlags(hrtf, cochlear, adapt)
                                IvannaNpeEngine.setEngineFlags(hrtf, cochlear, adapt)
                            },
                            onNpeManifoldChange = { enabled ->
                                parameterStore.setNpeManifoldEnabled(enabled)
                                IvannaNpeEngine.isManifoldEnabled = enabled
                            },
                            onSpatialEnabledChange = { enabled ->
                                try {
                                    parameterStore.setSpatialEnabled(enabled)
                                    if (enabled) {
                                        // Solo iniciar si permiso confirmado
                                        if (ContextCompat.checkSelfPermission(this@MainActivity, Manifest.permission.RECORD_AUDIO)
                                            == PackageManager.PERMISSION_GRANTED) {
                                            try {
                                                spatialEngineV2.start()
                                                // FIX (control sin efecto real — auditoría de
                                                // cableado): este toggle solo arrancaba
                                                // spatialEngineV2 (telemetría pura, no produce
                                                // audio — se deja porque alimenta el HUD de
                                                // "32 objetos" en la UI). El motor que sí
                                                // produce binaural real preservando el estéreo
                                                // (IvannaSpatialEngine, upmixer+VBAP+HRTF+
                                                // head-tracking) nunca se instanciaba en toda
                                                // la app. Se inicializa y activa acá también.
                                                com.ivanna.omega.spatial.IvannaSpatialEngine.shared.init()
                                                com.ivanna.omega.spatial.IvannaSpatialEngine.enabled = true
                                            } catch (e: Exception) {
                                                Log.e(TAG, "Error iniciando SpatialAudioEngineV2 desde UI", e)
                                                parameterStore.setSpatialEnabled(false)
                                            }
                                        } else {
                                            Log.w(TAG, "Permiso RECORD_AUDIO no confirmado — no iniciando SpatialAudioEngineV2")
                                            parameterStore.setSpatialEnabled(false)
                                        }
                                    } else {
                                        try {
                                            spatialEngineV2.stop()
                                            com.ivanna.omega.spatial.IvannaSpatialEngine.enabled = false
                                            com.ivanna.omega.spatial.IvannaSpatialEngine.shared.release()
                                        } catch (e: Exception) {
                                            Log.e(TAG, "Error deteniendo SpatialAudioEngineV2", e)
                                        }
                                    }
                                } catch (e: Exception) {
                                    Log.e(TAG, "Error en onSpatialEnabledChange", e)
                                    parameterStore.setSpatialEnabled(false)
                                }
                            },
                            onOpenVisualizer = { requestVisualizer() },
                              onOpenProfiles = { showProfiles = true },
                              onOpenMagisk = { showMagisk = true },
                            onOpenAdaptive = { showAdaptive = true },
                            metrics = omegaMetrics,
                            onMetricsUpdate = { omegaMetrics = it },
                            // ── Adaptive Control Center ──────────────────────
                            adaptiveTelemetry = adaptiveTelemetry,
                            adaptiveMode = adaptiveMode,
                            onAdaptiveModeChange = {
                                adaptiveMode = it
                                parameterStore.setAdaptiveModeOrdinal(it.ordinal)
                                applyAdaptiveUiControls()
                            },
                            adaptiveIntensity = adaptiveIntensity,
                            onAdaptiveIntensityChange = {
                                adaptiveIntensity = it
                                parameterStore.setAdaptiveIntensity(it)
                                applyAdaptiveUiControls()
                            },
                            voiceProtectionEnabled = voiceProtectionEnabled,
                            onVoiceProtectionChange = { enabled ->
                                voiceProtectionEnabled = enabled
                                parameterStore.setVoiceProtectionEnabled(enabled)
                                try {
                                    bridgePlayer.setVoiceProtectionEnabled(enabled)
                                } catch (e: Exception) {
                                    Log.e(TAG, "Error en onVoiceProtectionChange", e)
                                }
                            }
                        )
                      }
                    }
                }
            }
        }

        if (parameterStore.isAutoModeEnabled()) startAutoPresetLoop()
        startControlFrameLoop()
        startAdaptiveTelemetryLoop()
    }

    // FASE 4B: log de telemetría del ciclo adaptativo real. Corre fuera del
    // audio thread, throttle 500 ms (mínimo pedido por el prompt). El origen
    // es único: DSPBridge.nativeProcess() actualiza los atomics, y este loop
    // los saca por logcat con un tag dedicado. No usa omega_daemon / NPE /
    // HUD independientes — esos no representan la ruta audible real.
    private var adaptiveTelemetryJob: Job? = null
    private fun startAdaptiveTelemetryLoop() {
        adaptiveTelemetryJob?.cancel()
        adaptiveTelemetryJob = lifecycleScope.launch {
            while (true) {
                if (IvannaNativeLib.isLoaded) {
                    try {
                        val running = IvannaNativeLib.nativeIsAdaptiveEngineRunning()
                        val t = IvannaNativeLib.nativeGetAdaptiveTelemetry()
                        if (t != null && t.size >= 10) {
                            // FIX (UI sin consumidor de telemetría — Adaptive
                            // Control Center): antes esto solo mandaba a
                            // Logcat. adaptiveTelemetry es mutableStateOf,
                            // Compose recompone automáticamente donde se lea.
                            adaptiveTelemetry = com.ivanna.omega.ui.AdaptiveTelemetrySnapshot.fromArray(t, running)
                            Log.i("IVANNA.AdaptiveTelemetry",
                                "running=$running " +
                                "rms=%.4f peak=%.4f gr_db=%.2f target_gain=%.3f comp=%.3f ".format(
                                    t[0], t[1], t[2], t[3], t[4]) +
                                "exc_red=%.3f width=%.3f margin=%.3f voice_prot=%.3f applied=%.0f".format(
                                    t[5], t[6], t[7], t[8], t[9]))
                        } else {
                            adaptiveTelemetry = com.ivanna.omega.ui.AdaptiveTelemetrySnapshot(running = running)
                        }
                    } catch (t: Throwable) {
                        Log.w(TAG, "adaptiveTelemetryLoop tick fallo", t)
                    }
                }
                delay(500L)
            }
        }
    }

    // FASE 2: loop de control (fuera del audio thread) que empuja el sesgo
    // aprendido + genoma evolutivo + YAMNet al bus seqlock. 20 Hz basta:
    // el ControlFrame es paramétrico, no muestra a muestra.
    private var controlFrameJob: Job? = null
    private fun startControlFrameLoop() {
        controlFrameJob?.cancel()
        controlFrameJob = lifecycleScope.launch {
            while (true) {
                if (IvannaNativeLib.isLoaded) {
                    try {
                        val genre = IvannaNpeEngine.getDetectedGenre().lowercase()
                        val ctx = if (genre.isNotBlank() && genre != "unknown")
                            "genre:$genre" else "preset:${parameterStore.getCurrentPreset()}"
                        IvannaNativeLib.nativeSetLearningContext(ctx)
                        IvannaNativeLib.nativeApplyControlFrame()
                    } catch (t: Throwable) {
                        Log.w(TAG, "controlFrameLoop tick fallo", t)
                    }
                }
                delay(50L)
            }
        }
    }

    private fun updateDspState(transform: (DSPState) -> DSPState) {
        dspState = transform(dspState)
        dspPushJob?.cancel()
        dspPushJob = lifecycleScope.launch {
            delay(24L)
            dspState.pushToNative()
        }
    }

    private fun applyAdaptiveUiControls() {
        if (!IvannaNativeLib.isLoaded) return
        try {
            IvannaNativeLib.nativeSetAdaptiveControls(adaptiveMode.ordinal, adaptiveIntensity)
        } catch (t: Throwable) {
            Log.w(TAG, "applyAdaptiveUiControls fallo", t)
        }
    }

    private fun ensureMetadataHooksStarted() {
        if (metadataHooksStarted) return
        metadataListener.startListening()
        profileManager.applySmartProfile(application as IVANNAApplication)
        metadataHooksStarted = true
    }

    // FASE 2: captura la corrección del usuario sobre un parámetro. El valor
    // "autónomo" (Kernel Evo / Auto IA) se lee del ParameterStore justo antes
    // de aplicar el cambio nuevo; el sesgo se persiste y se acumula por
    // (género_detectado o preset_activo, param_key). El punto de aplicación
    // real del sesgo ocurre en el hilo de control C++ (control_apply_frame),
    // que consulta LearningBias.jniGetBiasForActiveContext(...).
    private fun captureCorrection(paramKey: String, userValue: Float) {
        try {
            val genre = IvannaNpeEngine.getDetectedGenre().lowercase()
            val preset = parameterStore.getCurrentPreset()
            val autonomous: Float? = readAutonomousValue(paramKey)
            val parameter = learningParameterForKey(paramKey)
            if (parameter != null && autonomous != null) {
                realtimeLearningController.forceAutonomousAnchor(parameter, autonomous)
                realtimeLearningController.captureUserCorrectionIfNeeded(parameter, userValue, genre, preset)
            } else {
                val ctx = if (genre.isNotBlank() && genre != "unknown") "genre:$genre" else "preset:$preset"
                learningBias.captureCorrection(ctx, paramKey, autonomous, userValue)
            }
        } catch (t: Throwable) {
            Log.w(TAG, "captureCorrection($paramKey) fallo silencioso", t)
        }
    }

    private fun learningParameterForKey(paramKey: String): RealtimeLearningController.Parameter? = when (paramKey) {
        "exciter"        -> RealtimeLearningController.Parameter.EXCITER
        "eq_gain"        -> RealtimeLearningController.Parameter.EQ
        "width"          -> RealtimeLearningController.Parameter.WIDTH
        "comp_threshold" -> RealtimeLearningController.Parameter.COMP_THRESHOLD
        "comp_ratio"     -> RealtimeLearningController.Parameter.COMP_RATIO
        "nho_harmonic"   -> RealtimeLearningController.Parameter.NHO_HARMONIC
        "spatial_angle"  -> RealtimeLearningController.Parameter.SPATIAL_ANGLE
        "spatial_width"  -> RealtimeLearningController.Parameter.SPATIAL_WIDTH
        else -> null
    }

    private fun readAutonomousValue(paramKey: String): Float? = when (paramKey) {
        "nho_harmonic"    -> parameterStore.getNhoHarmonic()
        "spatial_angle"   -> parameterStore.getSpatialAngle()
        "spatial_width"   -> parameterStore.getSpatialWidth()
        "exciter"         -> parameterStore.getExciter()
        "eq_gain"         -> parameterStore.getEqGain()
        "width"           -> parameterStore.getWidth()
        "comp_threshold"  -> parameterStore.getCompThreshold()
        "comp_ratio"      -> parameterStore.getCompRatio()
        else -> null
    }

    private var autoPresetJob: kotlinx.coroutines.Job? = null

    private fun startAutoPresetLoop() {
        autoPresetJob?.cancel()
        autoPresetJob = lifecycleScope.launch {
            while (true) {
                val genre = IvannaNpeEngine.getDetectedGenre().lowercase()
                val presetName = genreToPreset.entries.firstOrNull { genre.contains(it.key) }?.value
                if (presetName != null) {
                    val profile = IvannaEffectProfile.byName[presetName]
                    if (profile != null) {
                        (application as? IVANNAApplication)?.globalEffectManager?.applyProfile(profile)
                        Log.i(TAG, "Auto IA: género='$genre' → preset='$presetName'")
                    }
                }
                delay(6000)
            }
        }
    }

    private fun pushNpeNeuroParams() {
        IvannaNpeEngine.setNeuroParams(
            parameterStore.getNpeHarmonic(),
            parameterStore.getNpeLateralInhib(),
            parameterStore.getNpeOhcCompression(),
            parameterStore.getNpeMasterGain()
        )
    }

    // Conversión idéntica a la usada en IvannaControlPanel para mostrar el
    // valor legible ("%.1f dB" / "%.1f:1"): slider [0..1] → unidad real.
    private fun compThresholdSliderToDb(slider: Float): Float = -24f + slider * 24f
    private fun compRatioSliderToRatio(slider: Float): Float = 1f + slider * 19f

    private fun initCoreAudioEngine() {
        ensureMetadataHooksStarted()
        try {
            audioEngine.initialize(96000)
            audioEngine.setExciter(parameterStore.getExciter())
            audioEngine.setEqGain(parameterStore.getEqGain())
            audioEngine.setWidth(parameterStore.getWidth())

            // FIX (recableo a DSP real): dspState ya viene hidratado desde
            // ParameterStore en onCreate(); acá se empuja por primera vez al
            // motor nativo real (DSPBridge + daemon Magisk), que antes solo
            // recibía los valores por defecto hasta que alguien tocara un
            // slider.
            dspState.pushToNative()

            // FIX (independencia del mic): SpatialAudioEngineV2 es un procesador
            // de bloques puro (sin AudioRecord/AudioTrack propios — ver su
            // cabecera). No necesita RECORD_AUDIO para inicializarse; sólo deja
            // de recibir bloques reales si PlaybackCaptureService no está activo
            // (porque el usuario denegó el permiso), pero eso no es un crash ni
            // requiere esperar nada aquí.
            if (parameterStore.isSpatialEnabled()) {
                Log.i(TAG, "Iniciando SpatialAudioEngineV2 (no depende de RECORD_AUDIO)")
                try {
                    spatialEngineV2.start()
                } catch (e: Exception) {
                    Log.e(TAG, "Error iniciando SpatialAudioEngineV2 en initCoreAudioEngine", e)
                    parameterStore.setSpatialEnabled(false)
                }
            }

            // Modo no-root si no hay Magisk
            noRootProcessor = NoRootAudioProcessor(this)
            if (!noRootProcessor!!.hasMagisk()) {
                Log.i(TAG, "Iniciando modo no-root")
                noRootProcessor!!.start()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error crítico en initCoreAudioEngine", e)
            parameterStore.setSpatialEnabled(false)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        try { controlFrameJob?.cancel() } catch (e: Exception) { Log.e(TAG, "Error canceling controlFrameJob", e) }
        try { adaptiveTelemetryJob?.cancel() } catch (e: Exception) { Log.e(TAG, "Error canceling adaptiveTelemetryJob", e) }
        try { realtimeLearningController.release() } catch (e: Exception) { Log.e(TAG, "Error releasing realtimeLearningController", e) }
        try { bridgePlayer.stop() } catch (e: Exception) { Log.e(TAG, "Error stopping bridgePlayer", e) }
        try {
            autoPresetJob?.cancel()
        } catch (e: Exception) {
            Log.e(TAG, "Error canceling autoPresetJob", e)
        }
        try {
            noRootProcessor?.stop()
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping noRootProcessor", e)
        }
        try {
            audioEngine.release()
        } catch (e: Exception) {
            Log.e(TAG, "Error releasing audioEngine", e)
        }
        try {
            spatialEngineV2.stop()
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping spatialEngineV2", e)
        }
        try {
            IvannaVisualizerBridgeV2.release()
        } catch (e: Exception) {
            Log.e(TAG, "Error releasing visualizer", e)
        }
        try {
            IvannaNpeEngine.release()
        } catch (e: Exception) {
            Log.e(TAG, "Error releasing NPE engine", e)
        }
    }
}

@androidx.compose.runtime.Composable
private fun IconButtonClose(onClick: () -> Unit) {
    Box(modifier = Modifier.fillMaxSize().padding(16.dp)) {
        FilledTonalButton(
            onClick = onClick,
            modifier = Modifier.align(Alignment.TopEnd)
        ) { androidx.compose.material3.Text("Cerrar visualizador") }
    }
}
