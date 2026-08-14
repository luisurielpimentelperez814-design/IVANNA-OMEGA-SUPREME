package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.HrtfSubjectSelector

/**
 * PinnaMetricsSection — 3 medidas de la oreja del usuario (mm) para
 * individualización HRTF por geometría de pinna.
 *
 * Aplicar → findBestMatch() sobre SAF_model.json (dataset SOFA curado)
 * + SET_PINNA_METRICS al daemon vía socket.
 */
@Composable
fun PinnaMetricsSection(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    var concha by remember { mutableStateOf(30f) }   // 20–45 mm
    var helix  by remember { mutableStateOf(65f) }   // 50–80 mm
    var fosa   by remember { mutableStateOf(24f) }   // 15–35 mm
    var matched by remember { mutableStateOf<String?>(null) }

    Card(modifier = modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp),
               verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Geometría de tu oreja (pinna)", style = MaterialTheme.typography.titleMedium)
            Text("Concha: ${concha.toInt()} mm", style = MaterialTheme.typography.bodySmall)
            Slider(value = concha, onValueChange = { concha = it }, valueRange = 20f..45f)
            Text("Hélix: ${helix.toInt()} mm", style = MaterialTheme.typography.bodySmall)
            Slider(value = helix, onValueChange = { helix = it }, valueRange = 50f..80f)
            Text("Fosa: ${fosa.toInt()} mm", style = MaterialTheme.typography.bodySmall)
            Slider(value = fosa, onValueChange = { fosa = it }, valueRange = 15f..35f)
            Button(onClick = {
                val m = HrtfSubjectSelector.PinnaMetrics(concha, helix, fosa)
                matched = HrtfSubjectSelector.findBestMatch(context, m)
                OmegaEngineBridge.setPinnaMetrics(concha, helix, fosa)
            }) { Text("Aplicar geometría de oreja") }
            matched?.let {
                Text("Sujeto HRTF más cercano: $it",
                     color = MaterialTheme.colorScheme.primary)
            }
        }
    }
}
