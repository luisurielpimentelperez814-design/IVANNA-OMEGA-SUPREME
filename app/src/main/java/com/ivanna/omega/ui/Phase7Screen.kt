package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ivanna.omega.spatial.AutoEqManager
import com.ivanna.omega.spatial.ComputerVisionScanner

@Composable
fun Phase7Screen(onBack: () -> Unit) {
    var scanResult by remember { mutableStateOf<String?>(null) }
    var eqEnabled by remember { mutableStateOf(false) }
    
    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text("Fase 7: Hegemonía Total", style = MaterialTheme.typography.titleLarge)
        
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("Visión Computacional (ARCore)", style = MaterialTheme.typography.titleMedium)
                Spacer(modifier = Modifier.height(8.dp))
                Button(onClick = {
                    val data = ComputerVisionScanner.scanEarTopology()
                    scanResult = "Ancho: \${data.headWidthMm}mm\nProfundidad: \${data.headDepthMm}mm\nPinna: \${data.pinnaCavityDepthMm}mm"
                }) {
                    Text("Escanear Topología de Oreja")
                }
                if (scanResult != null) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(scanResult ?: "", color = MaterialTheme.colorScheme.secondary)
                }
            }
        }
        
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("AutoEQ (Neutralización OEM)", style = MaterialTheme.typography.titleMedium)
                Spacer(modifier = Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = eqEnabled, onCheckedChange = { eqEnabled = it })
                    Text("Activar Calibración (Sennheiser HD600)")
                }
            }
        }
        
        Spacer(modifier = Modifier.weight(1f))
        Button(onClick = onBack) { Text("Volver") }
    }
}
