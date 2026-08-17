package com.ivanna.omega.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.Iso226Calibrator
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay

/**
 * Iso226StatusPanel — estado REAL del calibrador de sonoridad ISO 226:2003.
 *
 * FIX (auditoría 2026-08-12): antes de este panel, la única mención de
 * "ISO 226" en toda la UI real (no el mockup React de src/, que no se
 * shippea) era un string decorativo en EQTab — "8 bandas · Q adaptativo ·
 * ISO 226" — sin ningún dato en vivo detrás. Iso226Calibrator.kt sí
 * calculaba listenPhon/refPhon/isCalibrated/lastGainsDsp y los aplicaba al
 * DSP correctamente (IvannaGlobalEffectManager, OmegaEngineBridge), pero
 * nada en Compose los leía — el usuario no tenía forma de ver si estaba
 * calibrado, a qué nivel, o qué curva de compensación se estaba aplicando.
 *
 * Iso226Calibrator expone su estado como `@Volatile var ... private set`,
 * no StateFlow — se pollea cada 500ms (mismo patrón ya usado en
 * MagiskStatusPanel.kt, que pollea el daemon cada 2s) en vez de refactorizar
 * el calibrador a StateFlow, para minimizar el blast radius del cambio.
 */
@Composable
fun Iso226StatusPanel(modifier: Modifier = Modifier) {
    var isCalibrated by remember { mutableStateOf(Iso226Calibrator.isCalibrated) }
    var listenPhon by remember { mutableStateOf(Iso226Calibrator.listenPhon) }
    var refPhon by remember { mutableStateOf(Iso226Calibrator.refPhon) }
    var gains by remember { mutableStateOf(Iso226Calibrator.lastGainsDsp.copyOf()) }

    LaunchedEffect(Unit) {
        while (true) {
            isCalibrated = Iso226Calibrator.isCalibrated
            listenPhon = Iso226Calibrator.listenPhon
            refPhon = Iso226Calibrator.refPhon
            gains = Iso226Calibrator.lastGainsDsp.copyOf()
            delay(500L)
        }
    }

    GlassCard(
        title = "SONORIDAD ISO 226:2003",
        accent = if (isCalibrated) AuroraCyan else Color(0xFF808080),
        subtitle = Iso226Calibrator.describe()
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Text("Nivel de escucha", color = Color.White.copy(alpha = 0.6f), fontSize = 11.sp)
                Text("${listenPhon.toInt()} phon", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            }
            Column(horizontalAlignment = Alignment.End) {
                Text("Nivel de referencia", color = Color.White.copy(alpha = 0.6f), fontSize = 11.sp)
                Text("${refPhon.toInt()} phon", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(Modifier.height(10.dp))
        Text(
            "Curva de compensación (10 bandas, dB)",
            color = Color.White.copy(alpha = 0.6f),
            fontSize = 11.sp
        )
        Spacer(Modifier.height(4.dp))
        Iso226GainCurve(gains = gains, modifier = Modifier.fillMaxWidth().height(48.dp))
    }
}

/** Curva de barras simple para las 10 ganancias de compensación ISO 226. */
@Composable
private fun Iso226GainCurve(gains: FloatArray, modifier: Modifier = Modifier) {
    Canvas(modifier = modifier) {
        if (gains.isEmpty()) return@Canvas
        val bandWidth = size.width / gains.size
        val centerY = size.height / 2f
        val maxDb = 12f // clampDbForEq range en Iso226Calibrator es [-12, 12]
        gains.forEachIndexed { i, db ->
            val barHeight = (db.coerceIn(-maxDb, maxDb) / maxDb) * centerY
            val x = i * bandWidth + bandWidth * 0.15f
            val barW = bandWidth * 0.7f
            drawRect(
                color = if (db >= 0f) AuroraCyan else Color(0xFFFF6B6B),
                topLeft = Offset(x, if (barHeight >= 0f) centerY - barHeight else centerY),
                size = androidx.compose.ui.geometry.Size(barW, kotlin.math.abs(barHeight))
            )
        }
    }
}
