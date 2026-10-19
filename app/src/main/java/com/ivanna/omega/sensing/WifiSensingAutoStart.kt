package com.ivanna.omega.sensing

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
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
    var signalSummary by remember { mutableStateOf("Sin datos de señal") }
    var userStopped by remember { mutableStateOf(false) }
    var askedPermission by remember { mutableStateOf(false) }
    val session = remember(context) {
        WifiScanSession(context) { update ->
            status = update.message
            active = update.sessionRunning
            observations = update.observations.size
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

    Box(Modifier.fillMaxSize()) {
        content()
        if (optedIn || active) Surface(
            modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth(),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.97f),
            tonalElevation = 5.dp
        ) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                Text(
                    if (active) "FotoHolo Wi-Fi · sesión en primer plano · $observations observaciones"
                    else "FotoHolo Wi-Fi · inactiva",
                    style = MaterialTheme.typography.labelMedium
                )
                Text(status, style = MaterialTheme.typography.bodySmall)
                Text(signalSummary, style = MaterialTheme.typography.bodySmall)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End
                ) {
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
                    } else if (optedIn &&
                        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                        OutlinedButton(onClick = {
                            locationPermissionLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
                        }) {
                            Text("Conceder permiso Wi-Fi")
                        }
                    }
                }
            }
        }
    }
}
