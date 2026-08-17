package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.graphics.Color
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/** Fila de slider autocontenida — IvannaSliderRow de SoundScreen.kt es
 *  private, no accesible desde aquí; se replica el patrón visual mínimo
 *  en vez de widen-ear visibilidad de código ajeno sin necesidad real. */
@Composable
private fun ExciterSliderRow(
    label: String, value: Float, min: Float, max: Float, unit: String,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = Color.White.copy(alpha = 0.7f), fontSize = 12.sp)
            Text("${"%.2f".format(value)} $unit", color = Color(0xFFFF8A3D), fontSize = 12.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = min..max,
            colors = SliderDefaults.colors(thumbColor = Color(0xFFFF8A3D), activeTrackColor = Color(0xFFFF8A3D))
        )
    }
}

/**
 * HarmonicExciterPanel — control manual del excitador armónico real
 * (dsp/HarmonicExciter.cpp: saturación suave por tanh + generación de 2ª/3ª
 * armónica, mezclada de vuelta con el parámetro "wet") y de la intensidad
 * anti-compresión comercial ("anti_dolby" en OmegaDspState).
 *
 * FIX (auditoría 2026-08-17, inspirado en mockup de Google AI Studio subido
 * por Luis — panel "Golden Ear GAN"): el backend YA estaba completamente
 * cableado — OmegaEngineBridge.sendPerceptualState() empaqueta harmonicGain/
 * antiDolbyIntensity en el comando real SET_PERCEPTUAL_STATE que
 * command_server.cpp procesa (verificado línea por línea) — pero solo se
 * disparaba automáticamente desde PerceptualDecisionEngine (motor de IA) o
 * el comando inicial de calibración en IVANNAApplication.kt. No existía
 * ningún control manual en la UI real.
 *
 * Nombrado honesto a propósito: NO se llama "GAN" como el mockup — el DSP
 * real es saturación tanh + armónicos, no una red generativa adversaria.
 * Los otros 5 parámetros de SET_PERCEPTUAL_STATE (compressor/exciterRed/
 * highCut/spatialWidth/loudnessTarget) se mantienen en los mismos valores
 * calibrados que ya usa IVANNAApplication.kt en su comando inicial, para
 * que mover estos 2 sliders no corrompa los otros 5 en el daemon.
 */
@Composable
fun HarmonicExciterPanel(modifier: Modifier = Modifier) {
    // Defaults idénticos al comando de calibración inicial real en
    // IVANNAApplication.kt — no inventados, copiados de ahí a propósito.
    var harmonicGain by remember { mutableStateOf(0.78f) }
    var antiDolby by remember { mutableStateOf(0.85f) }
    val scope = rememberCoroutineScope()

    fun push(newHarmonicGain: Float = harmonicGain, newAntiDolby: Float = antiDolby) {
        scope.launch(Dispatchers.IO) {
            runCatching {
                OmegaEngineBridge.sendPerceptualState(
                    compressor = -5.5f,
                    exciterRed = 0.15f,
                    highCut = 19500f,
                    spatialWidth = 1.55f,
                    loudnessTarget = -16.0f,
                    harmonicGain = newHarmonicGain,
                    antiDolby = newAntiDolby
                )
            }
        }
    }

    GlassCard(
        title = "EXCITADOR ARMÓNICO",
        accent = Color(0xFFFF8A3D),
        subtitle = "Sistema (daemon) · Saturación tanh + 2ª/3ª armónica · dsp/HarmonicExciter.cpp"
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            ExciterSliderRow("GANANCIA ARMÓNICA", harmonicGain, 0f, 1.5f, "x") { v ->
                harmonicGain = v
                push(newHarmonicGain = v)
            }
            ExciterSliderRow("ANTI-COMPRESIÓN COMERCIAL", antiDolby, 0f, 1f, "x") { v ->
                antiDolby = v
                push(newAntiDolby = v)
            }
            Text(
                "Neutraliza perfiles de compresión OEM agresivos (Dolby/Xiaomi HyperOS/" +
                    "OneUI) sin desactivarlos — no reemplaza el post-proc del fabricante, " +
                    "compite por prioridad en la cadena de efectos.",
                color = Color.White.copy(alpha = 0.5f),
                fontSize = 11.sp
            )
        }
    }
}
