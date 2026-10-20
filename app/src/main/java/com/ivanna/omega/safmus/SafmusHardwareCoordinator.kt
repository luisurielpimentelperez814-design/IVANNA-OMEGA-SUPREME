package com.ivanna.omega.safmus

import android.app.ActivityManager
import android.content.Context
import android.hardware.SensorManager
import android.hardware.camera2.CameraManager
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.BatteryManager
import android.os.Build
import android.os.PowerManager
import android.util.Log
import android.view.Display
import android.hardware.display.DisplayManager
import com.ivanna.omega.hardware.HardwareProbe
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * SAFMUS whole-device adapter coordinator.
 *
 * Adapters use public Android APIs to observe device domains and feed one bounded
 * resource budget to IVANNA. They do not claim to tune opaque OEM HALs. Actual
 * controls remain with their owning IVANNA subsystem; unsupported domains abstain.
 */
object SafmusHardwareCoordinator {
    private const val TAG = "SAFMUS-Hardware"
    private const val PREFS = "safmus_hardware_runtime"
    private const val POLL_MS = 5_000L
    @Volatile private var job: Job? = null

    enum class Budget { NORMAL, CONSERVATIVE, SAFE }

    data class Snapshot(
        val timestampMs: Long,
        val cpuThreads: Int,
        val availableMemoryKb: Long,
        val batteryPercent: Int?,
        val powerSave: Boolean,
        val thermalStatus: Int?,
        val gpuApi: String,
        val displayRefreshHz: Float?,
        val cameraCount: Int,
        val audioInputs: Int,
        val audioOutputs: Int,
        val sensorCount: Int,
        val hasVibrator: Boolean,
        val budget: Budget,
        val reasons: List<String>
    ) {
        fun asText(): String = buildString {
            appendLine("schema=1")
            appendLine("timestamp_ms=$timestampMs")
            appendLine("cpu_threads=$cpuThreads")
            appendLine("available_memory_kb=$availableMemoryKb")
            appendLine("battery_percent=${batteryPercent ?: "unknown"}")
            appendLine("power_save=$powerSave")
            appendLine("thermal_status=${thermalStatus ?: "unknown"}")
            appendLine("gpu_api=$gpuApi")
            appendLine("display_refresh_hz=${displayRefreshHz ?: "unknown"}")
            appendLine("camera_count=$cameraCount")
            appendLine("audio_inputs=$audioInputs")
            appendLine("audio_outputs=$audioOutputs")
            appendLine("sensor_count=$sensorCount")
            appendLine("has_vibrator=$hasVibrator")
            appendLine("budget=${budget.name.lowercase()}")
            appendLine("reasons=${reasons.joinToString(";")}")
            appendLine("actuation=delegated_to_existing_ivanna_subsystems")
        }
    }

    @Volatile var lastSnapshot: Snapshot? = null
        private set

    /** Idempotent. Call from IVANNAApplication after its existing subsystem startup. */
    fun start(context: Context, scope: CoroutineScope) {
        if (job?.isActive == true) return
        val app = context.applicationContext
        job = scope.launch(Dispatchers.IO) {
            while (isActive) {
                val previousBudget = lastSnapshot?.budget
                val deepProbe = previousBudget == null || previousBudget == Budget.NORMAL
                runCatching {
                    val snapshot = collect(app, deepProbe)
                    lastSnapshot = snapshot
                    // Connect the cross-domain resource budget to IVANNA's existing
                    // native DSP governor; NORMAL restores its baseline tier.
                    com.ivanna.omega.audio.ThermalGovernor.setExternalResourceBudget(
                        when (snapshot.budget) {
                            Budget.NORMAL -> 0
                            Budget.CONSERVATIVE -> 1
                            Budget.SAFE -> 2
                        }
                    )
                    if (previousBudget != snapshot.budget) {
                        // Re-apply route DSP when the SAFMUS budget changes.
                        runCatching { com.ivanna.omega.audio.AudioRouteManager.refreshResourceBudgetProfile() }
                    }
                    app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .putString("last_snapshot", snapshot.asText())
                        .putString("last_budget", snapshot.budget.name)
                        .putLong("last_timestamp_ms", snapshot.timestampMs)
                        .apply()
                    Log.i(TAG, snapshot.asText().trim().replace('\n', ' '))
                    // The existing ThermalGovernor owns native DSP actuation. This coordinator
                    // only publishes the cross-domain budget; it never writes arbitrary sysfs/HAL.
                }.onFailure { Log.w(TAG, "Hardware adapters failed safely: ${it.message}") }
                val load = when (lastSnapshot?.budget ?: Budget.NORMAL) {
                    Budget.NORMAL -> SafmusAdapterPolicy.Load.NORMAL
                    Budget.CONSERVATIVE -> SafmusAdapterPolicy.Load.CONSERVATIVE
                    Budget.SAFE -> SafmusAdapterPolicy.Load.SAFE
                }
                delay(SafmusAdapterPolicy.cadence(load).intervalMs)
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private fun collect(context: Context, deepProbe: Boolean): Snapshot {
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
        val memory = ActivityManager.MemoryInfo()
        runCatching { am?.getMemoryInfo(memory) }
        val pm = context.getSystemService(Context.POWER_SERVICE) as? PowerManager
        val thermal = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            runCatching { pm?.currentThermalStatus }.getOrNull()
        } else null
        val batteryManager = context.getSystemService(Context.BATTERY_SERVICE) as? BatteryManager
        val battery = runCatching {
            batteryManager?.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
                ?.takeIf { it in 0..100 }
        }.getOrNull()
        val powerSave = runCatching { pm?.isPowerSaveMode == true }.getOrDefault(false)

        // Inventory probes are lower priority than live safety signals.
        val prior = lastSnapshot
        val caps = if (deepProbe) runCatching { HardwareProbe.probe(context) }.getOrNull() else null
        val cameraCount = if (deepProbe) runCatching {
            (context.getSystemService(Context.CAMERA_SERVICE) as CameraManager).cameraIdList.size
        }.getOrDefault(caps?.cameraCount ?: prior?.cameraCount ?: 0) else (prior?.cameraCount ?: 0)
        val audio = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        val inputs = runCatching {
            audio?.getDevices(AudioManager.GET_DEVICES_INPUTS)?.size
        }.getOrNull() ?: 0
        val outputs = runCatching {
            audio?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)?.size
        }.getOrNull() ?: 0
        val sensors = if (deepProbe) runCatching {
            (context.getSystemService(Context.SENSOR_SERVICE) as SensorManager).getSensorList(
                android.hardware.Sensor.TYPE_ALL
            ).size
        }.getOrDefault(prior?.sensorCount ?: 0) else (prior?.sensorCount ?: 0)
        val displayRefresh = if (deepProbe) runCatching {
            val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
            dm.getDisplay(Display.DEFAULT_DISPLAY)?.refreshRate?.takeIf { it.isFinite() && it > 0f }
        }.getOrNull() ?: caps?.displayRefreshRateHz else prior?.displayRefreshHz
        val gpuApi = when {
            caps?.hasVulkan == true -> "vulkan_declared"
            !caps?.glEsVersion.isNullOrBlank() -> "gles_${caps?.glEsVersion}"
            else -> prior?.gpuApi ?: "unknown"
        }
        val reasons = mutableListOf<String>()
        var budget = Budget.NORMAL
        if (memory.availMem > 0L && memory.availMem < 256L * 1024L * 1024L) {
            budget = Budget.CONSERVATIVE
            reasons += "low_memory"
        }
        if (thermal != null && thermal >= PowerManager.THERMAL_STATUS_SEVERE) {
            budget = Budget.CONSERVATIVE
            reasons += "thermal_severe_or_higher"
        }
        if (battery != null && battery <= 10 && !powerSave) {
            budget = Budget.CONSERVATIVE
            reasons += "battery_critical"
        }
        if (powerSave || (thermal != null && thermal >= PowerManager.THERMAL_STATUS_CRITICAL)) {
            budget = Budget.SAFE
            reasons += if (powerSave) "system_power_save" else "thermal_critical"
        }
        if (reasons.isEmpty()) reasons += "within_observed_limits"
        return Snapshot(
            timestampMs = System.currentTimeMillis(),
            cpuThreads = Runtime.getRuntime().availableProcessors().coerceAtLeast(1),
            availableMemoryKb = memory.availMem / 1024L,
            batteryPercent = battery,
            powerSave = powerSave,
            thermalStatus = thermal,
            gpuApi = gpuApi,
            displayRefreshHz = displayRefresh,
            cameraCount = cameraCount,
            audioInputs = inputs,
            audioOutputs = outputs,
            sensorCount = sensors,
            hasVibrator = caps?.hasVibrator ?: prior?.hasVibrator ?: false,
            budget = budget,
            reasons = reasons
        )
    }
}
