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
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.ivanna.omega.audio.AudioEngine
import com.ivanna.omega.audio.AudioForegroundService
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.NoRootAudioProcessor
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.ParameterStore

/**
 * MainActivity v1.5 — UI Compose Material3
 *
 * FIXES DE CONECTIVIDAD:
 *   1. Solicita permiso RECORD_AUDIO en runtime (Android 6+) antes de
 *      inicializar AudioEngine — sin esto el engine crashea en silencio.
 *   2. Arranca AudioForegroundService para que el procesamiento no sea
 *      matado por el sistema cuando la app va a background.
 *   3. Conecta el toggle Anti-Dolby al IvannaGlobalEffectManager
 *      via IVANNAApplication — antes el toggle era solo visual (no hacía nada).
 *   4. Restaura el estado Anti-Dolby desde ParameterStore al iniciar.
 */
class MainActivity : ComponentActivity() {
    companion object {
        private const val TAG = "MainActivity"
    }

    private lateinit var audioEngine: AudioEngine
    private lateinit var parameterStore: ParameterStore
    private var noRootProcessor: NoRootAudioProcessor? = null

    // FIX: solicitar permiso RECORD_AUDIO antes de iniciar el engine
    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            Log.i(TAG, "RECORD_AUDIO concedido — iniciando AudioEngine")
            initAudioEngine()
        } else {
            Log.w(TAG, "RECORD_AUDIO denegado — funcionalidad limitada")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        parameterStore = ParameterStore(this)
        audioEngine = AudioEngine()

        // FIX: pedir permiso antes de inicializar
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            == PackageManager.PERMISSION_GRANTED) {
            initAudioEngine()
        } else {
            requestPermissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
        }

        // FIX: arrancar el servicio en primer plano para audio en background
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
                        // FIX: restaurar estado guardado del toggle
                        initialAntiDolby = parameterStore.isAntiDolbyEnabled(),
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
                        // FIX: conectar toggle al GlobalEffectManager real
                        onAntiDolbyChange = { enabled ->
                            parameterStore.setAntiDolbyEnabled(enabled)
                            val app = application as? IVANNAApplication
                            if (enabled) {
                                app?.globalEffectManager?.applyProfile(IvannaEffectProfile.SPATIAL)
                            } else {
                                app?.globalEffectManager?.applyProfile(IvannaEffectProfile.FLAT)
                            }
                            Log.i(TAG, "Anti-Dolby: ${if (enabled) "ON" else "OFF"}")
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

        // Modo no-root si no hay Magisk
        noRootProcessor = NoRootAudioProcessor(this)
        if (!noRootProcessor!!.hasMagisk()) {
            Log.i(TAG, "Iniciando modo no-root")
            noRootProcessor!!.start()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        noRootProcessor?.stop()
        audioEngine.release()
    }
}

@Composable
fun IvannaControlPanel(
    initialExciter: Float,
    initialEq: Float,
    initialWidth: Float,
    initialAntiDolby: Boolean = false,
    onExciterChange: (Float) -> Unit,
    onEqChange: (Float) -> Unit,
    onWidthChange: (Float) -> Unit,
    onAntiDolbyChange: (Boolean) -> Unit = {}
) {
    var exciter by remember { mutableFloatStateOf(initialExciter) }
    var eq by remember { mutableFloatStateOf(initialEq) }
    var width by remember { mutableFloatStateOf(initialWidth) }
    // FIX: estado inicial desde ParameterStore, no siempre false
    var antiDolbyEnabled by remember { mutableStateOf(initialAntiDolby) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "IVANNA-OMEGA-SUPREME v1.5",
            style = MaterialTheme.typography.headlineMedium
        )
        Text(
            text = "Anti-Dolby Audio Engine",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.primary
        )

        // FIX: toggle ahora llama a onAntiDolbyChange (estaba desconectado)
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

        Spacer(modifier = Modifier.weight(1f))

        Text(
            text = "\u00A9 2025-2026 IVANNA Team — Apache-2.0",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
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
