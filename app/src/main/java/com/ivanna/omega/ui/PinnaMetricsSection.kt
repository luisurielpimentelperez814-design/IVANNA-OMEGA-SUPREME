package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.HrtfSubjectSelector
import com.ivanna.omega.spatial.IvannaSpatialManager

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
    val prefs   = remember { context.getSharedPreferences("pinna_metrics", android.content.Context.MODE_PRIVATE) }

    var concha  by remember { mutableStateOf(prefs.getFloat("concha", 30f)) }
    var helix   by remember { mutableStateOf(prefs.getFloat("helix",  65f)) }
    var fosa    by remember { mutableStateOf(prefs.getFloat("fosa",   24f)) }
    var matched by remember { mutableStateOf(prefs.getString("matched", null)) }

    fun save() = prefs.edit()
        .putFloat("concha", concha)
        .putFloat("helix",  helix)
        .putFloat("fosa",   fosa)
        .apply()

    Card(modifier = modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp),
               verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Geometría de tu oreja (pinna)", style = MaterialTheme.typography.titleMedium)
            Text("Concha: ${concha.toInt()} mm", style = MaterialTheme.typography.bodySmall)
            Slider(value = concha, onValueChange = { concha = it; save() }, valueRange = 20f..45f)
            Text("Hélix: ${helix.toInt()} mm", style = MaterialTheme.typography.bodySmall)
            Slider(value = helix, onValueChange = { helix = it; save() }, valueRange = 50f..80f)
            Text("Fosa: ${fosa.toInt()} mm", style = MaterialTheme.typography.bodySmall)
            Slider(value = fosa, onValueChange = { fosa = it; save() }, valueRange = 15f..35f)
            Button(onClick = {
                val m = HrtfSubjectSelector.PinnaMetrics(concha, helix, fosa)
                matched = HrtfSubjectSelector.findBestMatch(context, m)
                prefs.edit().putString("matched", matched).apply()
                // HUECO CONOCIDO: SET_PINNA_METRICS en el daemon
                // (command_server.cpp) es eco puro — parsea y responde ok,
                // pero no escribe m_state ni publica al bus, así que por sí
                // solo no cambia el DSP. Se sigue llamando (telemetría/ABI),
                // pero el efecto real se aplica por la ruta que SÍ funciona:
                // ObjectRenderer in-process vía setHrtfSubject.
                OmegaEngineBridge.setPinnaMetrics(concha, helix, fosa)
                matched?.let { id ->
                    IvannaSpatialManager.setHrtfSubject(id)
                    SpatialAudioPrefs.save(
                        context,
                        SpatialAudioPrefs.load(context).copy(hrtfSubject = id)
                    )
                }
            }) { Text("Aplicar geometría de oreja") }
            matched?.let {
                Text("Sujeto HRTF más cercano: $it",
                     color = MaterialTheme.colorScheme.primary)
            }
        }
    }
}
