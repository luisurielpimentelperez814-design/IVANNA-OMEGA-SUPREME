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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.objects.AudioScene

@Composable
fun Immersive3DView(
    audioScene: AudioScene,
    cpuLoad: String = "1.2%",
    latencyMs: String = "1.8ms",
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(16.dp),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = Color(0xFF0F172A))
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "IVANNA 3D OBJECT VISUALIZER v9.0",
                    color = Color(0xFF38BDF8),
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "CPU: $cpuLoad | Latencia: $latencyMs",
                    color = Color(0xFF94A3B8),
                    fontSize = 12.sp
                )
            }

            Spacer(modifier = Modifier.height(16.dp))

            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(240.dp)
                    .background(Color(0xFF020617), shape = RoundedCornerShape(12.dp))
            ) {
                val center = Offset(size.width / 2f, size.height / 2f)
                val radius = minOf(size.width, size.height) * 0.4f

                drawCircle(color = Color(0xFF1E293B), radius = radius, center = center, style = Stroke(width = 2f))
                drawCircle(color = Color(0xFF0F172A), radius = radius * 0.66f, center = center, style = Stroke(width = 1.5f))
                drawCircle(color = Color(0xFF0F172A), radius = radius * 0.33f, center = center, style = Stroke(width = 1.5f))

                drawCircle(color = Color(0xFF38BDF8), radius = 10f, center = center)

                for (obj in audioScene.objects) {
                    val objX = center.x + obj.positionX * radius * 0.8f
                    val objY = center.y - obj.positionZ * radius * 0.8f

                    val color = when (obj.priority) {
                        10 -> Color(0xFFF43F5E)
                        in 7..9 -> Color(0xFF818CF8)
                        else -> Color(0xFF34D399)
                    }

                    drawCircle(color = color, radius = 12f, center = Offset(objX, objY))
                    drawCircle(color = color.copy(alpha = 0.3f), radius = 20f, center = Offset(objX, objY))
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            Text(
                text = "Objetos Activos: ${audioScene.activeObjectCount} | Renderizado VBAP + HRTF CIPIC",
                color = Color(0xFF64748B),
                fontSize = 12.sp
            )
        }
    }
}
