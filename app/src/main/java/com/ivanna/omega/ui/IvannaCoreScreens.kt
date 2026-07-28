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
import com.ivanna.omega.audio.AudioPipeline
import com.ivanna.omega.core.IvannaNativeLib
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
    Column(modifier = modifier.padding(16.dp)) {
        Text("OPE DSP: Ecualizador", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(16.dp))
        
        Text("Ganancia Graves (Low)")
        Slider(value = 0.5f, onValueChange = { /* TODO: Bind to AudioStateManager.eqLow */ })
        
        Text("Ganancia Medios (Mid)")
        Slider(value = 0.5f, onValueChange = { /* TODO: Bind to AudioStateManager.eqMid */ })
        
        Text("Ganancia Agudos (High)")
        Slider(value = 0.5f, onValueChange = { /* TODO: Bind to AudioStateManager.eqHigh */ })

        Spacer(modifier = Modifier.height(24.dp))
        Text("OPE DSP: Compresor", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text("Umbral (Threshold) dB")
        Slider(value = 0.8f, onValueChange = { /* TODO: Bind to AudioStateManager.compThreshold */ })

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
        Text("Ángulo Espacial")
        Slider(value = 0.7f, onValueChange = { /* TODO: Bind to AudioStateManager.hrtfAngle */ })
    }
}

// 🧠 3. MOTOR ADAPTATIVO: Anti-Dolby y Perfiles
@Composable
fun AdaptiveProfilesScreen(modifier: Modifier = Modifier) {
    Column(modifier = modifier.padding(16.dp)) {
        Text("Motor Adaptativo & Perfiles", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(16.dp))
        
        Row(horizontalArrangement = Arrangement.SpaceEvenly, modifier = Modifier.fillMaxWidth()) {
            Button(onClick = { /* TODO: Set Profile NATURAL */ }) { Text("NATURAL") }
            Button(onClick = { /* TODO: Set Profile STUDIO */ }) { Text("STUDIO") }
            Button(onClick = { /* TODO: Set Profile EXTREME */ }) { Text("EXTREME") }
        }

        Spacer(modifier = Modifier.height(24.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Anti-Dolby (YAMNet/EMA)", modifier = Modifier.weight(1f))
            Switch(checked = true, onCheckedChange = { /* TODO: Bind to AntiDolby toggle */ })
        }
    }
}

// 📊 4. TELEMETRÍA: Dashboard con YAMNet en tiempo real (3J)
@Composable
fun TelemetryDashboard(modifier: Modifier = Modifier) {
    // 3J: colectar género en tiempo real desde AudioPipeline.sharedYamnetResult
    val yamnet by AudioPipeline.sharedYamnetResult.collectAsState()

    Column(modifier = modifier.padding(16.dp)) {
        Text("Telemetría en Tiempo Real", fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(16.dp))

        // ── Clasificación YAMNet ─────────────────────────────────────────────
        Text("Clasificación YAMNet", fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF6FF3FF))
        Spacer(modifier = Modifier.height(8.dp))

        if (!yamnet.valid) {
            Text("Sin señal de audio activa", fontSize = 12.sp, color = Color(0xFF57708F))
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
