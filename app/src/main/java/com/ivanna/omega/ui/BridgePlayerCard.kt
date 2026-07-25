package com.ivanna.omega.ui

import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.IvannaBridgePlayer
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.ObsidianDeep

/**
 * BridgePlayerCard — control mínimo para IvannaBridgePlayer.
 *
 * Vive en la misma pantalla que IvannaControlPanel (montado por MainActivity)
 * para que sea evidente que este reproductor pasa por el MISMO motor DSP
 * (NHO + Spatial + HRTF + Kernel Evolutivo + fix de interleaving) que los
 * sliders de arriba. Sin este control, todo el tuning fino no tiene un
 * camino real por donde el usuario lo escuche con música (Global Effect
 * Manager solo puede aplicar efectos stock de Android a apps de terceros).
 *
 * API v1: seleccionar archivo, play/pause, stop, indicador de estado.
 * Sin seek, sin cola, sin media-session. Todo eso se agrega encima.
 */
@Composable
fun BridgePlayerCard(
    playerState: IvannaBridgePlayer.State,
    currentUri: Uri?,
    onPickFile: () -> Unit,
    onPlay: () -> Unit,
    onPause: () -> Unit,
    onResume: () -> Unit,
    onStop: () -> Unit,
    // FIX (reproducción consecutiva): cola opcional. Con queue.size <= 1 la
    // tarjeta se comporta exactamente igual que antes (compatibilidad hacia
    // atrás total); con queue.size > 1 aparece la fila de navegación y el
    // mini-listado, sin agregar altura fija que empuje/solape otras cards.
    queue: List<Uri> = emptyList(),
    queueIndex: Int = -1,
    onPickQueue: () -> Unit = {},
    onNext: () -> Unit = {},
    onPrev: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    val stateLabel = when (playerState) {
        IvannaBridgePlayer.State.IDLE    -> "IDLE"
        IvannaBridgePlayer.State.PLAYING -> "PLAYING"
        IvannaBridgePlayer.State.PAUSED  -> "PAUSED"
        IvannaBridgePlayer.State.STOPPED -> "STOPPED"
        IvannaBridgePlayer.State.ERROR   -> "ERROR"
    }
    val stateColor = when (playerState) {
        IvannaBridgePlayer.State.PLAYING -> AuroraCyan
        IvannaBridgePlayer.State.PAUSED  -> Color(0xFFFFC857)
        IvannaBridgePlayer.State.ERROR   -> Color(0xFFFF6B6B)
        else -> Color(0xFFB8C0CC)
    }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 8.dp),
        color = ObsidianDeep,
        shape = RoundedCornerShape(14.dp),
        tonalElevation = 4.dp
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "IVANNA BRIDGE PLAYER",
                    color = AuroraCyan,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 13.sp,
                    modifier = Modifier.weight(1f)
                )
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(Color(0x33000000))
                        .padding(horizontal = 8.dp, vertical = 2.dp)
                ) {
                    Text(
                        stateLabel,
                        color = stateColor,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp
                    )
                }
            }

            Spacer(Modifier.height(4.dp))
            Text(
                text = "Motor completo (NHO + Spatial + Kernel Evo) sonando con archivo real",
                color = Color(0xFF9AA3AF),
                fontSize = 10.sp
            )

            Spacer(Modifier.height(8.dp))
            Text(
                text = currentUri?.lastPathSegment ?: "Ningún archivo seleccionado",
                color = Color(0xFFCFD6E0),
                fontFamily = FontFamily.Monospace,
                fontSize = 11.sp
            )

            if (queue.size > 1) {
                Spacer(Modifier.height(4.dp))
                Text(
                    text = "Pista ${(queueIndex + 1).coerceAtLeast(1)} de ${queue.size}",
                    color = Color(0xFF9AA3AF),
                    fontFamily = FontFamily.Monospace,
                    fontSize = 10.sp
                )
            }

            Spacer(Modifier.height(10.dp))
            // Fila 1: selección de archivo(s). Ancho fijo por botón +
            // spacedBy, nunca se solapa porque no hay Box ni offset manual.
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                FilledTonalButton(onClick = onPickFile) { Text("Archivo…") }
                FilledTonalButton(onClick = onPickQueue) { Text("Cola…") }
            }

            Spacer(Modifier.height(6.dp))
            // Fila 2: transporte. Separada de la fila de selección para que
            // en pantallas angostas el Row haga wrap por línea en vez de
            // comprimir/solapar botones (antes todo iba en una sola Row).
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                if (queue.size > 1) {
                    OutlinedButton(
                        onClick = onPrev,
                        enabled = queueIndex > 0
                    ) { Text("⏮") }
                }
                Button(
                    onClick = onPlay,
                    enabled = currentUri != null &&
                        playerState != IvannaBridgePlayer.State.PLAYING
                ) { Text("▶ Play") }
                if (playerState == IvannaBridgePlayer.State.PLAYING) {
                    OutlinedButton(onClick = onPause) { Text("⏸ Pause") }
                } else if (playerState == IvannaBridgePlayer.State.PAUSED) {
                    OutlinedButton(onClick = onResume) { Text("▶ Resume") }
                }
                OutlinedButton(
                    onClick = onStop,
                    enabled = playerState == IvannaBridgePlayer.State.PLAYING ||
                              playerState == IvannaBridgePlayer.State.PAUSED
                ) { Text("⏹ Stop") }
                if (queue.size > 1) {
                    OutlinedButton(
                        onClick = onNext,
                        enabled = queueIndex in 0 until queue.lastIndex
                    ) { Text("⏭") }
                }
            }
        }
    }
}
