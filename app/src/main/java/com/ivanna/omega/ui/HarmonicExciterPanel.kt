package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

@Composable
private fun ExciterSliderRow(
    label: String, value: Float, min: Float, max: Float, unit: String,
    enabled: Boolean = true,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label,
                color = if (enabled) Color.White.copy(alpha = 0.7f) else Color.White.copy(alpha = 0.3f),
                fontSize = 12.sp)
            Text("${"%.2f".format(value)} $unit",
                color = if (enabled) Color(0xFFFF8A3D) else Color(0xFFFF8A3D).copy(alpha = 0.3f),
                fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = min..max,
            enabled = enabled,
            colors = SliderDefaults.colors(
                thumbColor = Color(0xFFFF8A3D),
                activeTrackColor = Color(0xFFFF8A3D),
                disabledThumbColor = Color(0xFFFF8A3D).copy(alpha = 0.3f),
                disabledActiveTrackColor = Color(0xFFFF8A3D).copy(alpha = 0.3f)
            )
        )
    }
}

/**
 * HarmonicExciterPanel v2
 *
 * FIXES:
 *   - default harmonicGain 0.78->0.0: el valor original causaba saturacion
 *     tanh al 78% en cada instalacion nueva sin que el usuario lo supiera.
 *   - Se agrega toggle BYPASS explicito: cuando esta OFF envia harmonicGain=0
 *     al daemon, eliminando por completo las armonicas 2/3.
 *   - Persistencia via SharedPreferences "harmonic_exciter_prefs" (ya existia
 *     en v15, se mantiene la misma clave para no perder config guardada).
 */
@Composable
fun HarmonicExciterPanel(modifier: Modifier = Modifier) {
    val ctx   = LocalContext.current
    val prefs = remember {
        ctx.getSharedPreferences("harmonic_exciter_prefs", android.content.Context.MODE_PRIVATE)
    }

    // FIX: default 0.0f — sin armonicas hasta que el usuario las active
    var exciterEnabled by remember { mutableStateOf(prefs.getBoolean("exciterEnabled", false)) }
    var harmonicGain   by remember { mutableStateOf(prefs.getFloat("harmonicGain", 0.0f)) }
    var antiDolby      by remember { mutableStateOf(prefs.getFloat("antiDolby",    0.0f)) }
    val scope = rememberCoroutineScope()

    fun save() = prefs.edit()
        .putBoolean("exciterEnabled", exciterEnabled)
        .putFloat("harmonicGain",    harmonicGain)
        .putFloat("antiDolby",       antiDolby)
        .apply()

    fun push(hg: Float = harmonicGain, ad: Float = antiDolby, en: Boolean = exciterEnabled) {
        scope.launch(Dispatchers.IO) {
            runCatching {
                OmegaEngineBridge.sendPerceptualState(
                    compressor     = -5.5f,
                    exciterRed     = 0.15f,
                    highCut        = 19500f,
                    spatialWidth   = 1.55f,
                    loudnessTarget = -16.0f,
                    // Si bypass -> 0f real al daemon
                    harmonicGain   = if (en) hg else 0f,
                    antiDolby      = ad
                )
            }
        }
        save()
    }

    GlassCard(
        title    = "EXCITADOR ARMÓNICO",
        accent   = Color(0xFFFF8A3D),
        subtitle = "Saturación tanh + 2ª/3ª armónica · HarmonicExciter.cpp"
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    if (exciterEnabled) "EXCITADOR ACTIVO" else "BYPASS (sin armonicas)",
                    color = if (exciterEnabled) Color(0xFFFF8A3D) else Color.White.copy(alpha = 0.4f),
                    fontSize = 12.sp, fontWeight = FontWeight.Bold
                )
                Switch(
                    checked = exciterEnabled,
                    onCheckedChange = { en ->
                        exciterEnabled = en
                        push(en = en)
                    }
                )
            }

            ExciterSliderRow(
                "GANANCIA ARMÓNICA", harmonicGain, 0f, 1.5f, "x",
                enabled = exciterEnabled
            ) { v -> harmonicGain = v; push(hg = v) }

            ExciterSliderRow(
                "ANTI-COMPRESIÓN OEM", antiDolby, 0f, 1f, "x"
            ) { v -> antiDolby = v; push(ad = v) }

            Text(
                "BYPASS desactiva completamente la saturacion armonica. " +
                "Con excitador ON: 2ª y 3ª armonicas por tanh. " +
                "antiDolby independiente del bypass.",
                color = Color.White.copy(alpha = 0.4f),
                fontSize = 10.sp
            )
        }
    }
}
