package com.ivanna.omega.assistant.core

import android.content.Context
import android.os.BatteryManager
import android.os.Build
import android.os.PowerManager
import android.app.ActivityManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class DynamicContextEngine(context: Context) {
    private val appContext = context.applicationContext
    private val batteryManager = appContext.getSystemService(Context.BATTERY_SERVICE) as BatteryManager
    private val activityManager = appContext.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
    private val powerManager = appContext.getSystemService(Context.POWER_SERVICE) as PowerManager
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private val _systemContext = MutableStateFlow(SystemContext())
    val systemContext: StateFlow<SystemContext> = _systemContext.asStateFlow()

    private val _audioContext = MutableStateFlow(AudioContext())
    val audioContext: StateFlow<AudioContext> = _audioContext.asStateFlow()

    init {
        scope.launch {
            while (isActive) { _systemContext.value = captureSystemContext(); delay(5000) }
        }
    }

    fun updateAudioContext(sampleRate: Int, audioRoute: String, eqProfile: String, hrtfActive: String, spatialMode: String, presetName: String, dspChain: List<String>, clipEvents: Int, rmsLevel: Float, peakLevel: Float) {
        _audioContext.value = AudioContext(sampleRate, audioRoute, eqProfile, hrtfActive, spatialMode, presetName, dspChain, clipEvents, rmsLevel, peakLevel)
    }

    fun buildFullContext(): String {
        val sys = _systemContext.value
        val audio = _audioContext.value
        return buildString {
            appendLine("[AUDIO]")
            appendLine("Ruta: ${audio.audioRoute} @ ${audio.sampleRate}Hz")
            appendLine("Preset: ${audio.presetName}")
            appendLine("EQ: ${audio.eqProfile} | HRTF: ${audio.hrtfActive} | Spatial: ${audio.spatialMode}")
            appendLine("DSP: ${audio.dspChainActive.joinToString(" → ")}")
            appendLine("RMS: ${String.format("%.1f", audio.currentRmsDb)} dB | Peak: ${String.format("%.1f", audio.currentPeakDb)} dB")
            appendLine("Clips/min: ${audio.clipEventsLastMinute}")
            appendLine()
            appendLine("[SISTEMA]")
            appendLine("CPU: ${sys.cpuCores} cores")
            appendLine("RAM: ${sys.availableRamMb}MB / ${sys.totalRamMb}MB ${if (sys.isLowMemory) "⚠️ LOW" else ""}")
            appendLine("Batería: ${sys.batteryPercent}% ${if (sys.isCharging) "⚡" else ""}")
            appendLine("Térmico: ${sys.thermalStatus}")
            appendLine("Android ${sys.androidVersion} (API ${sys.sdkLevel})")
            appendLine("${sys.deviceModel} ${if (sys.isEmulator) "[EMU]" else ""}")
        }
    }

    fun getCurrentScreenContext(): String = "IVANNA OMEGA SUPREME — Main Control"

    private fun captureSystemContext(): SystemContext {
        val memInfo = ActivityManager.MemoryInfo()
        activityManager.getMemoryInfo(memInfo)
        val batteryPct = batteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
        val status = batteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_STATUS)
        val isCharging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL
        val thermal = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            when (powerManager.currentThermalStatus) {
                PowerManager.THERMAL_STATUS_NONE -> "none"; PowerManager.THERMAL_STATUS_LIGHT -> "light"
                PowerManager.THERMAL_STATUS_MODERATE -> "moderate"; PowerManager.THERMAL_STATUS_SEVERE -> "severe"
                PowerManager.THERMAL_STATUS_CRITICAL -> "critical"; PowerManager.THERMAL_STATUS_EMERGENCY -> "emergency"
                PowerManager.THERMAL_STATUS_SHUTDOWN -> "shutdown"; else -> "unknown"
            }
        } else "unavailable"
        return SystemContext(
            cpuCores = Runtime.getRuntime().availableProcessors(),
            totalRamMb = memInfo.totalMem / (1024 * 1024),
            availableRamMb = memInfo.availMem / (1024 * 1024),
            isLowMemory = memInfo.lowMemory,
            batteryPercent = batteryPct,
            isCharging = isCharging,
            thermalStatus = thermal,
            androidVersion = Build.VERSION.RELEASE,
            sdkLevel = Build.VERSION.SDK_INT,
            deviceModel = "${Build.MANUFACTURER} ${Build.MODEL}",
            isEmulator = Build.FINGERPRINT.contains("generic") || Build.HARDWARE.contains("goldfish") || Build.PRODUCT.contains("sdk")
        )
    }

    data class SystemContext(
        val cpuCores: Int = 0, val totalRamMb: Long = 0, val availableRamMb: Long = 0,
        val isLowMemory: Boolean = false, val batteryPercent: Int = 100,
        val isCharging: Boolean = false, val thermalStatus: String = "unknown",
        val androidVersion: String = "unknown", val sdkLevel: Int = 0,
        val deviceModel: String = "unknown", val isEmulator: Boolean = false
    )

    data class AudioContext(
        val sampleRate: Int = 48000, val audioRoute: String = "unknown",
        val eqProfile: String = "flat", val hrtfActive: String = "none",
        val spatialMode: String = "off", val presetName: String = "default",
        val dspChainActive: List<String> = emptyList(), val clipEventsLastMinute: Int = 0,
        val currentRmsDb: Float = -96f, val currentPeakDb: Float = -96f
    )
}
