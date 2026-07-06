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
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
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
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.ui.IvannaControlPanel
import com.ivanna.omega.ui.theme.IvannaTheme
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
            IvannaTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = androidx.compose.material3.MaterialTheme.colorScheme.background
                ) {
                    // [FIX-AURORA-BG] antes VisualizerSurface e IvannaControlPanel
                    // eran mutuamente excluyentes (if/return@Surface): el wallpaper
                    // aurora solo se veía en pantalla completa, nunca como fondo real
                    // de la interfaz. Ahora VisualizerSurface vive siempre como capa
                    // de fondo (z-index 0) y el panel de control se dibuja encima
                    // (z-index 1); showVisualizer solo decide si el panel se oculta
                    // para ver el wallpaper a pantalla completa.
                    Box(modifier = Modifier.fillMaxSize()) {
                        VisualizerSurface(modifier = Modifier.fillMaxSize())

                        if (showVisualizer) {
                            BackHandler(enabled = true) { showVisualizer = false }
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
                                initialNpeHarmonic = parameterStore.getNpeHarmonicGain(),
                                initialNpeLateralInhib = parameterStore.getNpeLateralInhib(),
                                initialNpeOhcCompression = parameterStore.getNpeOhcCompression(),
                                initialNpeMasterGain = parameterStore.getNpeMasterGainDb(),
                                initialNpeAgcTarget = parameterStore.getNpeAgcTargetDb(),
                                initialNpeAgcRate = parameterStore.getNpeAgcRate(),
                                initialNpeHrtf = parameterStore.isNpeHrtfEnabled(),
                                initialNpeCochlear = parameterStore.isNpeCochlearEnabled(),
                                initialNpeAdapt = parameterStore.isNpeAdaptEnabled(),
                                initialSpatialEnabled = parameterStore.isSpatialEngineEnabled(),
                                // CABLEADO-FIX: Exciter/EQ/Width solo llamaban a audioEngine.set*(),
                                // que escribe en gState de audio_orchestrator.cpp. Ese motor nunca
                                // procesa audio real (nativeProcessAudio/nativeProcessAudioDirect no
                                // los llama nadie) — el motor que SÍ suena es DSPBridge (ControlFrame
                                // → g_eq/g_exciter/g_widener/g_gain en ivanna_omega_jni.cpp), igual
                                // que ya hacían los sliders de Compresor (alpha/beta) más abajo.
                                // Se mantiene audioEngine.set*() para los getters de fusión en
                                // PDEngine (nativeGetExciterValue/EqGainDb/WidthValue) y se añade el
                                // push real al motor vivo.
                                onExciterChange = { value ->
                                    parameterStore.setExciter(value)
                                    audioEngine.setExciter(value)
                                    // HarmonicExciter: drive_ = 1+drive*5, wet_ = wet (setAmount semantics)
                                    liveDspState = liveDspState.copy(drive = value, wet = value)
                                    liveDspState.pushToNative()
                                },
                                onEqChange = { value ->
                                    parameterStore.setEqGain(value)
                                    audioEngine.setEqGain(value)
                                    // GainStage: outputGain = dbToLin(master) — pasa directo en dB
                                    liveDspState = liveDspState.copy(master = value)
                                    liveDspState.pushToNative()
                                },
                                onWidthChange = { value ->
                                    parameterStore.setWidth(value)
                                    audioEngine.setWidth(value)
                                    // StereoWidener: width_ = gamma * 1.5 → invertir escala UI [0..1.5]
                                    liveDspState = liveDspState.copy(gamma = value / 1.5f)
                                    liveDspState.pushToNative()
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
                                onOpenVisualizer = { showVisualizer = true },
                onSpatialEnabledChange = { enabled ->
                    parameterStore.setSpatialEngineEnabled(enabled)
                    PlaybackCaptureService.setSpatialEnabledLive(enabled)
                    Log.i(TAG, "Motor Espacial Binaural: ${if (enabled) "ON" else "OFF"}")
                }
                            )
                        }
                    }
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

        // CABLEADO: restaura compresor (g_comp), Exciter/EQ/Width y parámetros
        // NHO/espaciales (g_pd) sobre el motor DSPBridge que ya corre en
        // AudioForegroundService. Exciter/EQ/Width antes solo se restauraban
        // en el motor muerto (audio_orchestrator.cpp, ver arriba).
        liveDspState = liveDspState.copy(
            alpha = parameterStore.getCompThreshold(),
            beta = parameterStore.getCompRatio(),
            drive = parameterStore.getExciter(),
            wet = parameterStore.getExciter(),
            master = parameterStore.getEqGain(),
            gamma = parameterStore.getWidth() / 1.5f
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


// NOTA v2.0: IvannaControlPanel + ControlSlider + EnergyBar movidos a
// com.ivanna.omega.ui.IvannaControlPanel (rediseño magistral Aurora Obsidiana).
// Toda la lógica de callbacks se conserva idéntica — regla de oro cumplida.
