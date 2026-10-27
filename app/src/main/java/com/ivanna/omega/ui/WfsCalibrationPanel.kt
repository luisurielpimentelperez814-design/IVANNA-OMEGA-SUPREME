package com.ivanna.omega.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Slider
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
    var yaw by remember { mutableStateOf(WfsCalibrationManager.listenerYawDegrees) }
    var status by remember {
        mutableStateOf("Ajusta la geometría de escucha. Al soltar un control, IVANNA guarda la posición y envía el layout al motor.")
    }

    fun applyPosition() {
        val sent = WfsCalibrationManager.setListener(ctx, lx, ly, lz, yaw)
        status = if (sent) {
            "Geometría enviada al motor. Esto confirma el envío, no una medición acústica ni una mejora audible automática."
        } else {
            "No se pudo enviar: comprueba que el motor nativo esté cargado. Los valores quedan disponibles para volver a intentar."
        }
    }

    fun fmt(value: Float) = String.format(Locale.getDefault(), "%.2f", value)

    Column(
        Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("WFS · CALIBRACIÓN DE SALA", style = MaterialTheme.typography.headlineSmall)
        Text(
            "Indica dónde está tu cabeza y hacia dónde miras respecto al conjunto virtual de siete altavoces. WFS solo ofrece el resultado esperado cuando la salida y el sistema de altavoces son compatibles; no convierte unos audífonos estéreo en un arreglo WFS físico."
        )

        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("1 · Posición izquierda / derecha", style = MaterialTheme.typography.titleMedium)
                Text("Oyente X: ${fmt(lx)} m de 0 a 3,5 m", style = MaterialTheme.typography.bodyLarge)
                Slider(value = lx, onValueChange = { lx = it }, onValueChangeFinished = { applyPosition() }, valueRange = 0f..3.5f)
                Text("Qué hace: mueve el punto de escucha a lo ancho de la sala. Úsalo para corregir si tu asiento queda desplazado del centro.", style = MaterialTheme.typography.bodySmall)
            }
        }

        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("2 · Altura del oyente", style = MaterialTheme.typography.titleMedium)
                Text("Oyente Y: ${fmt(ly)} m de 0 a 3,5 m", style = MaterialTheme.typography.bodyLarge)
                Slider(value = ly, onValueChange = { ly = it }, onValueChangeFinished = { applyPosition() }, valueRange = 0f..3.5f)
                Text("Qué hace: ajusta la altura de referencia de los altavoces virtuales respecto a tus oídos. Mide desde el suelo aproximadamente.", style = MaterialTheme.typography.bodySmall)
            }
        }

        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("3 · Distancia al frente / fondo", style = MaterialTheme.typography.titleMedium)
                Text("Oyente Z: ${fmt(lz)} m de 0 a 7 m", style = MaterialTheme.typography.bodyLarge)
                Slider(value = lz, onValueChange = { lz = it }, onValueChangeFinished = { applyPosition() }, valueRange = 0f..7f)
                Text("Qué hace: sitúa el punto de escucha desde la parte frontal hacia el fondo. Ajústalo a la distancia real entre el asiento y el frente de la sala.", style = MaterialTheme.typography.bodySmall)
            }
        }

        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("4 · Orientación del oyente", style = MaterialTheme.typography.titleMedium)
                Text("Giro horizontal: ${fmt(yaw)}°", style = MaterialTheme.typography.bodyLarge)
                Slider(value = yaw, onValueChange = { yaw = it }, onValueChangeFinished = { applyPosition() }, valueRange = 0f..360f)
                Text("Qué hace: gira el marco de referencia alrededor del eje vertical y recalcula las coordenadas relativas de los siete altavoces. Úsalo si escuchas mirando hacia un lado; 0° conserva la orientación original.", style = MaterialTheme.typography.bodySmall)
            }
        }

        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Acciones rápidas", style = MaterialTheme.typography.titleMedium)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = {
                        val sent = WfsCalibrationManager.applyLayout(ctx, "7ch-default")
                        status = if (sent) "Preset 7ch enviado con la geometría actual. Aún requiere escucha y validación en la sala."
                            else "No se pudo enviar el preset: motor nativo no disponible."
                    }, modifier = Modifier.weight(1f)) { Text("Aplicar preset 7ch") }
                    Button(onClick = {
                        lx = 1.75f
                        ly = 1.20f
                        lz = 3.50f
                        yaw = 0f
                        applyPosition()
                    }, modifier = Modifier.weight(1f)) { Text("Centrar") }
                }
                Text("Preset 7ch: reenvía el diseño de siete altavoces virtuales. Centrar: restaura una posición de referencia y orientación frontal.", style = MaterialTheme.typography.bodySmall)
            }
        }

        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Estado de aplicación", style = MaterialTheme.typography.titleSmall)
                Text(status, style = MaterialTheme.typography.bodyMedium)
            }
        }

        Text(
            "Recomendaciones: en audífonos, usa el modo binaural/HRTF en lugar de WFS físico; en estéreo por AUX o DAC USB-C, elige la salida estéreo normal salvo que conectes un arreglo multicanal real. Para una sala 7.1, confirma primero el número de canales que acepta tu receptor. Haz los ajustes a volumen moderado y cambia una variable por vez.",
            style = MaterialTheme.typography.bodySmall
        )
    }
}
