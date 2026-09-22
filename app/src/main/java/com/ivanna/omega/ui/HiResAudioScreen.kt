package com.ivanna.omega.ui
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.HiResAudioManager
import com.ivanna.omega.ui.theme.AmberSignal
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.ObsidianVoid
import com.ivanna.omega.ui.theme.TextPrimary
import com.ivanna.omega.ui.theme.TextSecondary

/**
 * Panel Audio Hi-Res: selectores DESPLEGABLES de sample rate y profundidad.
 *
 * CAUSAS REALES de "el boton no despliega nada" (2026-09-21):
 *  1. ControlTabScreen (la pantalla viva) nunca pasaba onOpenHiRes a
 *     IvannaControlPanel -> el boton AUDIO HI-RES era un no-op.
 *  2. Esta pantalla dibujaba Text() sin Surface ni color: fuera de un Surface el
 *     color de contenido por defecto de Compose es NEGRO, sobre fondo oscuro ->
 *     los rotulos eran invisibles y solo se veian los circulos de los RadioButton.
 *  3. apply() devolvia siempre false sin root, asi que nunca habia confirmacion.
 * Ahora: fondo y colores explicitos, dos desplegables, y el estado mostrado es el
 * resultado REAL devuelto por HiResAudioManager (no una constante).
 */
@Composable
fun HiResAudioScreen(onBack: () -> Unit, onOpenMusicIntel: () -> Unit = {} = {}) {
    val context = LocalContext.current
    remember { HiResAudioManager.loadPersisted(context) }
    var rate by remember { mutableStateOf(HiResAudioManager.currentRate) }
    var depth by remember { mutableStateOf(HiResAudioManager.currentDepth) }
    var status by remember { mutableStateOf("") }
    val (routeName, routeMax) = remember { HiResAudioManager.activeRoute(context) }

    Column(
        Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text(
            "◄ VOLVER", color = AuroraCyan, fontWeight = FontWeight.Bold,
            modifier = Modifier.clickable { onBack() }.padding(vertical = 8.dp)
        )
        Text("AUDIO HI-RES", color = TextPrimary, fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Text("Ruta activa: " + routeName + " — max real: " + khzLabel(routeMax) + " kHz", color = TextSecondary)

        HiResDropdown(
            label = "Sample rate",
            valueText = khzLabel(rate) + " kHz",
            options = HiResAudioManager.VALID_RATES.map { it to (khzLabel(it) + " kHz" + if (it <= routeMax) "" else "  (requiere USB-DAC)") },
            onSelect = { r ->
                rate = r
                status = "Guardando…"
                HiResAudioManager.apply(context, r, depth) { status = it }
            }
        )
        HiResDropdown(
            label = "Profundidad",
            valueText = depth.toString() + " bits",
            options = HiResAudioManager.VALID_DEPTHS.map { it to (it.toString() + " bits") },
            onSelect = { d ->
                depth = d
                status = "Guardando…"
                HiResAudioManager.apply(context, rate, d) { status = it }
            }
        )

        Text(
            "Seleccionado: " + khzLabel(rate) + " kHz / " + depth + " bits",
            color = TextPrimary, fontWeight = FontWeight.Bold
        )
        if (status.isNotEmpty()) Text(status, color = TextSecondary)
        if (rate > routeMax) {
            Text(
                "La ruta actual limita a " + khzLabel(routeMax) + " kHz: la seleccion queda guardada y solo tendra efecto audible con una ruta que la soporte (USB-DAC).",
                color = AmberSignal
            )
        }
        Text(
            "384 kHz/32-bit bit-perfect solo por USB-DAC (via directa). Bluetooth limitado por el codec A2DP; bocina/cable pasan por el mixer de Android. " +
            androidx.compose.material3.TextButton(onClick = onOpenMusicIntel) { Text("MUSIC INTELLIGENCE →") }
                "La captura por software (Ruta A, sin Magisk) procesa a 48 kHz fijos; el selector gobierna el daemon/ruta de sistema.",
            color = TextSecondary
        )
    }
}

@Composable
private fun HiResDropdown(
    label: String,
    valueText: String,
    options: List<Pair<Int, String>>,
    onSelect: (Int) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, color = TextSecondary)
        Box {
            OutlinedButton(
                onClick = { expanded = true },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
            ) { Text(valueText + "  ▾") }
            DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                options.forEach { (value, text) ->
                    DropdownMenuItem(
                        text = { Text(text) },
                        onClick = { expanded = false; onSelect(value) }
                    )
                }
            }
        }
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
