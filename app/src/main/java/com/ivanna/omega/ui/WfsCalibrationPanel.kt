package com.ivanna.omega.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.ivanna.omega.spatial.WfsCalibrationManager
import java.util.Locale

@Composable
fun WfsCalibrationPanel() {
    val ctx = LocalContext.current
    var lx by remember { mutableStateOf(WfsCalibrationManager.listenerX) }
    var ly by remember { mutableStateOf(WfsCalibrationManager.listenerY) }
    var lz by remember { mutableStateOf(WfsCalibrationManager.listenerZ) }
    var status by remember { mutableStateOf("Ajusta la posición; al soltar cada control se aplica la geometría relativa.") }

    fun applyPosition(x: Float, y: Float, z: Float) {
        val sent = WfsCalibrationManager.setListener(ctx, x, y, z)
        status = if (sent) {
            "Solicitud de geometría enviada al motor nativo; falta confirmación acústica."
        } else {
            "No enviada: motor nativo no disponible o el setter falló."
        }
    }

    Column(
        Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text("WFS — CALIBRACIÓN DE SALA", style = MaterialTheme.typography.titleMedium)
        Text("Preset: ${WfsCalibrationManager.presetName}")
        Text("Oyente X (ancho, 0–3.5 m): ${String.format(Locale.getDefault(), "%.2f", lx)} m")
        Slider(value = lx, onValueChange = { lx = it }, onValueChangeFinished = { applyPosition(lx, ly, lz) }, valueRange = 0f..3.5f)
        Text("Oyente Y (altura, 0–3.5 m): ${String.format(Locale.getDefault(), "%.2f", ly)} m")
        Slider(value = ly, onValueChange = { ly = it }, onValueChangeFinished = { applyPosition(lx, ly, lz) }, valueRange = 0f..3.5f)
        Text("Oyente Z (fondo, 0–7 m): ${String.format(Locale.getDefault(), "%.2f", lz)} m")
        Slider(value = lz, onValueChange = { lz = it }, onValueChangeFinished = { applyPosition(lx, ly, lz) }, valueRange = 0f..7f)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                val sent = WfsCalibrationManager.applyLayout(ctx, "7ch-default")
                status = if (sent) "Solicitud del preset 7ch enviada; falta confirmación acústica."
                    else "Preset no enviado: motor nativo no disponible o setter fallido."
            }) { Text("Preset 7ch") }
            Button(onClick = {
                lx = 1.75f; ly = 1.20f; lz = 3.50f
                applyPosition(lx, ly, lz)
            }) { Text("Centro") }
        }
        Text(status, style = MaterialTheme.typography.bodySmall)
    }
}
