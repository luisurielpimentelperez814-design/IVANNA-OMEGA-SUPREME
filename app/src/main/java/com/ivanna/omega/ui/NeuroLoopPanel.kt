/*
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 * Tarjeta de control del bucle cerrado neuro-acústico (v2.5.376).
 */
package com.ivanna.omega.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.ivanna.omega.neuromorphic.NeuroSensorGateway
import com.ivanna.omega.neuromorphic.NeuroUdpDriver
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.TextMuted
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

/** Texto de estado honesto a partir de lo que el bucle nativo reporta. */
internal fun neuroLoopStatusLine(enabled: Boolean, active: Boolean, frames: Int): String = when {
    !enabled -> "APAGADO · el audio no cambia"
    active -> "SENSOR ACTIVO · $frames frames aprendidos"
    frames > 0 -> "FRAMES VIEJOS O DE BAJA CALIDAD · fail-closed"
    else -> "ESPERANDO SENSOR REAL en 127.0.0.1:${NeuroUdpDriver.DEFAULT_PORT}"
}

@Composable
fun NeuroLoopPanel() {
    val driver = remember { NeuroUdpDriver() }
    var enabled by remember { mutableStateOf(NeuroSensorGateway.isEnabled) }
    var status by remember { mutableStateOf(floatArrayOf(0f, 0f, 0f, 0f)) }

    LaunchedEffect(enabled) {
        while (enabled) {
            status = NeuroSensorGateway.status()
            delay(500)
        }
    }

    GlassCard(
        title = "NEURO-ACÚSTICA · BUCLE CERRADO",
        accent = AuroraCyan,
        subtitle = neuroLoopStatusLine(enabled, status[0] > 0.5f, status[2].roundToInt()),
        rightSlot = {
            Switch(enabled, onCheckedChange = { want ->
                if (want) {
                    NeuroSensorGateway.registerDriver(driver)
                    enabled = NeuroSensorGateway.enable()
                } else {
                    NeuroSensorGateway.disable()
                    enabled = false
                }
            })
        }
    ) {
        if (enabled) {
            Text(
                "Confianza ${(status[1] * 100).roundToInt()} % · wet ${"%.2f".format(status[3])} · UDP recibidos ${driver.received.get()} · aceptados ${driver.accepted.get()} · rechazados ${driver.rejected.get()}",
                style = MaterialTheme.typography.labelSmall, color = TextMuted
            )
        }
        Text(
            "Solo datos reales: un procesador de sensor externo (EEG/ABR/fNIRS) envía frames por UDP local. El Moto G85 no trae ninguno; sin él no se inventa nada y el audio queda intacto. Ajustes acotados (±6 dB, con slew); la firma vive solo en RAM. No es un dispositivo médico.",
            style = MaterialTheme.typography.labelSmall, color = TextMuted
        )
    }
}
