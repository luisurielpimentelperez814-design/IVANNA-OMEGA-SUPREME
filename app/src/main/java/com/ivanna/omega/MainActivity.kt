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
import com.ivanna.omega.core.ParameterStore

/**
 * MainActivity v1.6 — UI Compose Material3
 *
 * CABLEADO v1.6 (esta entrega):
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
                            val profile = IvannaEffectProfile.byName[name] ?: return@onPresetSelected
                            val app = application as? IVANNAApplication
                            app?.globalEffectManager?.applyProfile(profile)
                            Log.i(TAG, "Preset aplicado: $name")
                        },
                        // CABLEADO: modo automático guiado por el clasificador FFT
                        onAutoModeChange = { enabled ->
                            parameterStore.setAutoModeEnabled(enabled)
                            Log.i(TAG, "Auto IA: ${if (enabled) "ON" else "OFF"}")
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
    onExciterChange: (Float) -> Unit,
    onEqChange: (Float) -> Unit,
    onWidthChange: (Float) -> Unit,
    onAntiDolbyChange: (Boolean) -> Unit = {},
    onPresetSelected: (String) -> Unit = {},
    onAutoModeChange: (Boolean) -> Unit = {}
) {
    var exciter by remember { mutableFloatStateOf(initialExciter) }
    var eq by remember { mutableFloatStateOf(initialEq) }
    var width by remember { mutableFloatStateOf(initialWidth) }
    var antiDolbyEnabled by remember { mutableStateOf(initialAntiDolby) }
    var selectedPreset by remember { mutableStateOf(initialPreset) }
    var autoMode by remember { mutableStateOf(initialAutoMode) }

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
            valueRange = -12f..12f,
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
