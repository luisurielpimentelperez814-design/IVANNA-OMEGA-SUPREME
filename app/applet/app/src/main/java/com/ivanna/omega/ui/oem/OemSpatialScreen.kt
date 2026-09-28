package com.ivanna.omega.ui.oem

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.*
import kotlin.math.*

@Composable
fun OemSpatialScreen(
    state: OemState,
    expertMode: Boolean,
    onBack: () -> Unit,
    onSetWidth: (Float) -> Unit,
    onSetAngle: (Float) -> Unit,
) {
    Column(
        Modifier.fillMaxSize().background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
    ) {
        // Header
        Row(Modifier.fillMaxWidth().background(ObsidianSoft).padding(14.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            IconButton(onClick = onBack, modifier = Modifier.size(32.dp)) {
                Icon(Icons.Default.ArrowBack, null, tint = AuroraCyan)
            }
            Column(Modifier.weight(1f)) {
                Text("AUDIO ESPACIAL", color = AuroraCyan, fontWeight = FontWeight.ExtraBold,
                    fontSize = 12.sp, letterSpacing = 1.5.sp)
                Text("HRTF · SOFA · Binaural 3D", color = TextMuted, fontSize = 9.sp)
            }
            Box(Modifier.size(8.dp).clip(CircleShape)
                .background(if (state.hrtfReady) PhosphorGreen else AmberSignal))
        }

        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)) {

            // HRTF Status Card
            OemCard(Modifier.fillMaxWidth()) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Text("HRTF · SOFA", color = TextMuted, fontSize = 8.sp,
                        fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                    Surface(shape = RoundedCornerShape(4.dp),
                        color = if (state.hrtfReady) PhosphorGreen.copy(0.15f) else CoralWarn.copy(0.15f)) {
                        Text(
                            if (state.hrtfReady) "CARGADO ✓" else "NO INICIALIZADO",
                            color = if (state.hrtfReady) PhosphorGreen else CoralWarn,
                            fontSize = 8.sp, fontWeight = FontWeight.Bold,
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)
                        )
                    }
                }
                Spacer(Modifier.height(8.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    Column(Modifier.weight(1f)) {
                        TelRow("Sujeto activo", state.hrtfSubject, AuroraCyan)
                        TelRow("Dataset", "CIPIC/KEMAR", TextSecondary)
                        TelRow("Formato", "IHR1 precalculado", TextSecondary)
                        TelRow("Interpolación", "VBAP lineal", TextSecondary)
                    }
                    Column(Modifier.weight(1f)) {
                        TelRow("Error ITD", "< 13.5 µs", PhosphorGreen)
                        TelRow("Error ILD", "< 0.7 dB", PhosphorGreen)
                        TelRow("Interp error", "< 0.4 dB", PhosphorGreen)
                        TelRow("Ancho actual", "${"%.2f".format(state.spatialWidth)}", AuroraCyan)
                    }
                }
            }

            // 3D Position Map
            OemCard(Modifier.fillMaxWidth()) {
                Text("MAPA DE POSICIÓN 3D", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))
                Box(Modifier.fillMaxWidth().height(160.dp), contentAlignment = Alignment.Center) {
                    androidx.compose.foundation.Canvas(Modifier.size(150.dp)) {
                        drawSpatialMap(state.spatialWidth)
                    }
                    Text("FRENTE", color = TextMuted, fontSize = 7.sp,
                        modifier = Modifier.align(Alignment.TopCenter).offset(y = 4.dp))
                    Text("IZQ", color = TextMuted, fontSize = 7.sp,
                        modifier = Modifier.align(Alignment.CenterStart).offset(x = 4.dp))
                    Text("DER", color = TextMuted, fontSize = 7.sp,
                        modifier = Modifier.align(Alignment.CenterEnd).offset(x = (-4).dp))
                }
            }

            // Spatial Controls
            OemCard(Modifier.fillMaxWidth()) {
                Text("CONTROLES ESPACIALES", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))

                var width by remember { mutableFloatStateOf(state.spatialWidth.coerceIn(0f, 2f)) }
                OemSliderRow("Anchura espacial", width, 0f, 2f, "${"%.2f".format(width)}×") {
                    width = it; onSetWidth(it)
                }

                if (expertMode) {
                    var angle by remember { mutableFloatStateOf(0f) }
                    OemSliderRow("Ángulo (°)", angle, -180f, 180f, "${"%.0f".format(angle)}°") {
                        angle = it; onSetAngle(it)
                    }
                }
            }

            // Binaural processing info
            OemCard(Modifier.fillMaxWidth()) {
                Text("PROCESAMIENTO BINAURAL", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(6.dp))
                TelRow("Estado HRTFConvolver", if (state.hrtfReady) "FFT OLA 512pt" else "—", TextSecondary)
                TelRow("Crossfade IR", "32 bloques × 512 = 341ms", TextSecondary)
                TelRow("Precisión twiddle", "double precis. (sin acum.)", PhosphorGreen)
                TelRow("NaN guard FFT", "activo (self-reset)", PhosphorGreen)
                TelRow("memory_order", "release/acquire sincronizado", PhosphorGreen)
                if (!state.hrtfReady) {
                    Spacer(Modifier.height(6.dp))
                    Surface(shape = RoundedCornerShape(6.dp), color = AmberSignal.copy(0.12f)) {
                        Text("Abrir CALIBRACIÓN Φ_SAF para inicializar el renderer HRTF.",
                            color = AmberSignal, fontSize = 9.sp,
                            modifier = Modifier.padding(10.dp))
                    }
                }
            }
        }
    }
}

private fun DrawScope.drawSpatialMap(width: Float) {
    val cx = size.width / 2
    val cy = size.height / 2
    val r = size.minDimension / 2 - 4f
    // Outer ring
    drawCircle(color = Color(0xFF223050), radius = r, style = Stroke(1.5f))
    drawCircle(color = Color(0xFF152035), radius = r * 0.6f, style = Stroke(1f))
    drawCircle(color = Color(0xFF0D1828), radius = r * 0.3f, style = Stroke(1f))
    // Crosshairs
    drawLine(Color(0xFF223050), start = androidx.compose.ui.geometry.Offset(cx, cy - r),
        end = androidx.compose.ui.geometry.Offset(cx, cy + r), strokeWidth = 1f)
    drawLine(Color(0xFF223050), start = androidx.compose.ui.geometry.Offset(cx - r, cy),
        end = androidx.compose.ui.geometry.Offset(cx + r, cy), strokeWidth = 1f)
    // Spatial field ellipse — width > 1 = wider stereo field
    val fieldW = r * 0.7f * width.coerceIn(0.2f, 2f)
    val fieldH = r * 0.45f
    drawOval(color = Color(0x336FF3FF), topLeft = androidx.compose.ui.geometry.Offset(cx - fieldW, cy - fieldH),
        size = androidx.compose.ui.geometry.Size(fieldW * 2, fieldH * 2))
    drawOval(color = Color(0xFF6FF3FF), topLeft = androidx.compose.ui.geometry.Offset(cx - fieldW, cy - fieldH),
        size = androidx.compose.ui.geometry.Size(fieldW * 2, fieldH * 2), style = Stroke(1.5f))
    // Listener dot
    drawCircle(color = Color(0xFF23F09A), radius = 5f,
        center = androidx.compose.ui.geometry.Offset(cx, cy))
}
