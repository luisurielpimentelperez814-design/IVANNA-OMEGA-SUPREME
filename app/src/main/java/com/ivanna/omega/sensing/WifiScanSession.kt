package com.ivanna.omega.sensing

import android.Manifest
import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.location.LocationManager
import android.net.wifi.ScanResult
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Foreground, opt-in Wi-Fi survey session.
 *
 * The owner must start only after explicit consent and stop when its UI leaves
 * the foreground. Android scans are preferred; if permissions/location or the
 * Android scan API are unavailable, a Magisk root backend is tried. Root scans
 * expose ordinary BSS/RSSI metadata only; CSI requires a chipset/driver provider.
 */
class WifiScanSession(
    context: Context,
    private val onUpdate: (WifiSurveyUpdate) -> Unit
) {
    private val appContext = context.applicationContext
    private val wifi = appContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
    private val handler = Handler(Looper.getMainLooper())
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val rootBackend = RootWifiSurveyBackend()
    private var running = false
    private var consented = false
    private var receiverRegistered = false
    private var rootScanInFlight = false
    private var scanRequestedAtMs = 0L

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != WifiManager.SCAN_RESULTS_AVAILABLE_ACTION || !running) return
            handler.removeCallbacks(scanTimeoutRunnable)
            val fresh = intent.getBooleanExtra(WifiManager.EXTRA_RESULTS_UPDATED, false)
            if (!fresh) {
                emit("Android no entregó resultados nuevos; se intentará la ruta root opt-in.", emptyList(), false)
                tryRootFallback()
                scheduleNext()
                return
            }
            val results = try {
                wifi?.scanResults.orEmpty()
            } catch (_: SecurityException) {
                emit("Android revocó el acceso al escaneo; se probará la ruta root opt-in.", emptyList(), false)
                tryRootFallback()
                scheduleNext()
                return
            }
            emit("Escaneo Android completado. RSSI y redes visibles no equivalen a CSI.", results.map { it.toObservation() }.sortedByDescending { it.rssiDbm }, true)
            scheduleNext()
        }
    }

    private val scanRunnable = Runnable { requestScan() }
    // Android may accept startScan() but never deliver a completion broadcast.
    // A watchdog prevents a driver/OEM failure from leaving the session stuck.
    private val scanTimeoutRunnable = Runnable {
        if (!running || !consented || scanRequestedAtMs == 0L) return@Runnable
        val elapsed = android.os.SystemClock.elapsedRealtime() - scanRequestedAtMs
        if (elapsed >= SCAN_TIMEOUT_MS) {
            emit("Android no confirmó el escaneo en " + (SCAN_TIMEOUT_MS / 1000) + "s; resultado no verificado. Probando fallback root opt-in.", emptyList(), false)
            tryRootFallback()
            scheduleNext()
        }
    }

    companion object {
        private const val SCAN_INTERVAL_MS = 30_000L
        private const val SCAN_TIMEOUT_MS = 15_000L
    }

    fun start(consentGranted: Boolean): Boolean {
        if (!consentGranted) {
            emit("Activa el consentimiento Wi-Fi en la aplicación antes de iniciar.", emptyList(), false)
            return false
        }
        if (running) return true
        val hasFineLocation = ContextCompat.checkSelfPermission(appContext, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (!hasFineLocation) {
            emit("Falta el permiso de ubicación precisa para escanear Wi-Fi. Concede el permiso explícitamente; no se usará root para eludir una denegación.", emptyList(), false)
            return false
        }
        if (!isLocationEnabled()) {
            emit("Activa Ubicación en Android para permitir el escaneo Wi-Fi; no se usará root para eludir esta protección.", emptyList(), false)
            return false
        }
        consented = true
        running = true
        if (wifi == null || !appContext.packageManager.hasSystemFeature(PackageManager.FEATURE_WIFI)) {
            emit("La API Wi-Fi de Android no está disponible en este dispositivo; comprobando únicamente el backend root consentido.", emptyList(), false)
            tryRootFallback()
            scheduleNext()
            return true
        }

        try {
            val filter = IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION)
            if (Build.VERSION.SDK_INT >= 33) {
                appContext.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
            } else {
                @Suppress("DEPRECATION")
                appContext.registerReceiver(receiver, filter)
            }
            receiverRegistered = true
        } catch (e: Exception) {
            emit("No se pudo registrar el receptor Android: ${e.javaClass.simpleName}; probando root.", emptyList(), false)
            tryRootFallback()
            scheduleNext()
            return true
        }
        emit("Sesión Wi-Fi opt-in activa en primer plano; solicitando medición.", emptyList(), false)
        requestScan()
        return true
    }

    fun stop() {
        running = false
        consented = false
        handler.removeCallbacks(scanRunnable)
        handler.removeCallbacks(scanTimeoutRunnable)
        if (receiverRegistered) {
            runCatching { appContext.unregisterReceiver(receiver) }
            receiverRegistered = false
        }
        emit("Sesión Wi-Fi detenida; no se harán más solicitudes.", emptyList(), false)
    }

    private fun stopWithMessage(message: String) {
        running = false
        consented = false
        handler.removeCallbacks(scanRunnable)
        handler.removeCallbacks(scanTimeoutRunnable)
        if (receiverRegistered) {
            runCatching { appContext.unregisterReceiver(receiver) }
            receiverRegistered = false
        }
        emit(message, emptyList(), false)
    }

    private fun isLocationEnabled(): Boolean = try {
        val lm = appContext.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) lm?.isLocationEnabled == true
        else {
            @Suppress("DEPRECATION")
            lm?.isProviderEnabled(LocationManager.GPS_PROVIDER) == true ||
                lm?.isProviderEnabled(LocationManager.NETWORK_PROVIDER) == true
        }
    } catch (_: Exception) { false }

    @SuppressLint("MissingPermission")
    private fun requestScan() {
        if (!running || !consented) return
        val hasPermission = ContextCompat.checkSelfPermission(appContext, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (!hasPermission) {
            stopWithMessage("Permiso de ubicación revocado durante el sondeo; sesión detenida sin intentar eludir el permiso.")
            return
        }
        if (!isLocationEnabled()) {
            stopWithMessage("Ubicación de Android desactivada; sesión detenida hasta que se reactive.")
            return
        }
        if (wifi == null) {
            tryRootFallback()
            scheduleNext()
            return
        }
        scanRequestedAtMs = android.os.SystemClock.elapsedRealtime()
        val accepted = try { wifi.startScan() } catch (_: SecurityException) { false } catch (_: RuntimeException) { false }
        if (!accepted) {
            handler.removeCallbacks(scanTimeoutRunnable)
            emit("Android rechazó o limitó el escaneo; probando backend root opt-in.", emptyList(), false)
            tryRootFallback()
            scheduleNext()
        } else {
            handler.removeCallbacks(scanTimeoutRunnable)
            handler.postDelayed(scanTimeoutRunnable, SCAN_TIMEOUT_MS)
        }
    }

    private fun tryRootFallback() {
        if (!running || !consented || rootScanInFlight) return
        rootScanInFlight = true
        scope.launch(Dispatchers.IO) {
            val result = rootBackend.scanOnce(consented)
            handler.post {
                rootScanInFlight = false
                if (running && consented) {
                    emit(result.message, result.observations, result.observations.isNotEmpty())
                }
            }
        }
    }

    private fun scheduleNext() {
        if (!running || !consented) return
        handler.removeCallbacks(scanRunnable)
        handler.postDelayed(scanRunnable, SCAN_INTERVAL_MS)
    }

    private fun emit(message: String, observations: List<WifiObservation>, fresh: Boolean) {
        onUpdate(WifiSurveyUpdate(message, observations, fresh, running, UniversalWifiSensingAdapter.analyzeMetadata(observations)))
    }

    private fun ScanResult.toObservation() = WifiObservation(
        ssid = SSID.orEmpty().take(64),
        rssiDbm = level,
        frequencyMhz = frequency,
        timestampMicros = timestamp
    )
}

data class WifiObservation(
    val ssid: String,
    val rssiDbm: Int,
    val frequencyMhz: Int,
    val timestampMicros: Long
)

data class WifiSurveyUpdate(
    val message: String,
    val observations: List<WifiObservation>,
    val freshResults: Boolean,
    val sessionRunning: Boolean,
    val sceneFeatures: UniversalWifiSensingAdapter.SceneFeatures
)
