package com.ivanna.omega.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ivanna.omega.ui.viewmodels.PerceptualViewModel
import com.ivanna.omega.ui.theme.*

/**
 * UI (2026-08-28): paleta slate/tailwind migrada a Aurora Obsidiana v3.0
 * (ObsidianVoid/Soft, AuroraCyan, PhosphorGreen, CoralWarn, AmberSignal) —
 * antes usaba su propio esquema (0A0C10/38BDF8/1E2330...) incoherente con
 * las demás 30 pantallas. Sustitución por valor, estructura intacta.
 *
 * FIX D: este archivo declaraba `class MainActivity` en com.ivanna.omega.ui,
 * homónimo del launcher real com.ivanna.omega.MainActivity (el único en el
 * AndroidManifest). Código muerto y fuente de ambigüedad en imports/Gradle.
 * No se borra: se renombra a CognitiveDashboardActivity, íntegro con su
 * CognitiveDashboardScreen.
 */
class CognitiveDashboardActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = ObsidianVoid
                ) {
                    CognitiveDashboardScreen()
                }
            }
        }
    }
}

@Composable
private fun appVersionName(): String {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    return remember {
        runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName }.getOrNull() ?: "?"
    }
}

@Composable
fun CognitiveDashboardScreen(vm: PerceptualViewModel = viewModel()) {
    val state by vm.uiState.collectAsState()
    val dspDecision by vm.dspDecision.collectAsState()
    val userProfile by vm.userProfile.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Top Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "IVANNA OMEGA SUPREME v${appVersionName()}",
                    color = TextPrimary,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
                Text(
                    text = "Cognitive Neural Audio Cortex",
                    color = AuroraCyan,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            Text(
                text = when {
                    state.isMagiskConnected -> "● MAGISK IPC ONLINE"
                    state.isBridgeConnected -> "● MOTOR NATIVO (sin módulo)"
                    else                    -> "○ SIN MOTOR"
                },
                color = when {
                    state.isMagiskConnected -> PhosphorGreen
                    state.isBridgeConnected -> AmberSignal
                    else                    -> CoralWarn
                },
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
        }

        // Real-Time Evolution Graph
        Card(
            colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Perceptual Dynamics (Fatigue vs. Immersion)",
                    color = TextSecondary,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
                Spacer(modifier = Modifier.height(12.dp))

                Canvas(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(120.dp)
                ) {
                    val width = size.width
                    val height = size.height

                    // Draw Fatigue Line (Red)
                    val fatiguePath = Path()
                    val fSize = state.fatigueHistory.size
                    if (fSize >= 2) {
                        state.fatigueHistory.forEachIndexed { i: Int, valNorm: Float ->
                            val x = (i.toFloat() / (fSize - 1)) * width
                            val y = height - (valNorm * height)
                            if (i == 0) fatiguePath.moveTo(x, y) else fatiguePath.lineTo(x, y)
                        }
                        drawPath(fatiguePath, color = CoralWarn, style = Stroke(width = 3.dp.toPx()))
                    }

                    // Draw Immersion Line (Cyan)
                    val immersionPath = Path()
                    val iSize = state.immersionHistory.size
                    if (iSize >= 2) {
                        state.immersionHistory.forEachIndexed { i: Int, valNorm: Float ->
                            val x = (i.toFloat() / (iSize - 1)) * width
                            val y = height - (valNorm * height)
                            if (i == 0) immersionPath.moveTo(x, y) else immersionPath.lineTo(x, y)
                        }
                        drawPath(immersionPath, color = AuroraCyan, style = Stroke(width = 3.dp.toPx()))
                    }
                }
            }
        }

        // Neural Cortex Status Card
        Card(
            colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
            shape = RoundedCornerShape(12.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column {
                    Text("Neural Confidence", color = TextSecondary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text("${state.neuralConfidencePercent}%", color = PhosphorGreen, fontSize = 16.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
                Column {
                    Text("Decision Time (medido)", color = TextSecondary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text(if (dspDecision.executionLatencyMs > 0f) "%.3f ms".format(dspDecision.executionLatencyMs) else "—", color = AuroraCyan, fontSize = 16.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
                Column {
                    Text("Spatial Mode", color = TextSecondary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text(dspDecision.spatialMode ?: "STEREO", color = AmberSignal, fontSize = 12.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
            }
        }

        // Aggressiveness Slider
        Card(
            colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Cortex Aggressiveness Level", color = TextPrimary, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                    Text("${(userProfile.aggressiveness * 100).toInt()}%", color = AuroraCyan, fontSize = 12.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
                Slider(
                    value = userProfile.aggressiveness,
                    onValueChange = { vm.setAggressiveness(it) },
                    valueRange = 0.0f..1.0f,
                    colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan)
                )
            }
        }

        // Reset Profile Action
        Button(
            onClick = { vm.resetToNeutralProfile() },
            modifier = Modifier.fillMaxWidth(),
            colors = ButtonDefaults.buttonColors(containerColor = ObsidianGlass),
            shape = RoundedCornerShape(8.dp)
        ) {
            Text("Reset to Neutral Profile", color = TextPrimary, fontFamily = FontFamily.Monospace)
        }
    }
}
