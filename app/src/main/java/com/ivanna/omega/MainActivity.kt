package com.ivanna.omega

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
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
import com.ivanna.omega.audio.AudioEngine
import com.ivanna.omega.audio.AudioForegroundService
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.NoRootAudioProcessor
import com.ivanna.omega.audio.PlaybackCaptureService
import com.ivanna.omega.audio.SpatialAudioEngineV2
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.OmegaEngine
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.ui.IvannaControlPanel
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import com.ivanna.omega.visualizer.VisualizerSurface
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

    // FIX: flujo real de MediaProjection para el visualizador — antes
    // onOpenVisualizer no solicitaba ningún permiso ni arrancaba el servicio.
    private var showVisualizer by mutableStateOf(false)

    private val mediaProjectionLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK && result.data != null) {
            Log.i(TAG, "MediaProjection concedido — arrancando PlaybackCaptureService")
            val intent = Intent(this, PlaybackCaptureService::class.java).apply {
                putExtra("resultCode", result.resultCode)
                putExtra("data", result.data)
            }
            ContextCompat.startForegroundService(this, intent)
            showVisualizer = true
        } else {
            Log.w(TAG, "MediaProjection denegado — visualizador mostrará silencio")
            showVisualizer = true // se abre igual, sólo que sin señal real
        }
    }

    private fun requestVisualizer() {
        val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        mediaProjectionLauncher.launch(mgr.createScreenCaptureIntent())
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

        // FIX: motor NPE (NHO+LIF+BiquadEnvelopeBank+AutonomousBrain) — sin
        // este init() el handle nativo se queda en 0 y todos los setters de
        // IvannaNpeEngine son no-ops silenciosos.
        IvannaNpeEngine.init(48000, NPE_BLOCK_FRAMES)
        IvannaNpeEngine.setBypass(parameterStore.isNpeBypass())
        IvannaNpeEngine.setNeuroParams(
            parameterStore.getNpeHarmonic(),
            parameterStore.getNpeLateralInhib(),
            parameterStore.getNpeOhcCompression(),
            parameterStore.getNpeMasterGain()
        )
        IvannaNpeEngine.setAGC(parameterStore.getNpeAgcTarget(), parameterStore.getNpeAgcRate())
        IvannaNpeEngine.setEngineFlags(
            parameterStore.getNpeHrtf(),
            parameterStore.getNpeCochlear(),
            parameterStore.getNpeAdapt()
        )
        IvannaNpeEngine.isManifoldEnabled = parameterStore.isNpeManifoldEnabled()

        // FIX: restaurar Compresor / NHO-Espacial / Kernel evolutivo / OmegaMode
        IvannaNativeLib.nativeSetCompressorParams(
            compThresholdSliderToDb(parameterStore.getCompThreshold()),
            compRatioSliderToRatio(parameterStore.getCompRatio())
        )
        IvannaNativeLib.nativeSetHarmonicGain(parameterStore.getNhoHarmonic())
        IvannaNativeLib.nativeSetSpatialAngleRad(parameterStore.getSpatialAngle())
        IvannaNativeLib.nativeSetSpatialWidthDirect(parameterStore.getSpatialWidth())
        OmegaEngine.setMode(parameterStore.getOmegaMode().coerceIn(0, 2))
        if (parameterStore.isEvoEnabled()) {
            IvannaNativeLib.nativeStartEvoThread()
        } else {
            IvannaNativeLib.nativeStopEvoThread()
        }
        // BUGFIX: NO arrancar spatialEngineV2 aquí. Esto crashea porque se ejecuta
        // antes de confirmar permiso RECORD_AUDIO. Se arranca en initAudioEngine()
        // DESPUÉS de confirmar el permiso de micrófono.
        // if (parameterStore.isSpatialEnabled()) spatialEngineV2.start()

        setContent {
            MaterialTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    if (showVisualizer) {
                        Box(modifier = Modifier.fillMaxSize()) {
                            VisualizerSurface(modifier = Modifier.fillMaxSize())
                            IconButtonClose { showVisualizer = false }
                        }
                    } else {
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
                            initialSpatialEnabled = parameterStore.isSpatialEnabled(),

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
                                if (mode == 3) IvannaNativeLib.nativeSetHRTFEnabled(true)
                            },
                            onCompThresholdChange = { slider ->
                                val ratioSlider = parameterStore.getCompRatio()
                                parameterStore.setCompParams(slider, ratioSlider)
                                IvannaNativeLib.nativeSetCompressorParams(
                                    compThresholdSliderToDb(slider),
                                    compRatioSliderToRatio(ratioSlider)
                                )
                            },
                            onCompRatioChange = { slider ->
                                val threshSlider = parameterStore.getCompThreshold()
                                parameterStore.setCompParams(threshSlider, slider)
                                IvannaNativeLib.nativeSetCompressorParams(
                                    compThresholdSliderToDb(threshSlider),
                                    compRatioSliderToRatio(slider)
                                )
                            },
                            onNhoHarmonicChange = { value ->
                                parameterStore.setNhoHarmonic(value)
                                IvannaNativeLib.nativeSetHarmonicGain(value)
                            },
                            onSpatialAngleChange = { rad ->
                                parameterStore.setSpatialAngle(rad)
                                IvannaNativeLib.nativeSetSpatialAngleRad(rad)
                            },
                            onSpatialWidthChange = { width ->
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
                                parameterStore.setSpatialEnabled(enabled)
                                if (enabled) spatialEngineV2.start() else spatialEngineV2.stop()
                            },
                            onOpenVisualizer = { requestVisualizer() }
                        )
                    }
                }
            }
        }

        if (parameterStore.isAutoModeEnabled()) startAutoPresetLoop()
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

    private fun initAudioEngine() {
        audioEngine.initialize(48000)
        audioEngine.setExciter(parameterStore.getExciter())
        audioEngine.setEqGain(parameterStore.getEqGain())
        audioEngine.setWidth(parameterStore.getWidth())

        // BUGFIX: Ahora que el permiso RECORD_AUDIO está confirmado, arrancar
        // SpatialAudioEngineV2 si está habilitado (se guardó de sesiones previas)
        if (parameterStore.isSpatialEnabled()) {
            Log.i(TAG, "Permiso confirmado — iniciando SpatialAudioEngineV2")
            spatialEngineV2.start()
        }

        // Modo no-root si no hay Magisk
        noRootProcessor = NoRootAudioProcessor(this)
        if (!noRootProcessor!!.hasMagisk()) {
            Log.i(TAG, "Iniciando modo no-root")
            noRootProcessor!!.start()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        autoPresetJob?.cancel()
        noRootProcessor?.stop()
        audioEngine.release()
        spatialEngineV2.stop()
        IvannaVisualizerBridgeV2.release()
        IvannaNpeEngine.release()
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
