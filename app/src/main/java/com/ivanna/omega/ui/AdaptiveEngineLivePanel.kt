package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.ui.theme.AmberSignal
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.CoralWarn
import com.ivanna.omega.ui.theme.GlassOutline
import com.ivanna.omega.ui.theme.ObsidianRaised
import com.ivanna.omega.ui.theme.PhosphorGreen
import com.ivanna.omega.ui.theme.TextMuted
import com.ivanna.omega.ui.theme.TextPrimary
import com.ivanna.omega.ui.theme.TextSecondary
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * Panel en vivo del Adaptive Engine + Pi-LSTM predictor.
 * Da vida a 4 entry points JNI que existían en la librería nativa pero nadie
 * llamaba desde Kotlin: nativeCreateAdaptiveEngine, nativeGetAdaptiveParameters,
 * nativeInitPILSTM y nativePredictSamples. Todo dato mostrado viene del JNI;
 * si el handle es 0 o la lectura falla, se muestra "—" (nunca se inventa).
 *
 * v2.5.369 (refactor visual): antes usaba una escala de grises propia
 * (0xFF101418, 0xFF1F2B2B, 0xFFA0A0A0, 0xFF666666, 0xFF7F8C8D) y un cian
 * 0xFF00E5FF distintos de la paleta Aurora Obsidiana. Ahora usa las
 * constantes del tema; los valores mostrados no cambian.
 */
@Composable
fun AdaptiveEngineLivePanel(modifier: Modifier = Modifier) {
    var engineHandle by remember { mutableStateOf(0L) }
    var pilstmReady by remember { mutableStateOf(false) }
    var params by remember { mutableStateOf<FloatArray?>(null) }
    var prediction by remember { mutableStateOf<FloatArray?>(null) }
    var clipCount by remember { mutableStateOf(-1) }

    // Creación perezosa del engine nativo (una sola vez, fuera del hilo de audio)
    LaunchedEffect(Unit) {
        engineHandle = withContext(Dispatchers.IO) {
            runCatching { IvannaNativeLib.nativeCreateAdaptiveEngine() }.getOrDefault(0L)
        }
        pilstmReady = withContext(Dispatchers.IO) {
            runCatching { IvannaNativeLib.nativeInitPILSTM(); true }.getOrDefault(false)
        }
    }

    // Telemetría a 4 Hz — parámetros adaptativos + predictor Pi-LSTM + clip counter
    LaunchedEffect(engineHandle, pilstmReady) {
        if (engineHandle == 0L && !pilstmReady) return@LaunchedEffect
        while (true) {
            if (engineHandle != 0L) {
                params = withContext(Dispatchers.IO) {
                    runCatching { IvannaNativeLib.nativeGetAdaptiveParameters() }.getOrNull()
                }
            }
            if (pilstmReady) {
                prediction = withContext(Dispatchers.IO) {
                    runCatching {
                        // Ventana nula de sondeo: el predictor devuelve su estado latente
                        IvannaNativeLib.nativePredictSamples(FloatArray(64), 64)
                    }.getOrNull()
                }
            }
            clipCount = withContext(Dispatchers.IO) {
                runCatching { IvannaNativeLib.nativeGetClipCount() }.getOrDefault(-1)
            }
            delay(250L)
        }
    }

    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(ObsidianRaised, RoundedCornerShape(12.dp))
            .border(1.dp, GlassOutline, RoundedCornerShape(12.dp))
            .padding(14.dp)
    ) {
        Text("ADAPTIVE ENGINE — EN VIVO", color = AuroraCyan,
             fontSize = 13.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 1.5.sp)
        Spacer(Modifier.height(10.dp))

        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Handle nativo", color = TextSecondary, fontSize = 11.sp)
            Text(if (engineHandle != 0L) "0x${engineHandle.toString(16)}" else "—",
                 color = if (engineHandle != 0L) PhosphorGreen else CoralWarn,
                 fontSize = 11.sp)
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Pi-LSTM predictor", color = TextSecondary, fontSize = 11.sp)
            Text(if (pilstmReady) "ACTIVO" else "—",
                 color = if (pilstmReady) PhosphorGreen else CoralWarn,
                 fontSize = 11.sp)
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Clips acumulados", color = TextSecondary, fontSize = 11.sp)
            Text(if (clipCount >= 0) "$clipCount" else "—",
                 color = if (clipCount > 0) AmberSignal else TextPrimary, fontSize = 11.sp)
        }
        Spacer(Modifier.height(8.dp))

        val p = params
        if (p == null) {
            Text("Parámetros adaptativos: — (engine no inicializado)",
                 color = TextMuted, fontSize = 11.sp)
        } else {
            Text("Parámetros adaptativos [${p.size}]", color = TextSecondary, fontSize = 11.sp)
            Spacer(Modifier.height(4.dp))
            val labels = listOf("threshold dB", "ratio", "attack ms", "release ms", "makeup dB", "knee")
            p.take(6).forEachIndexed { i, v ->
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text(labels.getOrElse(i) { "param[$i]" }, color = TextMuted, fontSize = 10.sp)
                    Text("%.3f".format(v), color = TextPrimary, fontSize = 10.sp)
                }
            }
        }

        val pr = prediction
        if (pr != null && pr.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Text("Pi-LSTM latente: ${"%.4f".format(pr.first())} (${pr.size} taps)",
                 color = AuroraCyan, fontSize = 10.sp)
        }

        if (clipCount > 0) {
            Spacer(Modifier.height(8.dp))
            TextButton(onClick = {
                runCatching { IvannaNativeLib.nativeResetClipCount() }
                clipCount = 0
            }) { Text("RESET CLIP COUNTER", color = AmberSignal, fontSize = 11.sp) }
        }
    }
}
