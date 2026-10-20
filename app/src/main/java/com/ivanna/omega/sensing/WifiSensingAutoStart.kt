package com.ivanna.omega.sensing

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.compose.ui.platform.LocalLifecycleOwner

/**
 * Starts a consented Wi-Fi survey automatically when the app enters foreground.
 * It never starts when opt-in is false, and pauses on backgrounding. The visible
 * status strip doubles as an immediate stop/revoke control.
 */
@Composable
fun WifiSensingAutoStart(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val prefs = remember(context) {
        context.getSharedPreferences("acoustic_consent_v1", Context.MODE_PRIVATE)
    }
    var status by remember { mutableStateOf("Medición Wi-Fi inactiva.") }
    var active by remember { mutableStateOf(false) }
    var optedIn by remember { mutableStateOf(prefs.getBoolean("wifi_sensing_opt_in", false)) }
    var observations by remember { mutableStateOf(0) }
    var networkRows by remember { mutableStateOf<List<WifiObservation>>(emptyList()) }
    var signalSummary by remember { mutableStateOf("Sin datos de señal") }
    var userStopped by remember { mutableStateOf(false) }
    var askedPermission by remember { mutableStateOf(false) }
    val session = remember(context) {
        WifiScanSession(context) { update ->
            status = update.message
            active = update.sessionRunning
            observations = update.observations.size
            if (update.observations.isNotEmpty()) {
                networkRows = update.observations.sortedByDescending { it.rssiDbm }.take(12)
            }
            val scene = update.sceneFeatures
            signalSummary = when (scene.level) {
                UniversalWifiSensingAdapter.DataLevel.NO_WIFI -> "Sin señal Wi-Fi nueva"
                UniversalWifiSensingAdapter.DataLevel.WIFI_METADATA_ONLY -> "RSSI · cambio=${scene.movementIndicator?.let { "%.2f".format(it) } ?: "sin referencia"} · confianza=${"%.0f".format(scene.confidence * 100)}%"
                UniversalWifiSensingAdapter.DataLevel.CSI_VERIFIED -> "CSI verificado · proveedor CSI activo"
            }
        }
    }
    val locationPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted && optedIn && lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
            active = session.start(consentGranted = true)
            if (!active) status = "Permiso concedido, pero el escaneo no pudo iniciarse. Comprueba Ubicación y Wi-Fi."
        } else {
            active = false
            status = if (granted) "Permiso concedido; activa la medición Wi-Fi desde la aplicación."
                else "Permiso de ubicación denegado; el escaneo Android no se iniciará ni se eludirá con root."
        }
    }

    DisposableEffect(lifecycleOwner, session, prefs) {
        fun syncAndStart() {
            optedIn = prefs.getBoolean("wifi_sensing_opt_in", false)
            if (optedIn && !userStopped) {
                val hasLocation = ContextCompat.checkSelfPermission(
                    context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
                if (!hasLocation && !askedPermission) {
                    // Con el consentimiento ya dado, se pide el permiso de ubicación una vez (Android
                    // lo exige para listar redes Wi-Fi) en lugar de esperar a que se toque un botón oculto.
                    askedPermission = true
                    active = false
                    status = "Solicitando el permiso de ubicación que Android exige para escanear Wi-Fi."
                    locationPermissionLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
                } else {
                    active = session.start(consentGranted = true)
                }
            } else {
                session.stop()
                active = false
                status = if (userStopped) "Medición detenida por el usuario."
                    else "Medición Wi-Fi inactiva: consentimiento no habilitado."
            }
        }
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> syncAndStart()
                Lifecycle.Event.ON_PAUSE -> {
                    session.stop()
                    active = false
                    status = "Medición pausada al salir de primer plano."
                }
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
            syncAndStart()
        }
        val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == "wifi_sensing_opt_in") {
                val consentEnabled = prefs.getBoolean("wifi_sensing_opt_in", false)
                optedIn = consentEnabled
                if (consentEnabled) {
                    // Un consentimiento nuevo explícito debe poder reactivar la sesión
                    // en el mismo ciclo de vida; userStopped solo bloquea hasta esta acción.
                    userStopped = false
                    if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
                        active = session.start(consentGranted = true)
                        if (!active) status = "Consentimiento guardado; el sondeo no pudo iniciarse."
                    }
                } else {
                    session.stop()
                    active = false
                    status = "Consentimiento Wi-Fi revocado; medición detenida."
                }
            }
        }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        onDispose {
            prefs.unregisterOnSharedPreferenceChangeListener(listener)
            lifecycleOwner.lifecycle.removeObserver(observer)
            session.stop()
        }
    }

    // FIX (panel blanco que tapaba la app): esta franja vive FUERA del tema oscuro de OmegaApp, así que
    // MaterialTheme.colorScheme.surface era el blanco por defecto, con texto negro, y además se dibujaba
    // ENCIMA del contenido (tapaba la barra inferior y parte del scroll). Ahora ocupa su propio espacio
    // debajo del contenido (el contenido se encoge en vez de quedar tapado), usa colores oscuros
    // explícitos y el gráfico queda plegado por defecto.
    var expanded by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize()) {
        Box(Modifier.weight(1f).fillMaxWidth()) { content() }
        if (optedIn || active) Surface(
            modifier = Modifier.fillMaxWidth(),
            color = Color(0xFF0B1220),
            contentColor = Color(0xFFDDE6F0),
            tonalElevation = 0.dp
        ) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                Text(
                    if (active) "FotoHolo Wi-Fi · sesión en primer plano · $observations observaciones"
                    else "FotoHolo Wi-Fi · inactiva",
                    style = MaterialTheme.typography.labelMedium
                )
                Text(status, style = MaterialTheme.typography.bodySmall)
                Text(signalSummary, style = MaterialTheme.typography.bodySmall)
                // Visualización real del sondeo: cada barra representa una red observada y su RSSI.
                // No presenta RSSI como CSI ni dibuja una reconstrucción espacial ficticia.
                if (expanded) Canvas(
                    modifier = Modifier.fillMaxWidth().height(76.dp)
                ) {
                    val left = 8f
                    val top = 8f
                    val bottom = size.height - 8f
                    val chartWidth = (size.width - 16f).coerceAtLeast(1f)
                    for (step in 0..4) {
                        val y = top + (bottom - top) * step / 4f
                        drawLine(
                            color = Color(0x337F8C9A),
                            start = androidx.compose.ui.geometry.Offset(left, y),
                            end = androidx.compose.ui.geometry.Offset(size.width - 8f, y),
                            strokeWidth = 1f
                        )
                    }
                    val rows = networkRows
                    if (rows.isNotEmpty()) {
                        val slot = chartWidth / rows.size
                        rows.forEachIndexed { index, network ->
                            val normalized = ((network.rssiDbm + 100f) / 70f).coerceIn(0f, 1f)
                            val barHeight = (bottom - top) * normalized
                            val x = left + index * slot + slot * 0.16f
                            val width = (slot * 0.68f).coerceAtLeast(2f)
                            val barColor = when {
                                network.rssiDbm >= -55 -> Color(0xFF37C987)
                                network.rssiDbm >= -70 -> Color(0xFFFFC857)
                                else -> Color(0xFFFF7A7A)
                            }
                            drawRect(
                                color = barColor,
                                topLeft = androidx.compose.ui.geometry.Offset(x, bottom - barHeight),
                                size = androidx.compose.ui.geometry.Size(width, barHeight)
                            )
                        }
                    } else {
                        drawCircle(
                            color = Color(0x667F8C9A),
                            radius = 4f,
                            center = androidx.compose.ui.geometry.Offset(size.width / 2f, size.height / 2f),
                            style = Stroke(width = 2f)
                        )
                    }
                }
                if (expanded) if (networkRows.isNotEmpty()) {
                    Text(
                        networkRows.take(5).joinToString("   ·   ") {
                            "${it.ssid.ifBlank { "<red oculta>" }} ${it.rssiDbm} dBm"
                        },
                        style = MaterialTheme.typography.labelSmall,
                        maxLines = 2
                    )
                } else {
                    Text("El gráfico aparecerá cuando el sondeo entregue redes reales.", style = MaterialTheme.typography.labelSmall)
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End
                ) {
                    androidx.compose.material3.TextButton(onClick = { expanded = !expanded }) {
                        Text(if (expanded) "Ocultar gráfico" else "Ver gráfico")
                    }
                    if (active) {
                        OutlinedButton(onClick = {
                            userStopped = true
                            prefs.edit().putBoolean("wifi_sensing_opt_in", false).apply()
                            session.stop()
                            active = false
                            status = "Consentimiento Wi-Fi revocado; medición detenida."
                        }) {
                            Text("Detener y revocar")
                        }
                    } else if (ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                        OutlinedButton(onClick = {
                            userStopped = false
                            prefs.edit().putBoolean("wifi_sensing_opt_in", true).apply()
                            optedIn = true
                            locationPermissionLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
                        }) {
                            Text(if (optedIn) "Conceder permiso Wi-Fi" else "Activar escaneo Wi-Fi")
                        }
                    } else if (!optedIn) {
                        OutlinedButton(onClick = {
                            userStopped = false
                            prefs.edit().putBoolean("wifi_sensing_opt_in", true).apply()
                            optedIn = true
                            if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
                                active = session.start(consentGranted = true)
                                if (!active) status = "No se pudo iniciar. Comprueba Wi-Fi, Ubicación y permiso preciso."
                            }
                        }) {
                            Text("Activar escaneo Wi-Fi")
                        }
                    } else {
                        OutlinedButton(onClick = {
                            userStopped = false
                            active = session.start(consentGranted = true)
                            if (!active) status = "No se pudo iniciar. Comprueba Wi-Fi, Ubicación y permiso preciso."
                        }) {
                            Text("Reintentar escaneo")
                        }
                    }
                }
            }
        }
    }
}
