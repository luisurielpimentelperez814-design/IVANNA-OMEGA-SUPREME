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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ivanna.omega.ui.viewmodels.PerceptualViewModel

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = Color(0xFF0A0C10)
                ) {
                    CognitiveDashboardScreen()
                }
            }
        }
    }
}

@Composable
fun CognitiveDashboardScreen(vm: PerceptualViewModel = viewModel()) {
    val state by vm.uiState.collectAsState()

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
                    text = "IVANNA OMEGA SUPREME v6.0",
                    color = Color(0xFFE2E8F0),
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
                Text(
                    text = "Cognitive Neural Audio Cortex",
                    color = Color(0xFF38BDF8),
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            Text(
                text = if (state.isBridgeConnected) "● MAGISK IPC ONLINE" else "○ IPC DISCONNECTED",
                color = if (state.isBridgeConnected) Color(0xFF4ADE80) else Color(0xFFF87171),
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
        }

        // Real-Time Evolution Graph
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E2330)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Perceptual Dynamics (Fatigue vs. Immersion)",
                    color = Color(0xFF94A3B8),
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
                    state.fatigueHistory.forEachIndexed { i: Int, valNorm: Float ->
                        val x = (i.toFloat() / (state.fatigueHistory.size - 1)) * width
                        val y = height - (valNorm * height)
                        if (i == 0) fatiguePath.moveTo(x, y) else fatiguePath.lineTo(x, y)
                    }
                    drawPath(fatiguePath, color = Color(0xFFF87171), style = Stroke(width = 3.dp.toPx()))

                    // Draw Immersion Line (Cyan)
                    val immersionPath = Path()
                    state.immersionHistory.forEachIndexed { i: Int, valNorm: Float ->
                        val x = (i.toFloat() / (state.immersionHistory.size - 1)) * width
                        val y = height - (valNorm * height)
                        if (i == 0) immersionPath.moveTo(x, y) else immersionPath.lineTo(x, y)
                    }
                    drawPath(immersionPath, color = Color(0xFF38BDF8), style = Stroke(width = 3.dp.toPx()))
                }
            }
        }

        // Neural Cortex Status Card
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E2330)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column {
                    Text("Neural Confidence", color = Color(0xFF94A3B8), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text("${state.neuralConfidencePercent}%", color = Color(0xFF4ADE80), fontSize = 16.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
                Column {
                    Text("Inference Latency", color = Color(0xFF94A3B8), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text("${state.inferenceLatencyUs} µs", color = Color(0xFF38BDF8), fontSize = 16.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
                Column {
                    Text("Spatial Mode", color = Color(0xFF94A3B8), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text(state.decision?.spatialMode ?: "STEREO", color = Color(0xFFF59E0B), fontSize = 12.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
            }
        }

        // Aggressiveness Slider
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E2330)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Cortex Aggressiveness Level", color = Color(0xFFE2E8F0), fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                    Text("${(state.aggressiveness * 100).toInt()}%", color = Color(0xFF38BDF8), fontSize = 12.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
                Slider(
                    value = state.aggressiveness,
                    onValueChange = { vm.setAggressiveness(it) },
                    valueRange = 0.0f..1.0f,
                    colors = SliderDefaults.colors(thumbColor = Color(0xFF38BDF8), activeTrackColor = Color(0xFF38BDF8))
                )
            }
        }

        // Reset Profile Action
        Button(
            onClick = { vm.resetToNeutralProfile() },
            modifier = Modifier.fillMaxWidth(),
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF334155)),
            shape = RoundedCornerShape(8.dp)
        ) {
            Text("Reset to Neutral Profile", color = Color(0xFFE2E8F0), fontFamily = FontFamily.Monospace)
        }
    }
}
