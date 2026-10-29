package com.ivanna.omega.ui

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.fotoholo.FotoHoloEngine
import com.ivanna.omega.fotoholo.SpeakerLayout
import com.ivanna.omega.fotoholo.SpeakerLayoutPolicy
import com.ivanna.omega.fotoholo.SpeakerMode
import com.ivanna.omega.spatial.WfsCalibrationManager

/**
 * Declaración del usuario: cuántas bocinas satélite tiene conectadas, en qué modo (estéreo o
 * surround) y si hay subwoofer activo. Al entrar en Surround vuelve a publicar el layout WFS 7ch
 * de referencia relativo a la posición del oyente persistida, usando la ruta nativa/daemon. El conteo
 * también modula el ancho perceptual; NO inventa coordenadas por satélite ni crea salidas físicas
 * discretas. La ruta WFS debe estar habilitada para que su geometría afecte el renderizado.
 */
@Composable
fun SpeakerLayoutPanel(modifier: Modifier = Modifier) {
    val context: Context = LocalContext.current
    var lineOut by remember { mutableStateOf(SpatialAudioPrefs.load(context).externalStereoLineOut) }
    var routeStatus by remember { mutableStateOf("Perfil solicitado; la ruta física debe comprobarse reproduciendo audio.") }
    var mode by remember { mutableStateOf(FotoHoloEngine.speakerLayout.mode) }
    var satellites by remember { mutableIntStateOf(FotoHoloEngine.speakerLayout.satelliteCount) }
    var subwoofer by remember { mutableStateOf(FotoHoloEngine.speakerLayout.subwooferActive) }

    LaunchedEffect(mode, satellites, subwoofer) {
        FotoHoloEngine.setSpeakerLayout(context, SpeakerLayout(mode, satellites, subwoofer))
    }
    LaunchedEffect(mode, lineOut) {
        // Reutiliza el layout WFS 7ch de referencia y la posición del oyente persistida; no
        // genera coordenadas ficticias a partir del simple conteo de satélites.
        if (mode == SpeakerMode.SURROUND && !lineOut) {
            val published = WfsCalibrationManager.restore(context)
            if (!published) {
                android.util.Log.w("SpeakerLayoutPanel", "Layout WFS no publicado; se conserva la ruta de audio previa")
            }
        }
    }
    val layout = SpeakerLayout(mode, satellites, subwoofer)
    val factor = SpeakerLayoutPolicy.widthFactor(layout)

    Column(
        modifier.fillMaxWidth().background(Color(0x1A6FF3FF), RoundedCornerShape(14.dp)).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(7.dp)
    ) {
        Text("DESTINO DEL DAC / AUXILIAR", color = Color.White, fontWeight = FontWeight.Bold)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("DAC → Sony MHC-PZ1D / equipo estéreo", modifier = Modifier.weight(1f), color = Color.White)
            Switch(checked = lineOut, onCheckedChange = { selected ->
                lineOut = selected
                SpatialAudioPrefs.save(context, SpatialAudioPrefs.load(context).copy(externalStereoLineOut = selected))
                val route = com.ivanna.omega.audio.AudioRouteManager.detectOutputRoute()
                com.ivanna.omega.audio.RouteDspCalibrator.onRouteChanged(context, route)
                routeStatus = "Perfil solicitado para ${route.name}; no certifica salida acústica ni efecto global."
            })
        }
        Text("Moto G85 → DAC USB-C → VIDEO/SAT AUDIO IN L/R del Sony MHC-PZ1D. Son dos canales. " +
            "El manual indica 2 frontales + 2 satélites que duplican el sonido frontal + subwoofer. " +
            "SOUND FIELD selecciona 2CH STEREO o 2CH SURR; SUBWOOFER es un botón físico. " +
            "Estos controles del Sony no se accionan desde esta pantalla. " +
            "YouTube/Spotify requieren verificar la ruta global Magisk; la captura Android depende de los permisos de cada aplicación.",
            color = Color(0xFFB5C2D4), fontSize = 12.sp)
        Text(routeStatus, color = Color(0xFFB5C2D4), fontSize = 11.sp)
        Text("BOCINAS DECLARADAS", color = Color(0xFF65E7F2), fontSize = 13.sp, fontWeight = FontWeight.ExtraBold)
        Text(
            (if (lineOut) "Auxiliar estéreo: no se publica una disposición WFS multicanal. El conteo solo matiza "
             else "Surround reutiliza las coordenadas WFS de referencia y las publica al motor. El conteo solo matiza ") +
            "el ancho perceptual (±15 % máx.); no crea canales físicos ni gestiona graves/LFE reales.",
            color = Color(0xFF9AA4B2), fontSize = 11.sp
        )
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ModeButton("Estéreo (2.0)", mode == SpeakerMode.STEREO) { mode = SpeakerMode.STEREO; if (satellites < 2) satellites = 2 }
            ModeButton("Surround", mode == SpeakerMode.SURROUND) { mode = SpeakerMode.SURROUND; if (satellites < 4) satellites = 4 }
        }
        if (mode == SpeakerMode.SURROUND) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Bocinas sin subwoofer: $satellites", color = Color.White, fontSize = 12.sp)
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Button(onClick = { satellites = (satellites - 1).coerceAtLeast(2) },
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF26323E))) { Text("−") }
                    Button(onClick = { satellites = (satellites + 1).coerceAtMost(8) },
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF26323E))) { Text("+") }
                }
            }
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Subwoofer activado en el equipo", color = Color.White, fontSize = 12.sp)
            Switch(checked = subwoofer, onCheckedChange = { subwoofer = it })
        }
        Text(
            "Ancho resultante ×%.2f%s".format(
                factor,
                if (factor == 1.0f) " (sin cambio: estéreo sin subwoofer)" else ""
            ),
            color = Color(0xFF9AA4B2), fontSize = 11.sp
        )
    }
}

@Composable
private fun ModeButton(label: String, selected: Boolean, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        colors = ButtonDefaults.buttonColors(
            containerColor = if (selected) Color(0xFF1C6E78) else Color(0xFF1A2430)
        )
    ) { Text(label, fontSize = 12.sp) }
}
