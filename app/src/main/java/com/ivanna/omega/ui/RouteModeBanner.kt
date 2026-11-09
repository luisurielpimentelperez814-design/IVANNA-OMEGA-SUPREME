package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.AudioBackendSelector
import com.ivanna.omega.audio.RouteCapabilities
import com.ivanna.omega.ui.theme.AmberSignal
import com.ivanna.omega.ui.theme.ObsidianRaised
import com.ivanna.omega.ui.theme.TextPrimary
import com.ivanna.omega.ui.theme.TextSecondary

/**
 * Aviso honesto de qué funciones no llegan al audio en el modo actual.
 * No aparece con root + daemon + Ruta B confirmada.
 *
 * v2.5.369 (refactor visual): antes usaba colores Material genéricos
 * (fondo verde 0x33FFB300, amarillo 0xFFFFD54F, grises 0xFFE0E0E0) fuera
 * de la paleta Aurora Obsidiana. Ahora es ObsidianRaised con borde ámbar,
 * tipografía del tema, y el subtítulo distingue daemon vivo (el problema
 * es el efecto sin confirmar, no el daemon) de daemon caído.
 */
@Composable
fun RouteModeBanner(modifier: Modifier = Modifier) {
    val mode by AudioBackendSelector.mode.collectAsState()
    val daemonAlive by AudioBackendSelector.daemonAlive.collectAsState()
    val limits = RouteCapabilities.limitsFor(mode, daemonAlive) ?: return
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(ObsidianRaised)
            .border(1.dp, AmberSignal.copy(alpha = 0.55f), RoundedCornerShape(14.dp))
            .padding(14.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "⚠",
                color = AmberSignal,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            Text(
                limits.first,
                color = AmberSignal,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(start = 8.dp)
            )
        }
        Text(
            // Con daemon vivo el problema ya no es el daemon: es que el
            // efecto del sistema aún no está confirmado. El texto viejo
            // ("requieren el daemon") mentía en ese caso.
            if (daemonAlive)
                "El daemon responde, pero el efecto del sistema aún no está procesando. Estas funciones no cambian el audio hasta que Ruta B se confirme:"
            else
                "Estas funciones requieren el daemon Magisk y no cambian el audio en este modo:",
            color = TextSecondary,
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(top = 6.dp)
        )
        limits.second.forEach {
            Text(
                "• $it",
                color = TextPrimary.copy(alpha = 0.85f),
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(top = 2.dp)
            )
        }
    }
}
