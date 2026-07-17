package com.ivanna.omega.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.*
import kotlin.math.log10
import kotlin.math.max

/**
 * AdaptiveDashboard — pantalla de telemetría real del AdaptiveDecisionEngine.
 *
 * Fuente única de datos: IvannaNativeLib.nativeGetAdaptiveTelemetry() → FloatArray[10]
 *   [0] rms          (lineal 0..1)
 *   [1] peak         (lineal 0..1)
 *   [2] gainReductionDb
 *   [3] targetGain   (0.5..1.0)
 *   [4] compAmount   (0..1)
 *   [5] exciterReduction (0..1)
 *   [6] spatialWidth (0.8..1.5)
 *   [7] safetyMargin (dB)
 *   [8] voiceProtect (0..1)
 *   [9] applied      (contador)
 *
 * Regla anti-control-fantasma: no hay sliders ni toggles aquí porque la
 * AdaptiveDecisionEngine no expone API de override manual. Solo visualización.
 */
@Composable
fun AdaptiveDashboard(
    telemetry: FloatArray?,        // null = ADE no corriendo o JNI no cargado
    bandEnergies: FloatArray? = null,  // FloatArray[3]: [low, mid, high] lineal RMS
    modifier: Modifier = Modifier
) {
    val t = telemetry

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // ── Header ───────────────────────────────────────────────────────────
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            PulseDot(active = t != null && (t.getOrElse(9) { 0f } > 0f))
            Text(
                "ADAPTIVE ENGINE",
                color = AuroraCyan,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 13.sp,
                letterSpacing = 2.sp
            )
            Spacer(Modifier.weight(1f))
            val appliedCount = t?.getOrElse(9) { 0f }?.toInt() ?: 0
            StatusPill(
                text = if (t == null) "OFFLINE" else if (appliedCount > 0) "ACTIVO" else "EN ESPERA",
                accent = if (t != null && appliedCount > 0) AuroraCyan else NeonMagenta
            )
        }

        // ── Signal meters ─────────────────────────────────────────────────
        GlassCard(title = "SEÑAL", accent = AuroraCyan.copy(alpha = 0.25f)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {

                val rmsDb  = if (t != null) linToDb(t[0]) else -60f
                val peakDb = if (t != null) linToDb(t[1]) else -60f
                val grDb   = t?.getOrElse(2) { 0f } ?: 0f

                MeterRow("RMS",  rmsDb,  -60f, 0f, AuroraCyan,    "${rmsDb.format(1)} dBFS")
                MeterRow("PEAK", peakDb, -60f, 0f, AmberSignal,   "${peakDb.format(1)} dBFS")
                if (grDb > 0.05f)
                    MeterRow("LIMITER", -grDb, -6f, 0f, NeonMagenta,
                             "−${grDb.format(1)} dB")
            }
        }

        // ── ADE decisions ─────────────────────────────────────────────────
        GlassCard(title = "DECISIONES ADE", accent = NeonMagenta.copy(alpha = 0.20f)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                val tg  = t?.getOrElse(3) { 1f }  ?: 1f
                val ca  = t?.getOrElse(4) { 0f }  ?: 0f
                val er  = t?.getOrElse(5) { 0f }  ?: 0f
                val sw  = t?.getOrElse(6) { 1f }  ?: 1f
                val sm  = t?.getOrElse(7) { 6f }  ?: 6f
                val vp  = t?.getOrElse(8) { 0f }  ?: 0f

                DecisionRow("target_gain",       tg,  0.5f, 1.0f, AuroraCyan,  "${(tg*100).toInt()}%")
                DecisionRow("comp_amount",        ca,  0f,   1.0f, AmberSignal, "${(ca*100).toInt()}%")
                DecisionRow("exciter_reduction",  er,  0f,   1.0f, NeonMagenta, "${(er*100).toInt()}%")
                DecisionRow("spatial_width",      sw,  0.8f, 1.5f, AuroraCyan,
                            "×${sw.format(2)}", normalizedValue = (sw - 0.8f) / 0.7f)
                DecisionRow("voice_protect",      vp,  0f,   1.0f, AmberSignal, "${(vp*100).toInt()}%")

                Row(
                    modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("safety_margin", color = Color.White.copy(0.4f),
                         fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                    Text("${sm.format(1)} dB", color = if (sm < 3f) NeonMagenta else AuroraCyan,
                         fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                }
            }
        }

        // ── Band energy (read-only, ADE inputs) ───────────────────────────
        // Band energy — datos reales del ADE (IIR bandpass en PDEngine + omega_daemon)
        // Ruta A: BiquadEnvelopeBank de PDEngine (bandas 0-1 low, 2-4 mid, 5-7 high)
        // Ruta B: 3 filtros IIR en omega_daemon::processLoop (ai_band_low/mid/high)
        // Backend real: nativeGetBandEnergies() → FloatArray[3]
        GlassCard(title = "ENERGÍA ESPECTRAL", accent = ObsidianSoft.copy(alpha = 0.5f)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                val bandLow  = bandEnergies?.getOrElse(0) { 0f } ?: 0f
                val bandMid  = bandEnergies?.getOrElse(1) { 0f } ?: 0f
                val bandHigh = bandEnergies?.getOrElse(2) { 0f } ?: 0f
                val total    = (bandLow + bandMid + bandHigh).coerceAtLeast(1e-6f)

                MeterRow("LOW",  bandLow,  0f, total.coerceAtLeast(0.01f), AmberSignal,
                         "${(bandLow/total*100).toInt()}%")
                MeterRow("MID",  bandMid,  0f, total.coerceAtLeast(0.01f), AuroraCyan,
                         "${(bandMid/total*100).toInt()}%")
                MeterRow("HIGH", bandHigh, 0f, total.coerceAtLeast(0.01f), NeonMagenta,
                         "${(bandHigh/total*100).toInt()}%")

                if (bandEnergies == null) {
                    Text("Sin señal de audio activa",
                         color = Color.White.copy(0.3f),
                         fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                }
            }
        }

        // ── applied counter ───────────────────────────────────────────────
        val applied = t?.getOrElse(9) { 0f }?.toInt() ?: 0
        if (applied > 0) {
            Text(
                "Decisiones aplicadas: $applied",
                color = AuroraCyan.copy(0.5f),
                fontFamily = FontFamily.Monospace,
                fontSize = 10.sp,
                modifier = Modifier.fillMaxWidth(),
                textAlign = TextAlign.Center
            )
        }
    }
}

// ── Helpers internos ──────────────────────────────────────────────────────────

@Composable
private fun PulseDot(active: Boolean) {
    val inf = rememberInfiniteTransition(label = "pulse")
    val alpha by inf.animateFloat(
        initialValue = if (active) 0.4f else 0.15f,
        targetValue  = if (active) 1.0f else 0.25f,
        animationSpec = infiniteRepeatable(tween(900), RepeatMode.Reverse),
        label = "dot"
    )
    Box(
        modifier = Modifier
            .size(8.dp)
            .clip(CircleShape)
            .background(if (active) AuroraCyan.copy(alpha) else NeonMagenta.copy(alpha))
    )
}

@Composable
private fun MeterRow(
    label: String,
    value: Float,   // dB value
    min: Float,
    max: Float,
    accent: Color,
    display: String
) {
    val fraction = ((value - min) / (max - min)).coerceIn(0f, 1f)
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(label, color = Color.White.copy(0.45f), fontFamily = FontFamily.Monospace,
             fontSize = 10.sp, modifier = Modifier.width(52.dp))
        Box(
            modifier = Modifier
                .weight(1f)
                .height(4.dp)
                .clip(RoundedCornerShape(2.dp))
                .background(ObsidianSoft)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(fraction)
                    .background(Brush.horizontalGradient(listOf(accent.copy(0.6f), accent)))
            )
        }
        Text(display, color = accent, fontFamily = FontFamily.Monospace,
             fontSize = 10.sp, modifier = Modifier.width(70.dp), textAlign = TextAlign.End)
    }
}

@Composable
private fun DecisionRow(
    label: String,
    value: Float,
    min: Float,
    max: Float,
    accent: Color,
    display: String,
    normalizedValue: Float? = null
) {
    val fraction = normalizedValue ?: ((value - min) / (max - min)).coerceIn(0f, 1f)
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(label, color = Color.White.copy(0.40f), fontFamily = FontFamily.Monospace,
             fontSize = 10.sp, modifier = Modifier.width(110.dp))
        Box(
            modifier = Modifier
                .weight(1f)
                .height(3.dp)
                .clip(RoundedCornerShape(1.5.dp))
                .background(ObsidianSoft)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(fraction)
                    .background(accent.copy(0.7f))
            )
        }
        Text(display, color = accent.copy(0.85f), fontFamily = FontFamily.Monospace,
             fontSize = 10.sp, modifier = Modifier.width(42.dp), textAlign = TextAlign.End)
    }
}

private fun linToDb(linear: Float): Float =
    if (linear > 1e-6f) 20f * log10(max(linear, 1e-6f)) else -60f

private fun Float.format(decimals: Int) = "%.${decimals}f".format(this)
