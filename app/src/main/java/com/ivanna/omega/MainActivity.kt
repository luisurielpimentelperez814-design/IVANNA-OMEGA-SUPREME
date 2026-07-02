package com.ivanna.omega

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
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
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.ivanna.omega.ai.SpectralClassifier
import com.ivanna.omega.audio.AudioEngine
import com.ivanna.omega.audio.AudioForegroundService
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.NoRootAudioProcessor
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.OmegaEngine
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPState

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

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            Log.i(TAG, "RECORD_AUDIO concedido — iniciando AudioEngine + Clasificador")
            initAudioEngine()
            SpectralClassifier.start()
        } else {
            Log.w(TAG, "RECORD_AUDIO denegado — funcionalidad limitada (sin clasificador)")
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
            SpectralClassifier.start()
        } else {
            requestPermissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
        }

        val serviceIntent = Intent(this, AudioForegroundService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)

        setContent {
            MaterialTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
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
                        }
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
        SpectralClassifier.stop()
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
    onEvoEnabledChange: (Boolean) -> Unit = {}
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

    // CABLEADO: lectura periódica del kernel evolutivo real (g_population),
    // acoplado a cues de audio reales dentro de PDEngine::process_block().
    LaunchedEffect(Unit) {
        while (true) {
            evoFitness = IvannaNativeLib.nativeGetBestFitness().toFloat()
            evoGeneration = IvannaNativeLib.nativeGetGeneration()
            kotlinx.coroutines.delay(1000)
        }
    }

    // CABLEADO: recolecta en vivo el SharedFlow real del SpectralClassifier (FFT en Kotlin).
    var classification by remember { mutableStateOf<SpectralClassifier.Classification?>(null) }
    var lastAutoPreset by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        SpectralClassifier.results.collect { result ->
            classification = result
            if (autoMode) {
                val target = AUTO_PRESET_MAP[result.label] ?: return@collect
                // Debounce: solo re-aplica el perfil si cambió la categoría detectada.
                if (target != lastAutoPreset) {
                    lastAutoPreset = target
                    selectedPreset = target
                    onPresetSelected(target)
                }
            }
        }
    }

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

        // ── Clasificador en vivo (SpectralClassifier — FFT real, sin modelo externo) ──
        Column(modifier = Modifier.fillMaxWidth()) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("Auto IA (clasificador en vivo)", style = MaterialTheme.typography.titleMedium)
                Switch(
                    checked = autoMode,
                    onCheckedChange = { enabled ->
                        autoMode = enabled
                        onAutoModeChange(enabled)
                        if (!enabled) lastAutoPreset = null
                    }
                )
            }

            val c = classification
            if (c == null) {
                Text(
                    "Escuchando…",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    "${c.label}  ·  ${"%.0f".format(c.confidence * 100)}%  ·  ${"%.0f".format(c.bpm)} BPM",
                    style = MaterialTheme.typography.bodyLarge
                )
                Spacer(modifier = Modifier.height(6.dp))
                EnergyBar("Bass", c.bassEnergy)
                EnergyBar("Mid", c.midEnergy)
                EnergyBar("High", c.highEnergy)
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
