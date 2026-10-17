package com.ivanna.omega.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.fotoholo.FotoHoloEngine
import com.ivanna.omega.fotoholo.FotoHoloMath
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.ObsidianDeep

/**
 * FOTOHOLOGRAFÍA · NANOTOMIX — control + escena paramétrica.
 *
 * Qué es: (1) una escena 3D proyectada dibujada con las 64 bandas Bark REALES de la captura y
 * (2) una modulación acotada del ancho estéreo que llega al StereoWidener por el mismo camino que el
 * slider de ancho. Qué NO es: un holograma óptico (una pantalla convencional no lo genera).
 * Necesita señal de la captura de audio (Ruta A); sin ella la escena queda plana y el ancho no se toca.
 */
@Composable
fun FotoHoloCard(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val tel by FotoHoloEngine.telemetry.collectAsState()
    var enabled by remember { mutableStateOf(FotoHoloEngine.enabled) }
    var intensity by remember { mutableStateOf(FotoHoloEngine.intensity) }
    var bypass by remember { mutableStateOf(FotoHoloEngine.bypass) }
    var rotation by remember { mutableStateOf(0f) }

    // Mientras la tarjeta está visible el motor muestrea el espectro aunque el efecto esté apagado.
    DisposableEffect(Unit) {
        FotoHoloEngine.viewerAttached()
        onDispose { FotoHoloEngine.viewerDetached() }
    }
    // Rotación sincronizada con el refresco real de pantalla (no un delay fijo).
    LaunchedEffect(Unit) {
        while (true) {
            androidx.compose.runtime.withFrameNanos { t -> rotation = (t / 1_000_000_000.0).toFloat() * 0.35f }
        }
    }

    Column(
        modifier
            .fillMaxWidth()
            .background(Color(0x1A6FF3FF), RoundedCornerShape(14.dp))
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("FOTOHOLOGRAFÍA · NANOTOMIX", color = AuroraCyan, fontSize = 13.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
        Text(
            "Escena 3D paramétrica de las 64 bandas Bark + modulación acotada del ancho estéreo. No es un holograma óptico.",
            color = Color(0xFF9AA4B2), fontSize = 11.sp
        )

        Canvas(Modifier.fillMaxWidth().height(170.dp).background(ObsidianDeep, RoundedCornerShape(10.dp))) {
            val cx = size.width / 2f
            val cy = size.height / 2f
            val half = minOf(size.width, size.height) * 0.48f
            val pts = Array(FotoHoloMath.BANDS) { i ->
                val p = FotoHoloMath.holoPoint(i, tel.bands.getOrElse(i) { 0f }, rotation)
                floatArrayOf(cx + p[0] * half * (size.width / size.height).coerceIn(1f, 2.2f) * 0.55f, cy + p[1] * half, p[2])
            }
            for (i in pts.indices) {
                val a = pts[i]
                val b = pts[(i + 1) % pts.size]
                val alpha = (0.25f + (a[2] - 0.6f) * 1.2f).coerceIn(0.15f, 0.95f)
                drawLine(AuroraCyan.copy(alpha = alpha), Offset(a[0], a[1]), Offset(b[0], b[1]), strokeWidth = 1.5f)
                val v = tel.bands.getOrElse(i) { 0f }.coerceIn(0f, 1f)
                drawCircle(AuroraCyan.copy(alpha = alpha), radius = 1.5f + 3.5f * v, center = Offset(a[0], a[1]))
            }
        }

        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Modular el ancho estéreo", color = Color.White, fontSize = 12.sp)
            Switch(checked = enabled, onCheckedChange = { enabled = it; FotoHoloEngine.setEnabled(context, it) })
        }
        Text("Intensidad: ${(intensity * 100).toInt()} %  (±${(FotoHoloMath.MAX_OFFSET * intensity * 100).toInt()} % de ancho como máximo)", color = Color(0xFF9AA4B2), fontSize = 11.sp)
        Slider(
            value = intensity,
            onValueChange = { intensity = it; FotoHoloEngine.setIntensity(context, it) },
            valueRange = 0f..1f
        )
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Bypass A/B (oír sin modular)", color = Color.White, fontSize = 12.sp)
            Switch(checked = bypass, onCheckedChange = { bypass = it; FotoHoloEngine.setBypass(it) })
        }

        val status = when {
            !tel.hasSignal -> "Sin señal: activa la captura de audio (Ruta A). Mientras tanto el ancho no se toca."
            !enabled -> "Espectro en vivo · modulación apagada."
            bypass -> "Bypass: suena el ancho que fijaste."
            else -> "Modulando · agudos abren, graves cierran."
        }
        Text(status, color = AuroraCyan, fontSize = 11.sp)
        Text(
            "Ancho tuyo %.2f → aplicado %.2f (Δ %+.3f) · graves %d %% · agudos %d %%".format(
                tel.baseWidth, tel.appliedWidth, tel.offset,
                (tel.features.lowShare * 100).toInt(), (tel.features.highShare * 100).toInt()
            ),
            color = Color(0xFF9AA4B2), fontSize = 11.sp
        )
    }
}
