package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.CoralWarn
import com.ivanna.omega.ui.theme.MagistralGold
import com.ivanna.omega.ui.theme.MagistralGoldHi
import com.ivanna.omega.ui.theme.MagistralLine
import com.ivanna.omega.ui.theme.MagistralMuted
import com.ivanna.omega.ui.theme.MagistralPanel2
import com.ivanna.omega.ui.theme.MagistralSerif
import com.ivanna.omega.ui.theme.MagistralText
import com.ivanna.omega.ui.theme.PhosphorGreen

/**
 * Ficha de especificación magistral (v2.5.442).
 *
 * Cada control de la app lleva su especificación completa en cuatro partes,
 * reutilizando el texto del prototipo aprobado
 * (~/workspace/ivanna-ui-refinement-text.txt — 124 filas .spec-row):
 * qué es / para qué sirve / cuándo usarlo / cuándo NO usarlo. Nada vago:
 * cada ficha dice realmente qué es, para qué sirve y cuándo sí y cuándo no.
 *
 * Uso:
 * - [SpecInfoButton]: icono "i" dorado junto al título de cada control; abre
 *   el diálogo con las cuatro partes.
 * - [ScreenSpecCard]: ficha de la pantalla/entorno, al inicio de cada sección.
 */
data class SpecInfo(
    val title: String,
    val what: String,
    val purpose: String,
    val whenUse: String,
    val whenAvoid: String
)

/** Botón de información dorado (44 dp de objetivo táctil) junto a cada control. */
@Composable
fun SpecInfoButton(spec: SpecInfo, modifier: Modifier = Modifier) {
    var open by remember { mutableStateOf(false) }
    Box(
        modifier = modifier
            .size(44.dp)
            .clip(CircleShape)
            .border(1.dp, MagistralGold, CircleShape)
            .clickable { open = true },
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = "i",
            fontFamily = MagistralSerif,
            fontWeight = FontWeight.Bold,
            fontSize = 20.sp,
            color = MagistralGoldHi
        )
    }
    if (open) {
        SpecDialog(spec = spec, onDismiss = { open = false })
    }
}

/** Diálogo con las cuatro partes de la especificación de un control. */
@Composable
fun SpecDialog(spec: SpecInfo, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = MagistralPanel2,
        title = {
            Text(
                text = spec.title,
                fontFamily = MagistralSerif,
                fontWeight = FontWeight.Bold,
                fontSize = 20.sp,
                color = MagistralGoldHi
            )
        },
        text = {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                SpecPart(label = "QUÉ ES", text = spec.what, color = AuroraCyan)
                SpecPart(label = "PARA QUÉ SIRVE", text = spec.purpose, color = MagistralGold)
                SpecPart(label = "ÚSALO CUANDO", text = spec.whenUse, color = PhosphorGreen)
                SpecPart(label = "EVÍTALO CUANDO", text = spec.whenAvoid, color = CoralWarn)
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("ENTENDIDO", color = AuroraCyan, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            }
        }
    )
}

@Composable
private fun SpecPart(
    label: String,
    text: String,
    color: androidx.compose.ui.graphics.Color
) {
    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
        Text(
            text = label,
            fontSize = 10.sp,
            fontWeight = FontWeight.ExtraBold,
            letterSpacing = 1.5.sp,
            color = color
        )
        Text(
            text = text,
            fontSize = 13.sp,
            lineHeight = 19.sp,
            color = MagistralText
        )
    }
}

/**
 * Ficha de la pantalla o entorno, plegable: muestra la cabecera y, al tocar,
 * las cuatro partes completas. Para pantallas densas cuya parrilla principal
 * es el contenido (Experiencia Auditiva, Intro), donde la ficha expandida
 * dejaría la parrilla con una sola fila visible. El contenido es el mismo
 * que [ScreenSpecCard]; solo cambia que empieza plegada.
 */
@Composable
fun CollapsibleScreenSpecCard(spec: SpecInfo, modifier: Modifier = Modifier) {
    var expanded by remember { mutableStateOf(false) }
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(MagistralPanel2, RoundedCornerShape(16.dp))
            .border(1.dp, MagistralLine, RoundedCornerShape(16.dp))
            .clickable { expanded = !expanded }
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = "FICHA · ",
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 2.sp,
                color = MagistralGold
            )
            Text(
                text = spec.title.uppercase(),
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 2.sp,
                color = MagistralMuted,
                modifier = Modifier.weight(1f)
            )
            Text(
                text = if (expanded) "OCULTAR ▲" else "LEER FICHA ▼",
                fontSize = 10.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 1.sp,
                color = AuroraCyan
            )
        }
        if (expanded) {
            SpecPart(label = "QUÉ ES", text = spec.what, color = AuroraCyan)
            SpecPart(label = "PARA QUÉ SIRVE", text = spec.purpose, color = MagistralGold)
            SpecPart(label = "ÚSALO CUANDO", text = spec.whenUse, color = PhosphorGreen)
            SpecPart(label = "EVÍTALO CUANDO", text = spec.whenAvoid, color = CoralWarn)
        }
    }
}
@Composable
fun ScreenSpecCard(spec: SpecInfo, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(MagistralPanel2, RoundedCornerShape(16.dp))
            .border(1.dp, MagistralLine, RoundedCornerShape(16.dp))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = "FICHA · ",
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 2.sp,
                color = MagistralGold
            )
            Text(
                text = spec.title.uppercase(),
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 2.sp,
                color = MagistralMuted
            )
        }
        SpecPart(label = "QUÉ ES", text = spec.what, color = AuroraCyan)
        SpecPart(label = "PARA QUÉ SIRVE", text = spec.purpose, color = MagistralGold)
        SpecPart(label = "ÚSALO CUANDO", text = spec.whenUse, color = PhosphorGreen)
        SpecPart(label = "EVÍTALO CUANDO", text = spec.whenAvoid, color = CoralWarn)
        Spacer(Modifier.height(2.dp))
    }
}
