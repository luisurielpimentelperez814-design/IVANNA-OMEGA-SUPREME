package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun IvannaControlPanel(modifier: Modifier = Modifier) {
    val scrollState = rememberScrollState()

    Surface(
        modifier = modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(scrollState)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Header del Sistema
            Text(
                text = "IVANNA OMEGA SUPREME",
                fontSize = 22.sp,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary
            )
            Text(
                text = "Neural Audio Kernel & DSP Dashboard",
                fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            HorizontalDivider()

            // 1. DASHBOARD DE TELEMETRÍA (10Hz)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Telemetry Live (10Hz)", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                    Spacer(modifier = Modifier.height(12.dp))
                    TelemetryBar(label = "Nivel RMS", value = 0.6f, color = MaterialTheme.colorScheme.primary)
                    Spacer(modifier = Modifier.height(8.dp))
                    TelemetryBar(label = "Pico (Peak)", value = 0.8f, color = Color.Red)
                    Spacer(modifier = Modifier.height(8.dp))
                    TelemetryBar(label = "Reducción de Ganancia (GR)", value = 0.2f, color = Color.Yellow)
                }
            }

            // 2. PERFILES Y MOTOR ADAPTATIVO
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Motor Adaptativo & Perfiles", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                    Spacer(modifier = Modifier.height(12.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Button(onClick = { /* TODO */ }) { Text("Natural") }
                        Button(onClick = { /* TODO */ }) { Text("Studio") }
                        Button(onClick = { /* TODO */ }) { Text("Extreme") }
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Anti-Dolby (YAMNet/EMA)", modifier = Modifier.weight(1f))
                        Switch(checked = true, onCheckedChange = { /* TODO */ })
                    }
                }
            }

            // 3. OPE DSP (EQ & COMPRESOR)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("OPE DSP Pipeline", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    ControlSlider(label = "Ecualizador - Graves (Low)", value = 0.5f)
                    ControlSlider(label = "Ecualizador - Medios (Mid)", value = 0.5f)
                    ControlSlider(label = "Ecualizador - Agudos (High)", value = 0.5f)
                    ControlSlider(label = "Compresor Threshold", value = 0.7f)
                }
            }

            // 4. MOTOR BINAURAL (HRTF 32 OBJ)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Motor Binaural & HRTF", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("HRTF 32 Objetos", modifier = Modifier.weight(1f))
                        Switch(checked = true, onCheckedChange = { /* TODO */ })
                    }
                    ControlSlider(label = "Ángulo Espacial / Ancho", value = 0.6f)
                }
            }
            
            Spacer(modifier = Modifier.height(32.dp))
        }
    }
}

@Composable
fun TelemetryBar(label: String, value: Float, color: Color) {
    Column {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, fontSize = 12.sp)
            Text("${(value * 100).toInt()}%", fontSize = 12.sp)
        }
        Spacer(modifier = Modifier.height(4.dp))
        LinearProgressIndicator(
            progress = { value },
            modifier = Modifier
                .fillMaxWidth()
                .height(6.dp),
            color = color
        )
    }
}

@Composable
fun ControlSlider(label: String, value: Float) {
    var sliderVal by remember { mutableStateOf(value) }
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        Text(label, fontSize = 12.sp)
        Slider(
            value = sliderVal,
            onValueChange = { sliderVal = it },
            modifier = Modifier.fillMaxWidth()
        )
    }
}
