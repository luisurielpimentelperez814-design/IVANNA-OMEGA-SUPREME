package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.AdaptiveMode
import com.ivanna.omega.audio.AntiDolbyController
import com.ivanna.omega.audio.AudioPipeline
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.audio.PlaybackCaptureService
import com.ivanna.omega.core.IvannaNativeLib
import androidx.compose.ui.platform.LocalContext
import kotlin.math.PI
import kotlinx.coroutines.delay

// ⚙️ 1. OPE DSP: Ecualizador y Compresor
@Composable
fun OpeEngineScreen(modifier: Modifier = Modifier) {
    // FASE 3E: nativeGetEvoBestFitness() está declarada y viva (el kernel
    // evolutivo real corre en su propio hilo nativo desde
    // MainActivity.onCreate -> nativeStartEvoThread) pero nunca se leía en
    // ninguna UI. nativeEvolveStep() del brief original NO se cablea aquí:
    // pertenece a un segundo kernel evolutivo legacy (evolutionary_kernel.cpp,
    // g_population) que nunca se inicializa (nativeInitializeEvolution jamás
    // se llama) — invocarlo operaría sobre estado no inicializado.
    var evoBestFitness by remember { mutableStateOf<Float?>(null) }
    LaunchedEffect(Unit) {
        while (true) {
            if (IvannaNativeLib.isLoaded) {
                evoBestFitness = runCatching { IvannaNativeLib.nativeGetEvoBestFitness() }.getOrNull()
            }
            delay(2000)
        }
    }
    // FIX (audit): antes los cuatro Sliders de esta pantalla tenían
    // onValueChange = { /* TODO: Bind to AudioStateManager */ } y
    // value fijo. Se cablean al StateFlow real de AudioStateManager y
    // se empuja el cambio al motor nativo por la misma ruta que ya usa
    // AdaptiveBackend.applyEQ / applyManualState:
    //   - eqBass/eqMid/eqTreble  -> nativeSetEQParams(low, mid, high, master)
    //   - compressorThreshold    -> nativeSetCompressorParams(th, ratio, attack, release)
    // Rangos: EQ ±12 dB (mismo que AdaptiveEngineScreen.kt:320-345),
    // threshold -40..0 dB (AdaptiveEngineScreen.kt:275).
    val audioState by AudioStateManager.state.collectAsState()
    fun pushEqNative(s: com.ivanna.omega.audio.AudioState) {
        if (!IvannaNativeLib.isLoaded) return
        runCatching {
            IvannaNativeLib.nativeSetEQParams(
                s.eqBass.coerceIn(-18f, 18f),
                s.eqMid.coerceIn(-18f, 18f),
                s.eqTreble.coerceIn(-18f, 18f),
                s.masterGain.coerceIn(0.1f, 2f)
            )
        }
    }
    fun pushCompNative(s: com.ivanna.omega.audio.AudioState) {
        if (!IvannaNativeLib.isLoaded) return
        runCatching {
            IvannaNativeLib.nativeSetCompressorParams(
                s.compressorThreshold.coerceIn(-60f, 0f),
                s.compressorRatio.coerceIn(1f, 20f),
                s.compressorAttack.coerceIn(0.1f, 500f),
                s.compressorRelease.coerceIn(1f, 2000f)
            )
        }
    }
    Column(modifier = modifier.padding(16.dp)) {
        Text("OPE DSP: Ecualizador", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(16.dp))

        Text("Graves (Low)  ${audioState.eqBass.toInt()} dB")
        Slider(
            value = audioState.eqBass,
            valueRange = -12f..12f,
            onValueChange = { v ->
                AudioStateManager.updateState { it.copy(eqBass = v) }
                pushEqNative(AudioStateManager.getCurrentState())
            }
        )

        Text("Medios (Mid)  ${audioState.eqMid.toInt()} dB")
        Slider(
            value = audioState.eqMid,
            valueRange = -12f..12f,
            onValueChange = { v ->
                AudioStateManager.updateState { it.copy(eqMid = v) }
                pushEqNative(AudioStateManager.getCurrentState())
            }
        )

        Text("Agudos (High)  ${audioState.eqTreble.toInt()} dB")
        Slider(
            value = audioState.eqTreble,
            valueRange = -12f..12f,
            onValueChange = { v ->
                AudioStateManager.updateState { it.copy(eqTreble = v) }
                pushEqNative(AudioStateManager.getCurrentState())
            }
        )

        Spacer(modifier = Modifier.height(24.dp))
        Text("OPE DSP: Compresor", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text("Umbral (Threshold)  ${audioState.compressorThreshold.toInt()} dB")
        Slider(
            value = audioState.compressorThreshold,
            valueRange = -40f..0f,
            onValueChange = { v ->
                AudioStateManager.updateState { it.copy(compressorThreshold = v) }
                pushCompNative(AudioStateManager.getCurrentState())
            }
        )

        Spacer(modifier = Modifier.height(24.dp))
        Text("Kernel Evolutivo (Motor Ω)", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text(
            evoBestFitness?.let { "Best fitness: %.4f".format(it) } ?: "Best fitness: —",
            fontSize = 14.sp
        )
    }
}

// 🎧 2. MOTOR BINAURAL: HRTF 32 Objetos
@Composable
fun BinauralScreen(modifier: Modifier = Modifier) {
    // FASE 3F: nativeSetHRTFEnabled estaba declarada e implementada
    // (ivanna_omega_jni.cpp:1017) pero el switch de esta pantalla nunca la
    // llamaba — quedaba como puro estado visual sin efecto en el motor.
    var hrtfEnabled by remember { mutableStateOf(true) }
    // FIX (audit): el slider "Ángulo Espacial" antes era Slider(0.7f, TODO).
    // Se cablea a nativeSetSpatialAngleRad (IvannaNativeLib.kt:118), con la
    // misma fórmula que ya usa MainActivity.kt:642 para el mismo control
    // en el dashboard: rad = (v - 0.5) * 2π, con v ∈ [0,1] (0.5 = frente).
    var angleNorm by remember { mutableStateOf(0.5f) }
    Column(modifier = modifier.padding(16.dp)) {
        Text("Motor Binaural (HRTF)", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(16.dp))

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Activar HRTF 32-Obj", modifier = Modifier.weight(1f))
            Switch(checked = hrtfEnabled, onCheckedChange = { on ->
                hrtfEnabled = on
                if (IvannaNativeLib.isLoaded) {
                    runCatching { IvannaNativeLib.nativeSetHRTFEnabled(on) }
                }
            })
        }

        Spacer(modifier = Modifier.height(16.dp))
        val degrees = ((angleNorm - 0.5f) * 360f).toInt()
        Text("Ángulo Espacial  ${degrees}°")
        Slider(
            value = angleNorm,
            valueRange = 0f..1f,
            onValueChange = { v ->
                angleNorm = v
                if (IvannaNativeLib.isLoaded && hrtfEnabled) {
                    val rad = (v - 0.5f) * 2f * PI.toFloat()
                    runCatching { IvannaNativeLib.nativeSetSpatialAngleRad(rad) }
                }
            }
        )
    }
}

// 🧠 3. MOTOR ADAPTATIVO: Anti-Dolby y Perfiles
//
// NOTA DE AUDITORÍA: esta pantalla NO está montada en el NavHost de
// MainActivity.kt (:250-290 sólo monta telemetry/ope/binaural/auditory/
// lab/adaptive_dash). Es huérfana de ruta — el usuario nunca la ve.
// Cablearla igual porque los controles ya sólo compilaban como máscara
// y, si alguien añade la ruta "adaptive_profiles", tiene que operar.
@Composable
fun AdaptiveProfilesScreen(modifier: Modifier = Modifier) {
    // FIX (audit): antes los tres botones eran /* TODO: Set Profile NATURAL/STUDIO/EXTREME */
    // y el switch de Anti-Dolby idem. Cableado real:
    //   - Botones -> AudioStateManager.updateState { adaptiveMode = ... }
    //     + nativeSetAdaptiveControls(ordinal, intensity*100) — misma
    //     ruta que AdaptiveEngineScreen.kt:200-215.
    //   - Switch -> AntiDolbyController(context).enable/disableAntiDolby()
    //     — misma instanciación que MainActivity.kt:454-465. Se guarda
    //     en remember para conservar el estado interno del controller.
    val context = LocalContext.current
    val audioState by AudioStateManager.state.collectAsState()
    val antiDolbyController = remember {
        AntiDolbyController(context).also { it.initialize() }
    }
    var antiDolbyOn by remember { mutableStateOf(false) }

    Column(modifier = modifier.padding(16.dp)) {
        Text("Motor Adaptativo & Perfiles", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(16.dp))

        fun selectProfile(mode: AdaptiveMode) {
            AudioStateManager.updateState { it.copy(adaptiveMode = mode) }
            if (IvannaNativeLib.isLoaded) {
                runCatching {
                    IvannaNativeLib.nativeSetAdaptiveControls(
                        mode.ordinal,
                        audioState.adaptiveIntensity * 100f
                    )
                }
            }
        }

        Row(horizontalArrangement = Arrangement.SpaceEvenly, modifier = Modifier.fillMaxWidth()) {
            Button(onClick = { selectProfile(AdaptiveMode.NATURAL) }) { Text("NATURAL") }
            Button(onClick = { selectProfile(AdaptiveMode.STUDIO) })  { Text("STUDIO") }
            Button(onClick = { selectProfile(AdaptiveMode.EXTREME) }) { Text("EXTREME") }
        }
        Spacer(modifier = Modifier.height(8.dp))
        Text("Modo actual: ${audioState.adaptiveMode.label}", fontSize = 12.sp)

        Spacer(modifier = Modifier.height(24.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Anti-Dolby (YAMNet/EMA)", modifier = Modifier.weight(1f))
            Switch(checked = antiDolbyOn, onCheckedChange = { on ->
                antiDolbyOn = on
                if (on) antiDolbyController.enableAntiDolby()
                else    antiDolbyController.disableAntiDolby()
            })
        }
    }
}

// 📊 4. TELEMETRÍA: Dashboard con YAMNet en tiempo real (3J)
@Composable
fun TelemetryDashboard(modifier: Modifier = Modifier) {
    // 3J: colectar género en tiempo real desde AudioPipeline.sharedYamnetResult
    val yamnet by AudioPipeline.sharedYamnetResult.collectAsState()
    // Distinguir "sin señal" (captura inactiva) de "clasificando" (captura activa, aún sin resultado)
    val isCapturing by PlaybackCaptureService.isCapturing.collectAsState()

    Column(modifier = modifier.padding(16.dp)) {
        Text("Telemetría en Tiempo Real", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(16.dp))

        // ── Clasificación YAMNet ─────────────────────────────────────────────
        Text("Clasificación YAMNet", fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF6FF3FF))
        Spacer(modifier = Modifier.height(8.dp))

        if (!yamnet.valid) {
            if (isCapturing) {
                // Pipeline activo pero el buffer YAMNet aún no se llenó
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(14.dp),
                        strokeWidth = 2.dp,
                        color = Color(0xFF6FF3FF)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Clasificando…", fontSize = 12.sp, color = Color(0xFF6FF3FF))
                }
            } else {
                Text("Sin señal de audio activa", fontSize = 12.sp, color = Color(0xFF57708F))
            }
        } else {
            Text("Voz / Speech")
            LinearProgressIndicator(
                progress = { yamnet.speech.coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().height(8.dp)
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text("%.0f%%".format(yamnet.speech * 100f), fontSize = 11.sp, color = Color(0xFF93A8C6))

            Spacer(modifier = Modifier.height(8.dp))
            Text("Música / Music")
            LinearProgressIndicator(
                progress = { yamnet.music.coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().height(8.dp),
                color = Color(0xFF23F09A)
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text("%.0f%%".format(yamnet.music * 100f), fontSize = 11.sp, color = Color(0xFF93A8C6))

            Spacer(modifier = Modifier.height(8.dp))
            Text("Graves / Bass")
            LinearProgressIndicator(
                progress = { yamnet.bass.coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().height(8.dp),
                color = Color(0xFFF7B733)
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text("%.0f%%".format(yamnet.bass * 100f), fontSize = 11.sp, color = Color(0xFF93A8C6))

            Spacer(modifier = Modifier.height(12.dp))
            val dominant = when {
                yamnet.speech >= yamnet.music && yamnet.speech >= yamnet.bass -> "VOZ"
                yamnet.music >= yamnet.bass -> "MÚSICA"
                else -> "GRAVES"
            }
            Text("Género dominante: $dominant",
                fontSize = 13.sp, fontWeight = FontWeight.Bold,
                color = Color(0xFFF1F8FF))
        }
    }
}
