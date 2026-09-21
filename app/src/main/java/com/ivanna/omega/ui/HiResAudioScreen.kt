package com.ivanna.omega.ui
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.HiResAudioManager
@Composable
fun HiResAudioScreen(onBack: () -> Unit = {}) {
    val context = LocalContext.current
    // FIX (botón que "no funciona"): el estado inicial se lee SIEMPRE de lo
    // persistido (no solo del @Volatile en memoria, que arranca en el default
    // 48k/24b cada vez que el proceso muere). Así la pantalla refleja tu
    // selección real aunque la app se haya reiniciado.
    HiResAudioManager.restore(context)
    var rate by remember { mutableStateOf(HiResAudioManager.currentRate) }
    var depth by remember { mutableStateOf(HiResAudioManager.currentDepth) }
    val (routeName, routeMax) = remember { HiResAudioManager.activeRoute(context) }
    var applied by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("◄ VOLVER", fontWeight = FontWeight.Bold, modifier = Modifier.clickable { onBack() })
        Text("AUDIO HI-RES")
        Text("Ruta activa: " + routeName + " — max real: " + khzLabel(routeMax) + " kHz")
        Text("Sample rate")
        HiResAudioManager.VALID_RATES.forEach { r ->
            // FIX REAL (botón "no despliega nada"): el gateo `r <= routeMax`
            // deshabilitaba TODAS las opciones Hi-Res cuando la ruta activa
            // reportaba un tope bajo (bocina/BT/mixer = 48 kHz) -> el panel
            // parecía no responder. Ahora TODA opción es siempre seleccionable
            // y persiste; si supera el tope de la ruta actual solo se avisa
            // (se aplicará al conectar una ruta que lo soporte, p.ej. USB-DAC).
            val soportada = r <= routeMax
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                        rate = r
                        applied = HiResAudioManager.apply(context, r, depth)
                    }
                    .padding(vertical = 4.dp)
            ) {
                RadioButton(selected = rate == r, onClick = null /* la fila maneja el click */)
                Text(khzLabel(r) + " kHz" + if (soportada) "" else " (se aplica en ruta USB-DAC)")
            }
        }
        Text("Profundidad")
        HiResAudioManager.VALID_DEPTHS.forEach { d ->
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                        depth = d
                        applied = HiResAudioManager.apply(context, rate, d)
                    }
                    .padding(vertical = 4.dp)
            ) {
                RadioButton(selected = depth == d, onClick = null)
                Text(d.toString() + " bits")
            }
        }
        // Confirmación SIEMPRE visible con los valores activos — el usuario
        // ve de inmediato qué quedó seleccionado, no un mensaje condicional.
        Text(
            "Activo: " + khzLabel(HiResAudioManager.currentRate) + " kHz / " +
                HiResAudioManager.currentDepth + " bits" +
                if (applied) " — aplicado y persistido (el daemon lo recoge al reiniciar)" else "",
            fontWeight = FontWeight.Bold
        )
        Text("384 kHz/32-bit bit-perfect solo por USB-DAC (via directa). Bluetooth limitado por el codec A2DP; bocina/cable pasan por el mixer de Android.")
    }
}

/** Etiqueta de sample rate sin truncar: 44100 -> "44.1", 48000 -> "48".
 *  (r/1000) enteros mostraba "44 kHz" y "176 kHz" para 44100/176400 — cifras
 *  que no existen. Se corrige al anadir la familia de 44.1 kHz al panel. */
private fun khzLabel(rate: Int): String {
    val whole = rate / 1000
    val frac  = (rate % 1000) / 100
    return if (frac == 0) whole.toString() else "$whole.$frac"
}
