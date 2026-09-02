package com.ivanna.omega.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.saf.SaFDirection
import com.ivanna.omega.saf.SaFEngine
import com.ivanna.omega.saf.SaFPhase
import com.ivanna.omega.ui.theme.*
import kotlin.math.cos
import kotlin.math.sin

/**
 * SaFCalibrationScreen — Interactive HRTF personalisation using Φ_SAF^∞.
 *
 * Flow:
 *   IDLE        → START button → CALIBRATING
 *   CALIBRATING → 5 directions × user feedback (CORRECT / INCORRECT)
 *   DONE        → shows 7-D latent parameter radar + option to restart
 *
 * @param onBack navigation callback to parent screen
 */
@Composable
fun SaFCalibrationScreen(
    rendererHandle: Long = 0L,
    onDismiss: () -> Unit = {},
    onOpenSpatialControl: () -> Unit = {}
) {
    val onBack = onDismiss
    val context = LocalContext.current
    val engine  = remember { SaFEngine(context).also { it.initialize() } }
    val state   by engine.state.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {

        // ── Header ────────────────────────────────────────────────────────
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment     = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    "Φ_SAF^∞  CALIBRACIÓN HRTF",
                    color      = AuroraCyan,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize   = 13.sp,
                    letterSpacing = 1.5.sp
                )
                Text(
                    "214 sujetos · 7-D PCA · Riemanniano",
                    color  = TextMuted,
                    fontFamily = FontFamily.Monospace,
                    fontSize   = 10.sp
                )
            }
            OutlinedButton(
                onClick = onBack,
                colors  = ButtonDefaults.outlinedButtonColors(contentColor = TextSecondary),
                border  = BorderStroke(1.dp, ObsidianEdge)
            ) { Text("← VOLVER", fontSize = 10.sp) }
            OutlinedButton(
                onClick = onOpenSpatialControl,
                colors  = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan),
                border  = BorderStroke(1.dp, AuroraCyan.copy(0.4f))
            ) { Text("ESPACIAL", fontSize = 10.sp) }
        }

        // ── Status pill ───────────────────────────────────────────────────
        PhaseBadge(state.phase, state.iteration)

        // ── Geometría de pinna (TAREA 4): solo antes de calibrar ──────────
        // 3 medidas del oído → sujeto HRTF más cercano del dataset SOFA.
        if (state.phase == SaFPhase.IDLE) {
            PinnaMetricsSection()
        }

        // ── Main content by phase ─────────────────────────────────────────
        when (state.phase) {
            SaFPhase.IDLE        -> IdlePanel  { engine.startCalibration() }
            SaFPhase.CALIBRATING -> CalibPanel (
                direction = state.currentDir,
                iteration = state.iteration,
                onCorrect = { engine.feedFeedback(state.currentDir, true) },
                onWrong   = { engine.feedFeedback(state.currentDir, false) }
            )
            SaFPhase.DONE        -> DonePanel(
                params      = state.params,
                iteration   = state.iteration,
                onRestart   = { engine.startCalibration() }
            )
        }

        // ── Latent params radar (visible during + after calibration) ──────
        if (state.phase != SaFPhase.IDLE) {
            LatentRadar(params = state.params)
        }
    }
}

// ── Phase badge ───────────────────────────────────────────────────────────────
@Composable
private fun PhaseBadge(phase: SaFPhase, iter: Int) {
    val (text, color) = when (phase) {
        SaFPhase.IDLE        -> "EN ESPERA"    to TextMuted
        SaFPhase.CALIBRATING -> "CALIBRANDO · iter $iter"  to AmberSignal
        SaFPhase.DONE        -> "CONVERGIDO ✓" to PhosphorGreen
    }
    Surface(
        color  = color.copy(alpha = 0.10f),
        shape  = RoundedCornerShape(20.dp),
        border = BorderStroke(1.dp, color.copy(alpha = 0.40f))
    ) {
        Text(
            text,
            color      = color,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize   = 11.sp,
            modifier   = Modifier.padding(horizontal = 14.dp, vertical = 6.dp)
        )
    }
}

// ── IDLE panel ────────────────────────────────────────────────────────────────
@Composable
private fun IdlePanel(onStart: () -> Unit) {
    Surface(
        color  = ObsidianSoft,
        shape  = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, AuroraCyan.copy(0.20f)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            Modifier.padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text("🎧", fontSize = 40.sp)
            Text(
                "Personalización HRTF adaptativa",
                color      = TextPrimary,
                fontWeight = FontWeight.Bold,
                textAlign  = TextAlign.Center,
                fontSize   = 14.sp
            )
            Text(
                "Escucharás 5 tonos de prueba. Para cada uno, indica si percibes " +
                "la dirección correctamente.\nEl optimizador Riemanniano Φ_SAF^∞ " +
                "ajustará tu HRTF en tiempo real.",
                color     = TextSecondary,
                fontSize  = 12.sp,
                textAlign = TextAlign.Center
            )
            Spacer(Modifier.height(4.dp))
            Button(
                onClick = onStart,
                modifier = Modifier.fillMaxWidth(),
                colors   = ButtonDefaults.buttonColors(containerColor = AuroraCyan)
            ) {
                Text(
                    "INICIAR CALIBRACIÓN",
                    color      = ObsidianVoid,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.sp
                )
            }
        }
    }
}

// ── CALIBRATING panel ─────────────────────────────────────────────────────────
@Composable
private fun CalibPanel(
    direction : SaFDirection,
    iteration : Int,
    onCorrect : () -> Unit,
    onWrong   : () -> Unit
) {
    // Pulse animation for direction arrow
    val inf = rememberInfiniteTransition(label = "pulse")
    val scale by inf.animateFloat(
        initialValue = 0.92f, targetValue = 1.08f,
        animationSpec = infiniteRepeatable(tween(800, easing = FastOutSlowInEasing), RepeatMode.Reverse),
        label = "scale"
    )

    Surface(
        color  = ObsidianSoft,
        shape  = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, AmberSignal.copy(0.30f)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            Modifier.padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Direction name
            Text(
                direction.label,
                color      = AmberSignal,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.ExtraBold,
                fontSize   = 22.sp,
                letterSpacing = 3.sp
            )

            // Big arrow
            Box(
                Modifier
                    .size(96.dp)
                    .scale(scale)
                    .clip(CircleShape)
                    .background(
                        Brush.radialGradient(
                            listOf(AmberSignal.copy(0.25f), Color.Transparent)
                        )
                    ),
                contentAlignment = Alignment.Center
            ) {
                Text(direction.arrow, fontSize = 52.sp)
            }

            // Hint
            Text(
                direction.hint,
                color     = TextSecondary,
                fontSize  = 12.sp,
                textAlign = TextAlign.Center
            )

            // Progress dots
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SaFDirection.ordered.forEachIndexed { i, _ ->
                    Box(
                        Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(
                                if (i < iteration % 5) AuroraCyan else ObsidianEdge
                            )
                    )
                }
            }

            Spacer(Modifier.height(4.dp))

            // Feedback buttons
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // INCORRECT
                OutlinedButton(
                    onClick  = onWrong,
                    modifier = Modifier.weight(1f).height(52.dp),
                    colors   = ButtonDefaults.outlinedButtonColors(contentColor = CoralWarn),
                    border   = BorderStroke(1.5.dp, CoralWarn)
                ) {
                    Text("✗  INCORRECTO", fontWeight = FontWeight.Bold, fontSize = 13.sp)
                }
                // CORRECT
                Button(
                    onClick  = onCorrect,
                    modifier = Modifier.weight(1f).height(52.dp),
                    colors   = ButtonDefaults.buttonColors(containerColor = PhosphorGreen)
                ) {
                    Text(
                        "✓  CORRECTO",
                        color      = ObsidianVoid,
                        fontWeight = FontWeight.Bold,
                        fontSize   = 13.sp
                    )
                }
            }
        }
    }
}

// ── DONE panel ────────────────────────────────────────────────────────────────
@Composable
private fun DonePanel(params: FloatArray, iteration: Int, onRestart: () -> Unit) {
    Surface(
        color  = ObsidianSoft,
        shape  = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, PhosphorGreen.copy(0.35f)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                "✓  HRTF PERSONALIZADO",
                color      = PhosphorGreen,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize   = 14.sp,
                letterSpacing = 1.sp
            )
            Text(
                "Φ_SAF^∞ convergió en $iteration iteraciones.\n" +
                "Tu perfil HRTF ha sido ajustado a tu anatomía auditiva.",
                color     = TextSecondary,
                fontSize  = 12.sp,
                textAlign = TextAlign.Center
            )

            // 7-D param values
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                params.forEachIndexed { i, v ->
                    ParamBar(label = "PC${i + 1}", value = v, index = i)
                }
            }

            Spacer(Modifier.height(4.dp))
            OutlinedButton(
                onClick  = onRestart,
                modifier = Modifier.fillMaxWidth(),
                colors   = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan),
                border   = BorderStroke(1.dp, AuroraCyan.copy(0.40f))
            ) { Text("↺  RE-CALIBRAR", fontWeight = FontWeight.SemiBold) }
        }
    }
}

// ── Latent radar (7-D bar chart) ──────────────────────────────────────────────
@Composable
private fun LatentRadar(params: FloatArray) {
    // Bounds ±3σ from SAF_model.json
    val kMax = floatArrayOf(
        2.888e-2f, 3.102e-2f, 3.354e-2f, 3.923e-2f,
        5.319e-2f, 8.036e-2f, 1.615e-1f
    )
    val colors = listOf(
        AuroraCyan, NeonMagenta, AmberSignal, PhosphorGreen,
        AuroraCyan, NeonMagenta, AmberSignal
    )

    Surface(
        color  = ObsidianSoft,
        shape  = RoundedCornerShape(12.dp),
        border = BorderStroke(1.dp, ObsidianEdge),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                "ESPACIO LATENTE q_t  (Φ_SAF^∞)",
                color      = TextSecondary,
                fontFamily = FontFamily.Monospace,
                fontSize   = 10.sp,
                letterSpacing = 1.sp
            )
            params.forEachIndexed { i, v ->
                if (i < kMax.size) ParamBar(label = "PC${i + 1}", value = v, index = i)
            }
        }
    }
}

@Composable
private fun ParamBar(label: String, value: Float, index: Int) {
    val kMax = floatArrayOf(
        2.888e-2f, 3.102e-2f, 3.354e-2f, 3.923e-2f,
        5.319e-2f, 8.036e-2f, 1.615e-1f
    )
    val accent = listOf(
        AuroraCyan, NeonMagenta, AmberSignal, PhosphorGreen,
        AuroraCyan, NeonMagenta, AmberSignal
    ).getOrElse(index) { AuroraCyan }

    val max      = kMax.getOrElse(index) { 0.1f }
    val fraction = ((value / max + 1f) / 2f).coerceIn(0f, 1f) // map [-max,+max] → [0,1]

    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(label, color = accent.copy(0.7f), fontFamily = FontFamily.Monospace,
             fontSize = 10.sp, modifier = Modifier.width(32.dp))

        Box(
            Modifier
                .weight(1f)
                .height(5.dp)
                .clip(RoundedCornerShape(2.5.dp))
                .background(ObsidianEdge)
        ) {
            // Center line
            Box(
                Modifier
                    .align(Alignment.CenterStart)
                    .padding(start = (fraction * 100).coerceIn(48f, 52f).dp)
                    .width(1.dp)
                    .fillMaxHeight()
                    .background(TextMuted.copy(0.4f))
            )
            // Value bar from center
            val isNeg = value < 0f
            Box(
                modifier = Modifier
                    .align(if (isNeg) Alignment.CenterStart else Alignment.CenterEnd)
                    .fillMaxHeight()
                    .fillMaxWidth((0.5f - fraction).let { if (isNeg) it else 1f - 0.5f - (1f - fraction - 0.5f) }.coerceIn(0f, 0.5f))
                    .background(Brush.horizontalGradient(
                        if (isNeg) listOf(accent.copy(0.3f), accent) else listOf(accent, accent.copy(0.3f))
                    ))
            )
        }

        Text(
            "%.3f".format(value),
            color      = accent,
            fontFamily = FontFamily.Monospace,
            fontSize   = 9.sp,
            modifier   = Modifier.width(52.dp),
            textAlign  = TextAlign.End
        )
    }
}
