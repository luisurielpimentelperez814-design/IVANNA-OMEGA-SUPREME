package com.ivanna.omega

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.ivanna.omega.audio.AudioEngine
import com.ivanna.omega.audio.AudioForegroundService
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.NoRootAudioProcessor
import com.ivanna.omega.audio.PlaybackCaptureService
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.OmegaEngine
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.visualizer.VisualizerSurface

/**
 * MainActivity v1.7 — UI Compose Material3
 *
 * CABLEADO v1.7 (esta entrega):
 *   1. Selector de motor OPE (PDEngine): DSP / DSP+NHO / DSP+NHO+Spatial,
 *      conectado a OmegaEngine.setMode() y persistido en ParameterStore.
 *      Requirió arreglar DSPBridge_nativeProcess (ivanna_omega_jni.cpp):
 *      antes trataba el buffer estéreo intercalado como mono (L==R
 *      aliased) y nunca llamaba a g_pd.process_block(), así que el modo
 *      no tenía ningún efecto audible sin importar su valor.
 *
 * CABLEADO v1.6:
 *   1. Presets reales de IvannaEffectProfile (Flat/Warm/Rock 70s/Spatial/Punch)
 *      expuestos como chips seleccionables, conectados a
 *      IVANNAApplication.globalEffectManager.applyProfile() — antes solo
 *      existían en código, sin control de UI.
 *   2. SpectralClassifier (FFT real en Kotlin) arrancado/detenido con el
 *      ciclo de vida de la Activity y mostrado en vivo (etiqueta, confianza,
 *      BPM, energía bass/mid/high) — antes no se invocaba desde ningún sitio.
 *   3. Modo "Auto IA": cuando está activo, el resultado del clasificador
 *      selecciona el preset automáticamente (con debounce por cambio de
 *      etiqueta, no por cada frame) y deshabilita la selección manual.
 *   4. Persistencia del preset y del modo auto en ParameterStore.
 *
 * FIXES DE CONECTIVIDAD (heredados v1.5):
 *   1. Permiso RECORD_AUDIO en runtime antes de iniciar AudioEngine.
 *   2. AudioForegroundService para que el procesamiento no muera en background.
 *   3. Toggle Anti-Dolby conectado a IvannaGlobalEffectManager.
 *   4. Estado Anti-Dolby restaurado desde ParameterStore al iniciar.
 */
class MainActivity : ComponentActivity() {
    companion object {
        private const val TAG = "MainActivity"
    }

    private lateinit var audioEngine: AudioEngine
    private lateinit var parameterStore: ParameterStore
    private var noRootProcessor: NoRootAudioProcessor? = null
    private var liveDspState = DSPState()

    private val mediaProjectionManager by lazy {
        getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
    }

    // FIX: motor de audio real ahora requiere consentimiento de captura de
    // pantalla (MediaProjection) para leer el audio interno digitalmente en
    // vez de usar el micrófono (que recapturaba la salida y sonaba horrible).
    private val screenCaptureLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK && result.data != null) {
            val serviceIntent = Intent(this, PlaybackCaptureService::class.java).apply {
                putExtra("resultCode", result.resultCode)
                putExtra("data", result.data)
            }
            ContextCompat.startForegroundService(this, serviceIntent)
            Log.i(TAG, "Captura de audio de reproducción iniciada")
        } else {
            Log.w(TAG, "Captura de audio de reproducción denegada")
        }
    }

    private fun requestPlaybackCapture() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            screenCaptureLauncher.launch(mediaProjectionManager.createScreenCaptureIntent())
        }
    }

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            initAudioEngine()
            requestPlaybackCapture()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        parameterStore = ParameterStore(this)
        audioEngine = AudioEngine()

        val hasMicPermission = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED

        if (hasMicPermission) {
            initAudioEngine()
            requestPlaybackCapture()
        } else {
            requestPermissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
        }

        val serviceIntent = Intent(this, AudioForegroundService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)

        setContent {
            var showVisualizer by remember { mutableStateOf(false) }
            MaterialTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    if (showVisualizer) {
                        Box(modifier = Modifier.fillMaxSize()) {
                            VisualizerSurface(modifier = Modifier.fillMaxSize())
                            IconButton(
                                onClick = { showVisualizer = false },
                                modifier = Modifier.align(Alignment.TopStart)
                            ) {
                                Icon(Icons.Default.Close, contentDescription = "Cerrar visualizador", tint = Color.White)
                            }
                        }
                        return@Surface
                    }
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
                        initialNpeHarmonic = parameterStore.getNpeHarmonicGain(),
                        initialNpeLateralInhib = parameterStore.getNpeLateralInhib(),
                        initialNpeOhcCompression = parameterStore.getNpeOhcCompression(),
                        initialNpeMasterGain = parameterStore.getNpeMasterGainDb(),
                        initialNpeAgcTarget = parameterStore.getNpeAgcTargetDb(),
                        initialNpeAgcRate = parameterStore.getNpeAgcRate(),
                        initialNpeHrtf = parameterStore.isNpeHrtfEnabled(),
                        initialNpeCochlear = parameterStore.isNpeCochlearEnabled(),
                        initialNpeAdapt = parameterStore.isNpeAdaptEnabled(),
                        onExciterChange = { value ->
                            parameterStore.setExciter(value)
                            audioEngine.setExciter(value)
                        },
                        onEqChange = { value ->
                            parameterStore.setEqGain(value)
                            audioEngine.setEqGain(value)
                        },
                        onWidthChange = { value ->
                            parameterStore.setWidth(value)
                            audioEngine.setWidth(value)
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
                        // CABLEADO: selección manual de preset → efecto global real
                        onPresetSelected = { name ->
                            parameterStore.setCurrentPreset(name)
                            val profile = IvannaEffectProfile.byName[name]
                            if (profile != null) {
                                val app = application as? IVANNAApplication
                                app?.globalEffectManager?.applyProfile(profile)
                                Log.i(TAG, "Preset aplicado: $name")
                            }
                        },
                        // CABLEADO: modo automático guiado por el clasificador FFT
                        onAutoModeChange = { enabled ->
                            parameterStore.setAutoModeEnabled(enabled)
                            Log.i(TAG, "Auto IA: ${if (enabled) "ON" else "OFF"}")
                        },
                        // CABLEADO v1.7: selector de motor PDEngine (NHO / Spatial).
                        // Antes el modo se seteaba en g_pd pero nativeProcess() nunca
                        // llamaba a process_block(), así que no tenía ningún efecto
                        // audible sin importar qué modo estuviera activo.
                        onOmegaModeChange = { mode ->
                            parameterStore.setOmegaMode(mode)
                            OmegaEngine.setMode(mode)
                            Log.i(TAG, "PDEngine mode: $mode")
                        },
                        // CABLEADO: Compresor real (g_comp) — vive en el mismo motor
                        // nativo (DSPBridge) que ya procesa audio vía AudioForegroundService.
                        // alpha→threshold(-24..0dB), beta→ratio(1:1..20:1).
                        onCompThresholdChange = { value ->
                            parameterStore.setCompThreshold(value)
                            liveDspState = liveDspState.copy(alpha = value)
                            liveDspState.pushToNative()
                        },
                        onCompRatioChange = { value ->
                            parameterStore.setCompRatio(value)
                            liveDspState = liveDspState.copy(beta = value)
                            liveDspState.pushToNative()
                        },
                        // CABLEADO: NHO harmonic gain y parámetros espaciales del PDEngine
                        // (g_pd), activo solo cuando Motor OPE está en +NHO / +NHO+Spatial.
                        onNhoHarmonicChange = { value ->
                            parameterStore.setNhoHarmonic(value)
                            IvannaNativeLib.nativeSetHarmonicGain(value)
                        },
                        onSpatialAngleChange = { value ->
                            parameterStore.setSpatialAngle(value)
                            IvannaNativeLib.nativeSetGamma(value)
                        },
                        onSpatialWidthChange = { value ->
                            parameterStore.setSpatialWidth(value)
                            IvannaNativeLib.nativeSetDelta(value)
                        },
                        // CABLEADO: kernel evolutivo (evo_thread_ dentro de g_pd) — ya
                        // arranca solo en DSPBridge.nativeInit; este switch permite pararlo.
                        onEvoEnabledChange = { enabled ->
                            parameterStore.setEvoEnabled(enabled)
                            if (enabled) IvannaNativeLib.nativeStartEvoThread()
                            else IvannaNativeLib.nativeStopEvoThread()
                        },
                        // CABLEADO: motor NPE (NHO+LIF+BiquadEnvelopeBank+AutonomousBrain)
                        // — llama directo a IvannaNpeEngine, que corre en el mismo hilo
                        // de audio real de PlaybackCaptureService (impacto audible directo).
                        onNpeBypassChange = { enabled ->
                            parameterStore.setNpeBypass(enabled)
                            IvannaNpeEngine.setBypass(enabled)
                        },
                        onNpeHarmonicChange = { value ->
                            parameterStore.setNpeHarmonicGain(value)
                            IvannaNpeEngine.setNeuroParams(
                                value, parameterStore.getNpeLateralInhib(),
                                parameterStore.getNpeOhcCompression(), parameterStore.getNpeMasterGainDb()
                            )
                        },
                        onNpeLateralInhibChange = { value ->
                            parameterStore.setNpeLateralInhib(value)
                            IvannaNpeEngine.setNeuroParams(
                                parameterStore.getNpeHarmonicGain(), value,
                                parameterStore.getNpeOhcCompression(), parameterStore.getNpeMasterGainDb()
                            )
                        },
                        onNpeOhcCompressionChange = { value ->
                            parameterStore.setNpeOhcCompression(value)
                            IvannaNpeEngine.setNeuroParams(
                                parameterStore.getNpeHarmonicGain(), parameterStore.getNpeLateralInhib(),
                                value, parameterStore.getNpeMasterGainDb()
                            )
                        },
                        onNpeMasterGainChange = { value ->
                            parameterStore.setNpeMasterGainDb(value)
                            IvannaNpeEngine.setNeuroParams(
                                parameterStore.getNpeHarmonicGain(), parameterStore.getNpeLateralInhib(),
                                parameterStore.getNpeOhcCompression(), value
                            )
                        },
                        onNpeAgcChange = { target, rate ->
                            parameterStore.setNpeAgcTargetDb(target)
                            parameterStore.setNpeAgcRate(rate)
                            IvannaNpeEngine.setAGC(target, rate)
                        },
                        onNpeFlagsChange = { hrtf, cochlear, adapt ->
                            parameterStore.setNpeHrtfEnabled(hrtf)
                            parameterStore.setNpeCochlearEnabled(cochlear)
                            parameterStore.setNpeAdaptEnabled(adapt)
                            IvannaNpeEngine.setEngineFlags(hrtf, cochlear, adapt)
                        },
                        onOpenVisualizer = { showVisualizer = true }
                    )
                }
            }
        }
    }

    private fun initAudioEngine() {
        audioEngine.initialize(48000)
        audioEngine.setExciter(parameterStore.getExciter())
        audioEngine.setEqGain(parameterStore.getEqGain())
        audioEngine.setWidth(parameterStore.getWidth())

        // Aplica el preset persistido al motor de efectos globales al arrancar
        val profile = IvannaEffectProfile.byName[parameterStore.getCurrentPreset()]
        if (profile != null) {
            (application as? IVANNAApplication)?.globalEffectManager?.applyProfile(profile)
        }

        noRootProcessor = NoRootAudioProcessor(this)
        if (!noRootProcessor!!.hasMagisk()) {
            Log.i(TAG, "Iniciando modo no-root")
            noRootProcessor!!.start()
        }

        // CABLEADO: restaura el modo PDEngine persistido (0=DSP, 1=+NHO, 2=+NHO+Spatial)
        OmegaEngine.setMode(parameterStore.getOmegaMode())

        // CABLEADO: restaura compresor (g_comp) y parámetros NHO/espaciales (g_pd)
        // sobre el motor DSPBridge que ya corre en AudioForegroundService.
        liveDspState = liveDspState.copy(
            alpha = parameterStore.getCompThreshold(),
            beta = parameterStore.getCompRatio()
        )
        liveDspState.pushToNative()
        IvannaNativeLib.nativeSetHarmonicGain(parameterStore.getNhoHarmonic())
        IvannaNativeLib.nativeSetGamma(parameterStore.getSpatialAngle())
        IvannaNativeLib.nativeSetDelta(parameterStore.getSpatialWidth())
        if (!parameterStore.isEvoEnabled()) IvannaNativeLib.nativeStopEvoThread()
    }

    override fun onDestroy() {
        super.onDestroy()
        noRootProcessor?.stop()
        audioEngine.release()
    }
}

// Mapa de sugerencia: etiqueta del clasificador → nombre de preset real.
private val AUTO_PRESET_MAP = mapOf(
    "Habla" to "Warm",
    "Música" to "Rock 70s",
    "Electrónica" to "Punch",
    "Silencio" to "Flat"
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun IvannaControlPanel(
    initialExciter: Float,
    initialEq: Float,
    initialWidth: Float,
    initialAntiDolby: Boolean = false,
    initialPreset: String = "Warm",
    initialAutoMode: Boolean = false,
    initialOmegaMode: Int = 0,
    initialCompThreshold: Float = 0.5f,
    initialCompRatio: Float = 0.16f,
    initialNhoHarmonic: Float = 0.0f,
    initialSpatialAngle: Float = 0.5f,
    initialSpatialWidth: Float = 0.5f,
    initialEvoEnabled: Boolean = true,
    initialNpeBypass: Boolean = false,
    initialNpeHarmonic: Float = 0.2f,
    initialNpeLateralInhib: Float = 0.2f,
    initialNpeOhcCompression: Float = 0.3f,
    initialNpeMasterGain: Float = 0.0f,
    initialNpeAgcTarget: Float = -18.0f,
    initialNpeAgcRate: Float = 0.3f,
    initialNpeHrtf: Boolean = true,
    initialNpeCochlear: Boolean = true,
    initialNpeAdapt: Boolean = true,
    onExciterChange: (Float) -> Unit,
    onEqChange: (Float) -> Unit,
    onWidthChange: (Float) -> Unit,
    onAntiDolbyChange: (Boolean) -> Unit = {},
    onPresetSelected: (String) -> Unit = {},
    onAutoModeChange: (Boolean) -> Unit = {},
    onOmegaModeChange: (Int) -> Unit = {},
    onCompThresholdChange: (Float) -> Unit = {},
    onCompRatioChange: (Float) -> Unit = {},
    onNhoHarmonicChange: (Float) -> Unit = {},
    onSpatialAngleChange: (Float) -> Unit = {},
    onSpatialWidthChange: (Float) -> Unit = {},
    onEvoEnabledChange: (Boolean) -> Unit = {},
    onNpeBypassChange: (Boolean) -> Unit = {},
    onNpeHarmonicChange: (Float) -> Unit = {},
    onNpeLateralInhibChange: (Float) -> Unit = {},
    onNpeOhcCompressionChange: (Float) -> Unit = {},
    onNpeMasterGainChange: (Float) -> Unit = {},
    onNpeAgcChange: (Float, Float) -> Unit = { _, _ -> },
    onNpeFlagsChange: (Boolean, Boolean, Boolean) -> Unit = { _, _, _ -> },
    onOpenVisualizer: () -> Unit = {}
) {
    var exciter by remember { mutableFloatStateOf(initialExciter) }
    var eq by remember { mutableFloatStateOf(initialEq) }
    var width by remember { mutableFloatStateOf(initialWidth) }
    var antiDolbyEnabled by remember { mutableStateOf(initialAntiDolby) }
    var selectedPreset by remember { mutableStateOf(initialPreset) }
    var autoMode by remember { mutableStateOf(initialAutoMode) }
    var omegaMode by remember { mutableIntStateOf(initialOmegaMode) }
    var compThreshold by remember { mutableFloatStateOf(initialCompThreshold) }
    var compRatio by remember { mutableFloatStateOf(initialCompRatio) }
    var nhoHarmonic by remember { mutableFloatStateOf(initialNhoHarmonic) }
    var spatialAngle by remember { mutableFloatStateOf(initialSpatialAngle) }
    var spatialWidth by remember { mutableFloatStateOf(initialSpatialWidth) }
    var evoEnabled by remember { mutableStateOf(initialEvoEnabled) }
    var evoFitness by remember { mutableFloatStateOf(0f) }
    var evoGeneration by remember { mutableIntStateOf(0) }
    var npeBypass by remember { mutableStateOf(initialNpeBypass) }
    var npeHarmonic by remember { mutableFloatStateOf(initialNpeHarmonic) }
    var npeLateralInhib by remember { mutableFloatStateOf(initialNpeLateralInhib) }
    var npeOhcCompression by remember { mutableFloatStateOf(initialNpeOhcCompression) }
    var npeMasterGain by remember { mutableFloatStateOf(initialNpeMasterGain) }
    var npeAgcTarget by remember { mutableFloatStateOf(initialNpeAgcTarget) }
    var npeAgcRate by remember { mutableFloatStateOf(initialNpeAgcRate) }
    var npeHrtf by remember { mutableStateOf(initialNpeHrtf) }
    var npeCochlear by remember { mutableStateOf(initialNpeCochlear) }
    var npeAdapt by remember { mutableStateOf(initialNpeAdapt) }
    var npeGenre by remember { mutableStateOf("\u2014") }

    // CABLEADO: lectura periódica del género detectado por AutonomousBrain
    // (motor NPE real, IvannaNpeEngine, corriendo en PlaybackCaptureService).
    LaunchedEffect(Unit) {
        while (true) {
            npeGenre = IvannaNpeEngine.getDetectedGenre()
            kotlinx.coroutines.delay(1000)
        }
    }

    // CABLEADO: lectura periódica del kernel evolutivo real (g_population),
    // acoplado a cues de audio reales dentro de PDEngine::process_block().
    LaunchedEffect(Unit) {
        while (true) {
            evoFitness = IvannaNativeLib.nativeGetBestFitness().toFloat()
            evoGeneration = IvannaNativeLib.nativeGetGeneration()
            kotlinx.coroutines.delay(1000)
        }
    }

    var lastAutoPreset by remember { mutableStateOf<String?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "IVANNA-OMEGA-SUPREME v1.6",
            style = MaterialTheme.typography.headlineMedium
        )
        Text(
            text = "Anti-Dolby Audio Engine",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.primary
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text("Modo Anti-Dolby", style = MaterialTheme.typography.bodyLarge)
            Switch(
                checked = antiDolbyEnabled,
                onCheckedChange = { enabled ->
                    antiDolbyEnabled = enabled
                    onAntiDolbyChange(enabled)
                }
            )
        }

        HorizontalDivider()

        ControlSlider(
            label = "Exciter",
            value = exciter,
            valueRange = 0f..1f,
            onValueChange = { exciter = it; onExciterChange(it) }
        )

        ControlSlider(
            label = "EQ Gain",
            value = eq,
            valueRange = -18f..18f,
            onValueChange = { eq = it; onEqChange(it) }
        )

        ControlSlider(
            label = "Stereo Width",
            value = width,
            valueRange = 0f..1f,
            onValueChange = { width = it; onWidthChange(it) }
        )

        HorizontalDivider()

        // ── Presets reales (IvannaEffectProfile) — antes solo existían en código ──
        Column(modifier = Modifier.fillMaxWidth()) {
            Text("Presets de sonido", style = MaterialTheme.typography.titleMedium)
            Spacer(modifier = Modifier.height(8.dp))
            LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(IvannaEffectProfile.byName.keys.toList()) { name ->
                    FilterChip(
                        selected = selectedPreset == name,
                        enabled = !autoMode,
                        onClick = {
                            selectedPreset = name
                            onPresetSelected(name)
                        },
                        label = { Text(name) }
                    )
                }
            }
        }

        HorizontalDivider()

        // ── Motor PDEngine (NHO harmonic shaping + Spatial ITD/ILD) ──
        // CABLEADO v1.7: antes solo existía en C++, sin ningún control.
        Column(modifier = Modifier.fillMaxWidth()) {
            Text("Motor OPE", style = MaterialTheme.typography.titleMedium)
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                when (omegaMode) {
                    1 -> "DSP + NHO (saturación armónica no lineal)"
                    2 -> "DSP + NHO + Spatial (ITD/ILD, imagen estéreo)"
                    else -> "Solo DSP (EQ/Comp/Exciter/Widener)"
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(8.dp))
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                listOf("DSP", "+NHO", "+NHO+Spatial").forEachIndexed { index, label ->
                    SegmentedButton(
                        selected = omegaMode == index,
                        onClick = {
                            omegaMode = index
                            onOmegaModeChange(index)
                        },
                        shape = SegmentedButtonDefaults.itemShape(index = index, count = 3)
                    ) {
                        Text(label)
                    }
                }
            }
        }

        HorizontalDivider()

        // ── Compresor (g_comp) — parte del motor DSPBridge activo ──
        Column(modifier = Modifier.fillMaxWidth()) {
            Text("Compresor", style = MaterialTheme.typography.titleMedium)
            Spacer(modifier = Modifier.height(4.dp))
            ControlSlider(
                label = "Threshold (${"%.1f".format(-24f + compThreshold * 24f)} dB)",
                value = compThreshold,
                valueRange = 0f..1f,
                onValueChange = { compThreshold = it; onCompThresholdChange(it) }
            )
            ControlSlider(
                label = "Ratio (${"%.1f".format(1f + compRatio * 19f)}:1)",
                value = compRatio,
                valueRange = 0f..1f,
                onValueChange = { compRatio = it; onCompRatioChange(it) }
            )
        }

        HorizontalDivider()

        // ── NHO / Motor espacial (g_pd) — activo en modo +NHO / +NHO+Spatial ──
        Column(modifier = Modifier.fillMaxWidth()) {
            Text("NHO / Espacial", style = MaterialTheme.typography.titleMedium)
            Spacer(modifier = Modifier.height(4.dp))
            ControlSlider(
                label = "Ganancia armónica",
                value = nhoHarmonic,
                valueRange = 0f..1f,
                onValueChange = { nhoHarmonic = it; onNhoHarmonicChange(it) }
            )
            ControlSlider(
                label = "Ángulo espacial",
                value = spatialAngle,
                valueRange = 0f..1f,
                onValueChange = { spatialAngle = it; onSpatialAngleChange(it) }
            )
            ControlSlider(
                label = "Ancho espacial",
                value = spatialWidth,
                valueRange = 0f..1f,
                onValueChange = { spatialWidth = it; onSpatialWidthChange(it) }
            )
        }

        HorizontalDivider()

        // ── Kernel evolutivo (g_population, hilo de fondo dentro de g_pd) ──
        Column(modifier = Modifier.fillMaxWidth()) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("Kernel evolutivo", style = MaterialTheme.typography.titleMedium)
                Switch(
                    checked = evoEnabled,
                    onCheckedChange = { evoEnabled = it; onEvoEnabledChange(it) }
                )
            }
            Text(
                "Generación $evoGeneration · fitness ${"%.3f".format(evoFitness)}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        HorizontalDivider()

        // ── Motor NPE (NHO+LIF+BiquadEnvelopeBank+AutonomousBrain) ──
        // Procesa en PlaybackCaptureService, después de DSPBridge, sobre el
        // mismo buffer intercalado — impacto audible directo.
        Column(modifier = Modifier.fillMaxWidth()) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("Motor NPE (neuromórfico)", style = MaterialTheme.typography.titleMedium)
                Switch(
                    checked = !npeBypass,
                    onCheckedChange = { on ->
                        npeBypass = !on
                        onNpeBypassChange(!on)
                    }
                )
            }
            Text(
                "Género detectado: $npeGenre",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            OutlinedButton(onClick = onOpenVisualizer, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.PlayArrow, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text("Abrir visualizador")
            }
            Spacer(modifier = Modifier.height(4.dp))
            ControlSlider(
                label = "Ganancia armónica (NHO)",
                value = npeHarmonic,
                valueRange = 0f..2f,
                onValueChange = { npeHarmonic = it; onNpeHarmonicChange(it) }
            )
            ControlSlider(
                label = "Inhibición lateral",
                value = npeLateralInhib,
                valueRange = 0f..1f,
                onValueChange = { npeLateralInhib = it; onNpeLateralInhibChange(it) }
            )
            ControlSlider(
                label = "Compresión OHC",
                value = npeOhcCompression,
                valueRange = 0f..1f,
                onValueChange = { npeOhcCompression = it; onNpeOhcCompressionChange(it) }
            )
            ControlSlider(
                label = "Master gain (dB)",
                value = npeMasterGain,
                valueRange = -18f..18f,
                onValueChange = { npeMasterGain = it; onNpeMasterGainChange(it) }
            )
            ControlSlider(
                label = "AGC target (dB)",
                value = npeAgcTarget,
                valueRange = -36f..0f,
                onValueChange = { npeAgcTarget = it; onNpeAgcChange(it, npeAgcRate) }
            )
            ControlSlider(
                label = "AGC rate",
                value = npeAgcRate,
                valueRange = 0f..1f,
                onValueChange = { npeAgcRate = it; onNpeAgcChange(npeAgcTarget, it) }
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("HRTF", style = MaterialTheme.typography.bodySmall)
                    Switch(checked = npeHrtf, onCheckedChange = { npeHrtf = it; onNpeFlagsChange(it, npeCochlear, npeAdapt) })
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Coclear", style = MaterialTheme.typography.bodySmall)
                    Switch(checked = npeCochlear, onCheckedChange = { npeCochlear = it; onNpeFlagsChange(npeHrtf, it, npeAdapt) })
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Adapt (LIF)", style = MaterialTheme.typography.bodySmall)
                    Switch(checked = npeAdapt, onCheckedChange = { npeAdapt = it; onNpeFlagsChange(npeHrtf, npeCochlear, it) })
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "\u00A9 2025-2026 IVANNA Team — Apache-2.0",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
fun EnergyBar(label: String, value: Float) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, style = MaterialTheme.typography.labelMedium, modifier = Modifier.width(48.dp))
        LinearProgressIndicator(
            progress = { value.coerceIn(0f, 1f) },
            modifier = Modifier
                .weight(1f)
                .height(8.dp)
        )
    }
}

@Composable
fun ControlSlider(
    label: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    onValueChange: (Float) -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            Text("%.1f".format(value), style = MaterialTheme.typography.bodyMedium)
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            modifier = Modifier.fillMaxWidth()
        )
    }
}
