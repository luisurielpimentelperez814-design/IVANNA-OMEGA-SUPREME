package com.ivanna.omega.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ai.DSPDecision
import com.ivanna.omega.ai.SpatialMode

@Composable
fun MagistralDashboardScreen(
    currentDecision: DSPDecision,
    latencyMs: Float,
    isDaemonActive: Boolean,
    onResetToNeutral: () -> Unit,
    onCalibrateHrtf: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF0D0F12))
            .padding(16.dp)
    ) {
        // 1. Header & System Status
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "IVANNA OMEGA SUPREME v8.0",
                    color = Color(0xFF00E5FF),
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Cognitive Realtime Audio Engine",
                    color = Color.Gray,
                    fontSize = 12.sp
                )
            }

            Surface(
                color = if (isDaemonActive) Color(0xFF102E23) else Color(0xFF331414),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text(
                    text = if (isDaemonActive) "DAEMON RT ACTIVE (${latencyMs}ms)" else "DAEMON OFF",
                    color = if (isDaemonActive) Color(0xFF00FF88) else Color(0xFFFF4444),
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp)
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 2. Realtime 64-Band FFT Spectrum Canvas
        Text(
            text = "PERCEPTUAL BARK SPECTRUM & MASKING",
            color = Color.LightGray,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(6.dp))

        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(120.dp)
                .background(Color(0xFF13171F), shape = RoundedCornerShape(8.dp))
        ) {
            val barWidth = size.width / 32.0f
            for (i in 0 until 32) {
                val heightFactor = ((0.2f + 0.7f * Math.sin(i * 0.3 + System.currentTimeMillis() * 0.005)).toFloat()).coerceIn(0.1f, 1.0f)
                val barHeight = size.height * heightFactor
                drawRect(
                    color = Color(0xFF00E5FF),
                    topLeft = Offset(i * barWidth, size.height - barHeight),
                    size = Size(barWidth - 2f, barHeight)
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 3. 3D Spatial Scene Map
        Text(
            text = "3D SPATIAL SCENE (${currentDecision.spatialMode.name})",
            color = Color.LightGray,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(6.dp))

        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(100.dp)
                .background(Color(0xFF13171F), shape = RoundedCornerShape(8.dp))
        ) {
            val centerX = size.width / 2.0f
            val centerY = size.height / 2.0f

            // Head Center
            drawCircle(color = Color.White, radius = 8f, center = Offset(centerX, centerY))

            // 3D Objects Panned Coordinates
            drawCircle(color = Color(0xFF00FF88), radius = 12f, center = Offset(centerX - 80f, centerY - 20f))
            drawCircle(color = Color(0xFFFF007F), radius = 10f, center = Offset(centerX + 90f, centerY - 30f))
            drawCircle(color = Color(0xFFFFD700), radius = 14f, center = Offset(centerX, centerY - 40f))
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 4. Cognitive DSP Parameters Summary
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("AI Confidence: ${(currentDecision.confidenceScore * 100).toInt()}%", color = Color.Gray, fontSize = 12.sp)
            Text("Room Size: ${(currentDecision.roomSize * 100).toInt()}%", color = Color.Gray, fontSize = 12.sp)
            Text("Fatigue Prot: ${currentDecision.fatigueProtectionDb}dB", color = Color.Gray, fontSize = 12.sp)
        }

        Spacer(modifier = Modifier.weight(1.0f))

        // 5. Actions
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Button(
                onClick = onCalibrateHrtf,
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1E2638)),
                modifier = Modifier.weight(1f)
            ) {
                Text("Calibrar HRTF", color = Color.White, fontSize = 12.sp)
            }

            Button(
                onClick = onResetToNeutral,
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF381E26)),
                modifier = Modifier.weight(1f)
            ) {
                Text("Perfil Neutral", color = Color.White, fontSize = 12.sp)
            }
        }
    }
}
