package com.ivanna.omega.ui

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

/**
 * First-run disclosure gate for optional acoustic features.
 * This records feature choices only; Android runtime permissions and system
 * MediaProjection consent must still be requested immediately before use.
 */
@Composable
fun AcousticConsentGate(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val prefs = remember(context) {
        context.getSharedPreferences("acoustic_consent_v1", Context.MODE_PRIVATE)
    }
    var accepted by remember { mutableStateOf(prefs.getBoolean("disclosure_accepted", false)) }
    var mic by remember { mutableStateOf(prefs.getBoolean("room_mic_opt_in", false)) }
    var network by remember { mutableStateOf(prefs.getBoolean("local_network_opt_in", false)) }
    var geometry by remember { mutableStateOf(prefs.getBoolean("geometry_camera_opt_in", false)) }
    var adaptive by remember { mutableStateOf(prefs.getBoolean("adaptive_dsp_opt_in", false)) }

    if (accepted) {
        content()
        return
    }

    Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Text("Antes de activar la inteligencia acústica", style = MaterialTheme.typography.headlineSmall)
            Text(
                "IVANNA-OMEGA puede usar mediciones para personalizar el audio. Estas funciones son opcionales. " +
                    "Puedes continuar sin activarlas y cambiar tus decisiones después en Ajustes."
            )
            HorizontalDivider()
            Text("Qué hace cada opción", style = MaterialTheme.typography.titleMedium)
            Text("Micrófono: solo para una sesión visible de calibración iniciada por ti. No implica grabación ambiental permanente ni subida de audio.")
            Text("Red local/Wi-Fi: solo para descubrir dispositivos de audio compatibles cuando lo solicites. La red no revela de forma fiable las dimensiones de una habitación ni la anatomía de nadie.")
            Text("Geometría: si el dispositivo lo admite, una futura medición guiada por cámara/profundidad puede estimar distancias. Se muestra una vista previa y la incertidumbre; no se escanea en segundo plano.")
            Text("Ajuste DSP adaptativo: permite proponer/aplicar cambios de audio limitados. Nunca debe elevar automáticamente el volumen maestro; los cambios deben poder deshacerse.")
            Text("Metadatos musicales: solo fuentes autorizadas y campos permitidos por cada proveedor. No se interceptan ni descifran streams ni se envía audio protegido a una IA sin autorización expresa del proveedor.")
            HorizontalDivider()
            Text("Autoriza por función (todas desactivadas por defecto)", style = MaterialTheme.typography.titleMedium)
            ConsentChoice(mic, { mic = it }, "Permitir iniciar calibración acústica con micrófono")
            ConsentChoice(network, { network = it }, "Permitir descubrimiento local de dispositivos de audio")
            ConsentChoice(geometry, { geometry = it }, "Permitir medición de espacio mediante cámara/sensor compatible")
            ConsentChoice(adaptive, { adaptive = it }, "Permitir modo de ajustes DSP adaptativos reversibles")
            Text(
                "Tu elección aquí no sustituye los permisos de Android. El sistema preguntará por micrófono, cámara u otras capacidades justo cuando abras la función correspondiente. " +
                    "Puedes denegar permisos sin bloquear el resto de la aplicación."
            )
            Button(
                onClick = {
                    prefs.edit()
                        .putBoolean("disclosure_accepted", true)
                        .putBoolean("room_mic_opt_in", mic)
                        .putBoolean("local_network_opt_in", network)
                        .putBoolean("geometry_camera_opt_in", geometry)
                        .putBoolean("adaptive_dsp_opt_in", adaptive)
                        .apply()
                    accepted = true
                },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Continuar") }
            OutlinedButton(
                onClick = {
                    // Continue with optional sensing and adaptation disabled.
                    prefs.edit().putBoolean("disclosure_accepted", true)
                        .putBoolean("room_mic_opt_in", false)
                        .putBoolean("local_network_opt_in", false)
                        .putBoolean("geometry_camera_opt_in", false)
                        .putBoolean("adaptive_dsp_opt_in", false).apply()
                    accepted = true
                },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Continuar sin funciones opcionales") }
        }
    }
}

@Composable
private fun ConsentChoice(checked: Boolean, onChecked: (Boolean) -> Unit, label: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Checkbox(checked = checked, onCheckedChange = onChecked)
        Text(label, modifier = Modifier.weight(1f).padding(top = 12.dp))
    }
}
