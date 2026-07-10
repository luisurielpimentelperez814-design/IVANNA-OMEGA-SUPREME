package com.ivanna.omega

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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.ivanna.omega.ai.LearningBias
import com.ivanna.omega.audio.AudioEngine
import com.ivanna.omega.audio.AudioForegroundService
import com.ivanna.omega.audio.IvannaBridgePlayer
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.NoRootAudioProcessor
import com.ivanna.omega.audio.PlaybackCaptureService
import com.ivanna.omega.audio.SpatialAudioEngineV2
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.OmegaEngine
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.ui.BridgePlayerCard
import com.ivanna.omega.ui.IvannaControlPanel
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
        private const val NPE_BLOCK_FRAMES = PlaybackCaptureService.INPUT_SAMPLES
    }

    private lateinit var audioEngine: AudioEngine
    private lateinit var parameterStore: ParameterStore
    private var noRootProcessor: NoRootAudioProcessor? = null
    private val spatialEngineV2 = SpatialAudioEngineV2()

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
        bridgePlayer = IvannaBridgePlayer(applicationContext)
        learningBias = LearningBias(applicationContext).also { it.load() }

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

        // FIX: arrancar el servicio en primer plano para audio en background
        val serviceIntent = Intent(this, AudioForegroundService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)

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
        IvannaNpeEngine.init(48000, NPE_BLOCK_FRAMES)
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
            IvannaNativeLib.nativeSetCompressorParams(
                compThresholdSliderToDb(parameterStore.getCompThreshold()),
                compRatioSliderToRatio(parameterStore.getCompRatio())
            )
            IvannaNativeLib.nativeSetHarmonicGain(parameterStore.getNhoHarmonic())
            IvannaNativeLib.nativeSetSpatialAngleRad(parameterStore.getSpatialAngle())
            IvannaNativeLib.nativeSetSpatialWidthDirect(parameterStore.getSpatialWidth())
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
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background // ahora resuelve a ObsidianVoid
                ) {
                    if (showVisualizer) {
                        Box(modifier = Modifier.fillMaxSize()) {
                            VisualizerSurface(modifier = Modifier.fillMaxSize())
                            IconButtonClose { showVisualizer = false }
                        }
                    } else {
                      Column(
                        modifier = Modifier
                          .fillMaxSize()
                          .verticalScroll(rememberScrollState())
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
                                this@MainActivity.audioEngine.setExciter(value)
                            },
                            onEqChange = { value ->
                                captureCorrection("eq_gain", value)
                                parameterStore.setEqGain(value)
                                this@MainActivity.audioEngine.setEqGain(value)
                            },
                            onWidthChange = { value ->
                                captureCorrection("width", value)
                                parameterStore.setWidth(value)
                                this@MainActivity.audioEngine.setWidth(value)
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
                                IvannaNativeLib.nativeSetCompressorParams(
                                    compThresholdSliderToDb(slider),
                                    compRatioSliderToRatio(ratioSlider)
                                )
                            },
                            onCompRatioChange = { slider ->
                                captureCorrection("comp_ratio", slider)
                                val threshSlider = parameterStore.getCompThreshold()
                                parameterStore.setCompParams(threshSlider, slider)
                                IvannaNativeLib.nativeSetCompressorParams(
                                    compThresholdSliderToDb(threshSlider),
                                    compRatioSliderToRatio(slider)
                                )
                            },
                            onNhoHarmonicChange = { value ->
                                captureCorrection("nho_harmonic", value)
                                parameterStore.setNhoHarmonic(value)
                                IvannaNativeLib.nativeSetHarmonicGain(value)
                            },
                            onSpatialAngleChange = { rad ->
                                captureCorrection("spatial_angle", rad)
                                parameterStore.setSpatialAngle(rad)
                                IvannaNativeLib.nativeSetSpatialAngleRad(rad)
                            },
                            onSpatialWidthChange = { width ->
                                captureCorrection("spatial_width", width)
                                parameterStore.setSpatialWidth(width)
                                IvannaNativeLib.nativeSetSpatialWidthDirect(width)
                            },
                            onEvoEnabledChange = { enabled ->
                                parameterStore.setEvoEnabled(enabled)
                                if (enabled) IvannaNativeLib.nativeStartEvoThread()
                                else IvannaNativeLib.nativeStopEvoThread()
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
                                        } catch (e: Exception) {
                                            Log.e(TAG, "Error deteniendo SpatialAudioEngineV2", e)
                                        }
                                    }
                                } catch (e: Exception) {
                                    Log.e(TAG, "Error en onSpatialEnabledChange", e)
                                    parameterStore.setSpatialEnabled(false)
                                }
                            },
                            onOpenVisualizer = { requestVisualizer() }
                        )
                      }
                    }
                }
            }
        }

        if (parameterStore.isAutoModeEnabled()) startAutoPresetLoop()
        startControlFrameLoop()
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
            val ctx = if (genre.isNotBlank() && genre != "unknown") "genre:$genre" else "preset:$preset"
            val autonomous: Float? = readAutonomousValue(paramKey)
            learningBias.captureCorrection(ctx, paramKey, autonomous, userValue)
        } catch (t: Throwable) {
            Log.w(TAG, "captureCorrection($paramKey) fallo silencioso", t)
        }
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
        try {
            audioEngine.initialize(48000)
            audioEngine.setExciter(parameterStore.getExciter())
            audioEngine.setEqGain(parameterStore.getEqGain())
            audioEngine.setWidth(parameterStore.getWidth())

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
        try { learningBias.release() } catch (e: Exception) { Log.e(TAG, "Error releasing learningBias", e) }
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
