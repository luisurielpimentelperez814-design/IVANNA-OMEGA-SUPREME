package com.ivanna.omega.ui
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.HiResAudioManager
@Composable
fun HiResAudioScreen() {
    val context = LocalContext.current
    var rate by remember { mutableStateOf(HiResAudioManager.currentRate) }
    var depth by remember { mutableStateOf(HiResAudioManager.currentDepth) }
    val (routeName, routeMax) = remember { HiResAudioManager.activeRoute(context) }
    var applied by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("AUDIO HI-RES")
        Text("Ruta activa: " + routeName + " — max real: " + (routeMax/1000) + " kHz")
        Text("Sample rate")
        HiResAudioManager.VALID_RATES.forEach { r ->
            val enabled = r <= routeMax
            Row(verticalAlignment = Alignment.CenterVertically) {
                RadioButton(selected = rate == r, onClick = if (enabled) ({ rate = r; applied = HiResAudioManager.apply(context, r, depth) }) else null)
                Text((r/1000).toString() + " kHz" + if (enabled) "" else " (no soportado por la ruta)")
            }
        }
        Text("Profundidad")
        HiResAudioManager.VALID_DEPTHS.forEach { d ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                RadioButton(selected = depth == d, onClick = { depth = d; applied = HiResAudioManager.apply(context, rate, d) })
                Text(d.toString() + " bits")
            }
        }
        if (applied) Text("Aplicado y persistido — el daemon lo recoge al (re)iniciar.")
        Text("384 kHz/32-bit bit-perfect solo por USB-DAC (via directa). Bluetooth limitado por el codec A2DP; bocina/cable pasan por el mixer de Android.")
    }
}
