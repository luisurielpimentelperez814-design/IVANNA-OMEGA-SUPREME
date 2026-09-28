package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.MusicIntelligenceWorker

/**
 * Panel MUSIC INTELLIGENCE — telemetría REAL del motor (PASO 6/7):
 * todo viene de MusicIntelligenceWorker.state (decide() nativo). Nada simulado.
 */
@Composable
fun MusicIntelligencePanel(onBack: () -> Unit) {
    val ctx = LocalContext.current
    val st by MusicIntelligenceWorker.state.collectAsState()
    var enabled by remember { mutableStateOf(MusicIntelligenceWorker.isEnabled(ctx)) }
    val bg = Color(0xFF0A0E14); val accent = Color(0xFF00E5FF); val dim = Color(0xFF9AA4B2)

    Column(
        Modifier.fillMaxSize().background(bg).windowInsetsPadding(WindowInsets.systemBars)
            .verticalScroll(rememberScrollState()).padding(horizontal = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Spacer(Modifier.height(24.dp))
        Text("MUSIC INTELLIGENCE", color = accent, fontSize = 18.sp,
            fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
        Text("IME nativo · decide() → DSP real (WFS · HRTF · EQ · dinámica · ambiente)",
            color = dim, fontSize = 10.sp)
        Spacer(Modifier.height(16.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Optimización activa (el IME controla EQ/espacial/dinámica)",
                color = Color.White, fontSize = 12.sp, modifier = Modifier.weight(1f))
            Switch(checked = enabled, onCheckedChange = {
                enabled = it; MusicIntelligenceWorker.setEnabled(ctx, it)
            })
        }
        Spacer(Modifier.height(16.dp))
        Text("Estilo: ${st.style}", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text("Confianza: ${(st.confidence * 100).toInt()}%", color = accent, fontSize = 14.sp)
        Spacer(Modifier.height(8.dp))
        Text("Bloques analizados: ${st.blocks} · decisión en ${st.adaptMs} ms",
            color = dim, fontSize = 11.sp)
        Spacer(Modifier.height(16.dp))
        Text("OPTIMIZACIÓN (objetivos del motor)", color = accent, fontSize = 11.sp, letterSpacing = 1.sp)
        Spacer(Modifier.height(8.dp))
        Text("WFS spread:  ${"%.2f".format(st.wfsSpread)}", color = Color.White, fontSize = 13.sp)
        Text("HRTF depth:  ${"%.2f".format(st.hrtfDepth)}", color = Color.White, fontSize = 13.sp)
        Text("EQ tilt:     ${"%+.1f".format(st.eqTiltDb)} dB", color = Color.White, fontSize = 13.sp)
        Text("Dinámica:    ${"%.2f".format(st.dynamicsAmount)} (1 = sin comprimir)", color = Color.White, fontSize = 13.sp)
        Text("Ambiente:    ${"%.2f".format(st.envDepth)}", color = Color.White, fontSize = 13.sp)
        if (st.lastAppliedAtMs > 0) {
            Spacer(Modifier.height(8.dp))
            Text("Última aplicación al DSP: ${(System.currentTimeMillis() - st.lastAppliedAtMs) / 1000}s atrás",
                color = dim, fontSize = 10.sp)
        }
        Spacer(Modifier.height(24.dp))
        TextButton(onClick = onBack) { Text("← VOLVER", color = accent) }
    }
}
