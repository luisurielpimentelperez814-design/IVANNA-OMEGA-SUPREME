package com.ivanna.omega.ui

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.*

/** User-declared Genezy MHCPZ1 layout; declared impedances are not measured curves. */
data class GenezySpeakerProfile(
    val frontLeftOhmsDeclared: Float = 8f,
    val frontRightOhmsDeclared: Float = 8f,
    val satelliteLeftOhmsDeclared: Float = 24f,
    val satelliteRightOhmsDeclared: Float = 24f,
    val bassAssemblyOhmsDeclared: Float = 6f,
    val frontDriversPerSide: Int = 2,
    val tweetersPerSide: Int = 2,
    val bass15InchCount: Int = 1,
    val bass4InchCount: Int = 2
) {
    companion object { val GENEZY_MHCPZ1 = GenezySpeakerProfile() }
}

enum class GenezyZone(val label: String, val azimuthDeg: Float) {
    FRONT_LEFT("Frontal L", -35f),
    FRONT_RIGHT("Frontal R", 35f),
    SIDE_LEFT("Satélite lateral L", -110f),
    SIDE_RIGHT("Satélite lateral R", 110f)
}

data class VirtualSpatialPosition(val index: Int, val azimuthDeg: Float, val elevationDeg: Float)
data class GenezyPositionRoute(
    val position: VirtualSpatialPosition,
    val primaryZone: GenezyZone,
    val secondaryZone: GenezyZone,
    val primaryWeight: Float,
    val secondaryWeight: Float
)

/**
 * Deterministic mapping proposal for 16 virtual azimuths. It is not itself an audio output
 * router; discrete-channel application requires runtime output-channel capability and DSP wiring.
 */
object GenezySpatialMapping {
    val positions: List<VirtualSpatialPosition> = (0 until 16).map { i ->
        VirtualSpatialPosition(i + 1, -180f + i * 22.5f, 0f)
    }

    fun route(position: VirtualSpatialPosition, lateralExtension: Boolean = true): GenezyPositionRoute {
        val zones = if (lateralExtension) GenezyZone.values().toList()
            else listOf(GenezyZone.FRONT_LEFT, GenezyZone.FRONT_RIGHT)
        fun distance(a: Float, b: Float): Float = kotlin.math.abs(((a - b + 540f) % 360f) - 180f)
        val nearest = zones.sortedBy { distance(position.azimuthDeg, it.azimuthDeg) }.take(2)
        val d0 = distance(position.azimuthDeg, nearest[0].azimuthDeg)
        val d1 = distance(position.azimuthDeg, nearest[1].azimuthDeg)
        val s0 = 1f / (d0 + 1f)
        val s1 = 1f / (d1 + 1f)
        return GenezyPositionRoute(position, nearest[0], nearest[1], s0 / (s0 + s1), s1 / (s0 + s1))
    }

    fun routeAll(lateralExtension: Boolean = true): List<GenezyPositionRoute> =
        positions.map { route(it, lateralExtension) }
}

@Composable
fun GenezyMhcpz1ProfileCard(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val prefs = remember(context) { context.getSharedPreferences("ivanna_genezy_profile", Context.MODE_PRIVATE) }
    var enabled by remember { mutableStateOf(prefs.getBoolean("enabled", false)) }
    var lateralExtension by remember { mutableStateOf(prefs.getBoolean("lateral_extension", true)) }
    LaunchedEffect(enabled, lateralExtension) {
        com.ivanna.omega.fotoholo.FotoHoloEngine.setGenezyProfile(context, enabled, lateralExtension)
    }
    val profile = GenezySpeakerProfile.GENEZY_MHCPZ1

    Column(
        modifier.fillMaxWidth().background(AuroraCyan.copy(alpha = 0.1f), RoundedCornerShape(14.dp)).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(7.dp)
    ) {
        Text("PERFIL FÍSICO · GENEZY MHCPZ1", color = AuroraCyan, fontSize = 13.sp, fontWeight = FontWeight.ExtraBold)
        Text("Salida prevista: estéreo AUX hacia el Genezy. Su amplificador distribuye la señal a 2 frontales + 2 satélites + subwoofer; IVANNA no finge cinco canales PCM discretos.", color = TextSecondary, fontSize = 11.sp)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Usar perfil como referencia espacial", color = TextPrimary, fontSize = 12.sp)
            Switch(checked = enabled, onCheckedChange = {
                enabled = it
                prefs.edit().putBoolean("enabled", it).apply()
            })
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Extensión lateral de satélites L/R", color = TextPrimary, fontSize = 12.sp)
            Switch(checked = lateralExtension, onCheckedChange = {
                lateralExtension = it
                prefs.edit().putBoolean("lateral_extension", it).apply()
            })
        }
        Text("Frontales: 2 bocinas + 2 tweeters por lado · 8 Ω declarados", color = TextSecondary, fontSize = 11.sp)
        Text("Satélites laterales L/R: 24 Ω declarados por lado", color = TextSecondary, fontSize = 11.sp)
        Text("Graves: 1×15″ + 2×4″ · 6 Ω declarados para el conjunto", color = TextSecondary, fontSize = 11.sp)
        Text(
            if (enabled) "Perfil solicitado: NanoTomix aplica un ajuste de anchura de hasta +8 % solo cuando está activo, sin bypass y con una ruta DSP disponible. La separación física de los cinco grupos la realiza el Genezy."
            else "Perfil apagado: se conserva el ancho base. El crossover y la distribución interna del Genezy permanecen bajo control de su amplificador.",
            color = if (enabled) AuroraCyan else TextSecondary, fontSize = 11.sp
        )
        if (enabled) Text(
            "Topología física declarada: FL · FR · SL · SR · SUB. El estado de aplicación se verifica en la telemetría FotoHolo; el interruptor por sí solo no demuestra efecto audible.",
            color = TextSecondary, fontSize = 10.sp
        )
    }
}
