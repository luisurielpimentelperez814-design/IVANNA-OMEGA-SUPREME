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

/**
 * Opt-in Wi-Fi survey session for FotoHolo.
 *
 * Call start() only after the user has enabled Wi-Fi sensing and granted the
 * required runtime permission. Keep the owning screen in the foreground and
 * call stop() from its onStop/onDestroy. Android may throttle/reject scans;
 * this class reports that state and never treats cached results as fresh.
 *
 * This collects ordinary scan metadata/RSSI only. It is NOT CSI and cannot
 * reconstruct anatomy or room geometry on its own.
 */
class WifiScanSession(
    context: Context,
    private val onUpdate: (WifiSurveyUpdate) -> Unit
) {
    private val appContext = context.applicationContext
    private val wifi = appContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
    private val handler = Handler(Looper.getMainLooper())
    private var running = false
    private var receiverRegistered = false
    private var scanRequestedAtMs = 0L

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != WifiManager.SCAN_RESULTS_AVAILABLE_ACTION || !running) return
            val fresh = intent.getBooleanExtra(WifiManager.EXTRA_RESULTS_UPDATED, false)
            if (!fresh) {
                emit("Android no entregó resultados nuevos; no se reutilizan resultados antiguos.", emptyList(), false)
                scheduleNext()
                return
            }
            val results = try {
                wifi?.scanResults.orEmpty()
            } catch (_: SecurityException) {
                emit("Permiso de Wi-Fi/ubicación revocado; sesión detenida.", emptyList(), false)
                stop()
                return
            }
            val observations = results.map { it.toObservation() }
                .sortedByDescending { it.rssiDbm }
            emit("Medición Wi-Fi estándar completada. RSSI no es CSI.", observations, true)
            scheduleNext()
        }
    }

    private val scanRunnable = Runnable { requestScan() }

    @SuppressLint("MissingPermission")
    fun start(consentGranted: Boolean): Boolean {
        if (!consentGranted) {
            emit("Activa el consentimiento Wi-Fi en la aplicación antes de iniciar.", emptyList(), false)
            return false
        }
        if (running) return true
        val manager = wifi
        if (manager == null || !appContext.packageManager.hasSystemFeature(PackageManager.FEATURE_WIFI)) {
            emit("Este dispositivo no expone Wi-Fi; conecta un sensor compatible si lo necesitas.", emptyList(), false)
            return false
        }
        if (ContextCompat.checkSelfPermission(appContext, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED) {
            emit("Falta el permiso de ubicación precisa requerido por Android para leer resultados Wi-Fi.", emptyList(), false)
            return false
        }
        if (!isLocationEnabled()) {
            emit("Activa Ubicación en Android para permitir resultados de escaneo Wi-Fi.", emptyList(), false)
            return false
        }
        running = true
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
            running = false
            emit("No se pudo registrar el receptor de resultados Wi-Fi: ${e.javaClass.simpleName}.", emptyList(), false)
            return false
        }
        emit("Sesión visible iniciada; se solicitará un escaneo y Android decidirá si está permitido.", emptyList(), false)
        requestScan()
        return true
    }

    fun stop() {
        running = false
        handler.removeCallbacks(scanRunnable)
        if (receiverRegistered) {
            runCatching { appContext.unregisterReceiver(receiver) }
            receiverRegistered = false
        }
        emit("Sesión Wi-Fi detenida.", emptyList(), false)
    }

    private fun isLocationEnabled(): Boolean = try {
        val lm = appContext.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) lm?.isLocationEnabled == true
        else {
            @Suppress("DEPRECATION")
            (lm?.isProviderEnabled(LocationManager.GPS_PROVIDER) == true ||
                lm?.isProviderEnabled(LocationManager.NETWORK_PROVIDER) == true)
        }
    } catch (_: Exception) {
        false
    }

    @SuppressLint("MissingPermission")
    private fun requestScan() {
        if (!running) return
        if (ContextCompat.checkSelfPermission(appContext, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED || !isLocationEnabled()) {
            emit("Permiso o Ubicación ya no disponibles; sesión detenida.", emptyList(), false)
            stop()
            return
        }
        scanRequestedAtMs = android.os.SystemClock.elapsedRealtime()
        val accepted = try {
            wifi?.startScan() == true
        } catch (_: SecurityException) {
            false
        } catch (_: RuntimeException) {
            false
        }
        if (!accepted) {
            emit("Android rechazó o limitó el escaneo. Se reintentará con pausa; no se inventan resultados.", emptyList(), false)
            scheduleNext()
        }
    }

    private fun scheduleNext() {
        if (!running) return
        handler.removeCallbacks(scanRunnable)
        // 30 s cadence stays conservative under Android's foreground scan throttling.
        val elapsed = android.os.SystemClock.elapsedRealtime() - scanRequestedAtMs
        handler.postDelayed(scanRunnable, (30_000L - elapsed).coerceAtLeast(30_000L))
    }

    private fun emit(message: String, observations: List<WifiObservation>, fresh: Boolean) {
        onUpdate(WifiSurveyUpdate(message, observations, fresh, running))
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
    val sessionRunning: Boolean
)
