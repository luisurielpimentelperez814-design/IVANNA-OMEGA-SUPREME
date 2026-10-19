package com.ivanna.omega.sensing

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import android.content.pm.PackageManager

/**
 * Conservative capability report for FotoHolo wireless sensing.
 *
 * Android's public WifiManager scan API exposes scan metadata/RSSI, not CSI
 * subcarrier matrices. This class deliberately does not infer CSI support from
 * ordinary Wi-Fi availability and does not start scans or collect observations.
 * A vendor/companion CSI provider must implement its own verified adapter.
 */
data class WifiSensingCapabilities(
    val wifiPresent: Boolean,
    val standardScanApiAvailable: Boolean,
    val wifiRttSupported: Boolean,
    val csiProviderRequired: Boolean = true,
    val csiAvailable: Boolean = false,
    val explanation: String
)

object WifiSensingCapabilityProbe {
    fun inspect(context: Context): WifiSensingCapabilities {
        val pm = context.packageManager
        val wifiPresent = pm.hasSystemFeature(PackageManager.FEATURE_WIFI)
        val manager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        val scanApiAvailable = wifiPresent && manager != null
        val rttSupported = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            runCatching { manager?.isDeviceToApRttSupported == true }.getOrDefault(false)
        } else {
            false
        }

        val explanation = when {
            !wifiPresent -> "Este dispositivo no declara Wi-Fi; se requiere un sensor externo compatible."
            rttSupported -> "Wi-Fi RTT está disponible para distancia entre dispositivos compatibles; no equivale a CSI ni a un escaneo corporal."
            scanApiAvailable -> "Solo se detecta la API Wi-Fi estándar. RSSI/listas de redes no proporcionan CSI ni geometría anatómica."
            else -> "No se pudo confirmar una API Wi-Fi estándar; no se iniciará una medición."
        }

        return WifiSensingCapabilities(
            wifiPresent = wifiPresent,
            standardScanApiAvailable = scanApiAvailable,
            wifiRttSupported = rttSupported,
            csiProviderRequired = true,
            csiAvailable = false,
            explanation = explanation
        )
    }
}
