package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.ivanna.omega.spatial.AutoEqManager
import com.ivanna.omega.spatial.IvannaSpatialManager

@Composable
fun Phase7Screen(onBack: () -> Unit) {
    val ctx = LocalContext.current
    // FIX (control muerto + sin persistencia): eqEnabled era un
    // mutableStateOf suelto — no llamaba a AutoEqManager (importado y
    // nunca usado) y se reseteaba a false al recomponer/reabrir.
    val prefs = remember {
        ctx.getSharedPreferences("ivanna_phase7_prefs", android.content.Context.MODE_PRIVATE)
    }
    val profile = remember { AutoEqManager.availableProfiles.first() }

    var eqEnabled by remember { mutableStateOf(prefs.getBoolean("autoEqEnabled", false)) }
    var eqApplied by remember { mutableStateOf(false) }

    // Reaplicar el estado guardado al motor nativo cuando el renderer
    // esté listo (init del SpatialManager es asíncrono).
    LaunchedEffect(eqEnabled) {
        // IvannaSpatialManager.ready no es un State observable, así que se
        // reintenta unos segundos en vez de fallar en silencio si el
        // renderer todavía se está creando en su hilo de init.
        var attempts = 0
        while (attempts < 20) {
            eqApplied = IvannaSpatialManager.setAutoEq(eqEnabled, profile)
            if (eqApplied) break
            attempts++
            kotlinx.coroutines.delay(500)
        }
    }
    
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
                // El escaneo antropométrico real (MediaPipe/ARCore) no está
                // implementado: la versión anterior devolvía medidas fijas
                // hardcodeadas fingiendo un escaneo. Eliminado por completo.
                // La calibración antropométrica real hoy es la manual:
                // sliders de pinna (PinnaMetricsSection) → sujeto HRTF más
                // cercano del dataset (SaFCalibrationScreen).
                Text(
                    "No disponible. La calibración antropométrica activa es la manual (medidas de pinna) en la pantalla de calibración SAF.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.secondary
                )
            }
        }
        
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("AutoEQ (Neutralización OEM)", style = MaterialTheme.typography.titleMedium)
                Spacer(modifier = Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = eqEnabled, onCheckedChange = { on ->
                        eqEnabled = on
                        prefs.edit().putBoolean("autoEqEnabled", on).apply()
                        eqApplied = IvannaSpatialManager.setAutoEq(on, profile)
                    })
                    Text("Activar Calibración ($profile)")
                }
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    when {
                        !eqEnabled -> "AutoEQ desactivado"
                        eqApplied  -> "Aplicado al renderer nativo (bandas AutoEQ activas)"
                        else       -> "Guardado — pendiente: el renderer espacial aún no está listo"
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.secondary
                )
            }
        }
        
        Spacer(modifier = Modifier.weight(1f))
        Button(onClick = onBack) { Text("Volver") }
    }
}
