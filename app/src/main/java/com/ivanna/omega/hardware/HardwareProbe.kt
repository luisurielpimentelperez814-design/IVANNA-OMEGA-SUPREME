package com.ivanna.omega.hardware

import android.app.ActivityManager
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorManager
import android.hardware.camera2.CameraManager
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.os.Vibrator
import android.view.WindowManager

/**
 * Qué tiene REALMENTE este teléfono, medido con APIs públicas y SIN pedir ningún permiso:
 *  - getDefaultSensor() solo consulta el catálogo de sensores (no activa ninguno).
 *  - CameraManager.cameraIdList solo lista cámaras (no abre ninguna).
 *  - AudioManager.getDevices() / AudioRecord.getMinBufferSize() no capturan audio.
 *
 * Todo lo que no se pudo determinar queda en su valor "no detectado" en lugar de suponerse presente.
 * Nada de esto prueba que el sensor o la ruta funcionen bajo carga: eso se verifica en el dispositivo.
 */
data class HardwareCapabilities(
    // Sensores de movimiento / entorno
    val hasAccelerometer: Boolean,
    val hasGyroscope: Boolean,
    val hasMagnetometer: Boolean,
    val hasProximity: Boolean,
    val hasRotationVector: Boolean,
    // Cámara (solo presencia; la app no la abre)
    val cameraCount: Int,
    // Háptica
    val hasVibrator: Boolean,
    val hasAmplitudeControl: Boolean,
    // Audio — salida
    val outputSampleRateHz: Int?,
    val outputFramesPerBuffer: Int?,
    val hasLowLatencyFeature: Boolean,
    val hasProAudioFeature: Boolean,
    val hasBuiltinSpeaker: Boolean,
    val hasWiredHeadphones: Boolean,
    val hasUsbAudioOutput: Boolean,
    val hasBluetoothA2dpOutput: Boolean,
    // Audio — entrada (solo soporte; capturar exige RECORD_AUDIO y lo decide el usuario)
    val hasBuiltinMic: Boolean,
    val supportedCaptureRateHz: Int?,
    // Gráficos
    val glEsVersion: String?,
    val hasVulkan: Boolean,
    val displayRefreshRateHz: Float?,
    // Otros
    val hasUsbHost: Boolean,
) {
    /** Líneas legibles para el panel de diagnóstico / registro. Distingue "no detectado" de "sin datos". */
    fun describe(): List<String> = listOf(
        "Acelerómetro: ${yn(hasAccelerometer)} · Giroscopio: ${yn(hasGyroscope)} · Magnetómetro: ${yn(hasMagnetometer)} · Proximidad: ${yn(hasProximity)}",
        "Rotation vector (orientación estimada, NO seguimiento espacial preciso): ${yn(hasRotationVector)}",
        "Cámaras listadas por el sistema: $cameraCount (la app no las abre)",
        "Vibración: ${yn(hasVibrator)} · control de amplitud: ${yn(hasAmplitudeControl)}",
        "Salida de audio nativa: ${outputSampleRateHz ?: "s/d"} Hz, ${outputFramesPerBuffer ?: "s/d"} frames/búfer · baja latencia declarada: ${yn(hasLowLatencyFeature)} · pro audio declarado: ${yn(hasProAudioFeature)}",
        "Rutas de salida ahora: altavoz ${yn(hasBuiltinSpeaker)} · cable ${yn(hasWiredHeadphones)} · USB ${yn(hasUsbAudioOutput)} · Bluetooth A2DP ${yn(hasBluetoothA2dpOutput)}",
        "Micrófono integrado: ${yn(hasBuiltinMic)} · captura PCM16 mono admitida a: ${supportedCaptureRateHz?.let { "$it Hz" } ?: "ninguna frecuencia probada"}",
        "OpenGL ES: ${glEsVersion ?: "s/d"} · Vulkan: ${yn(hasVulkan)} · pantalla: ${displayRefreshRateHz?.let { "%.0f Hz".format(it) } ?: "s/d"}",
        "USB host: ${yn(hasUsbHost)}",
    )

    private fun yn(b: Boolean) = if (b) "sí" else "no detectado"
}

object HardwareProbe {

    /** Última captura de capacidades (null hasta que termina el sondeo de arranque). */
    @Volatile var last: HardwareCapabilities? = null

    /** Frecuencias de captura a probar, de la preferida a la menos preferida. */
    private val CAPTURE_RATES = listOf(48_000, 44_100, 16_000)

    @Suppress("DEPRECATION")
    fun probe(context: Context): HardwareCapabilities {
        val app = context.applicationContext
        val pm = app.packageManager

        val sm = runCatching { app.getSystemService(Context.SENSOR_SERVICE) as? SensorManager }.getOrNull()
        fun sensor(type: Int) = runCatching { sm?.getDefaultSensor(type) != null }.getOrDefault(false)

        val cameras = runCatching {
            (app.getSystemService(Context.CAMERA_SERVICE) as CameraManager).cameraIdList.size
        }.getOrDefault(0)

        val vib = runCatching { app.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator }.getOrNull()

        val am = runCatching { app.getSystemService(Context.AUDIO_SERVICE) as? AudioManager }.getOrNull()
        val outTypes = runCatching {
            am?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)?.map { it.type }?.toSet()
        }.getOrNull().orEmpty()
        val inTypes = runCatching {
            am?.getDevices(AudioManager.GET_DEVICES_INPUTS)?.map { it.type }?.toSet()
        }.getOrNull().orEmpty()

        val glEs = runCatching {
            (app.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager)
                .deviceConfigurationInfo.glEsVersion
        }.getOrNull()

        val refresh = runCatching {
            (app.getSystemService(Context.WINDOW_SERVICE) as WindowManager).defaultDisplay.refreshRate
        }.getOrNull()

        return HardwareCapabilities(
            hasAccelerometer = sensor(Sensor.TYPE_ACCELEROMETER),
            hasGyroscope = sensor(Sensor.TYPE_GYROSCOPE),
            hasMagnetometer = sensor(Sensor.TYPE_MAGNETIC_FIELD),
            hasProximity = sensor(Sensor.TYPE_PROXIMITY),
            hasRotationVector = sensor(Sensor.TYPE_ROTATION_VECTOR) || sensor(Sensor.TYPE_GAME_ROTATION_VECTOR),
            cameraCount = cameras,
            hasVibrator = runCatching { vib?.hasVibrator() == true }.getOrDefault(false),
            hasAmplitudeControl = runCatching { vib?.hasAmplitudeControl() == true }.getOrDefault(false),
            outputSampleRateHz = runCatching { am?.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() }.getOrNull(),
            outputFramesPerBuffer = runCatching { am?.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)?.toIntOrNull() }.getOrNull(),
            hasLowLatencyFeature = feature(pm, PackageManager.FEATURE_AUDIO_LOW_LATENCY),
            hasProAudioFeature = feature(pm, PackageManager.FEATURE_AUDIO_PRO),
            hasBuiltinSpeaker = AudioDeviceInfo.TYPE_BUILTIN_SPEAKER in outTypes,
            hasWiredHeadphones = AudioDeviceInfo.TYPE_WIRED_HEADPHONES in outTypes || AudioDeviceInfo.TYPE_WIRED_HEADSET in outTypes,
            hasUsbAudioOutput = AudioDeviceInfo.TYPE_USB_DEVICE in outTypes || AudioDeviceInfo.TYPE_USB_HEADSET in outTypes,
            hasBluetoothA2dpOutput = AudioDeviceInfo.TYPE_BLUETOOTH_A2DP in outTypes,
            hasBuiltinMic = AudioDeviceInfo.TYPE_BUILTIN_MIC in inTypes,
            supportedCaptureRateHz = PermissionPolicy.pickSupportedRate(CAPTURE_RATES) { rate ->
                runCatching {
                    AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
                }.getOrDefault(-1)
            },
            glEsVersion = glEs,
            hasVulkan = feature(pm, PackageManager.FEATURE_VULKAN_HARDWARE_VERSION),
            displayRefreshRateHz = refresh,
            hasUsbHost = feature(pm, PackageManager.FEATURE_USB_HOST),
        )
    }

    private fun feature(pm: PackageManager, name: String): Boolean =
        runCatching { pm.hasSystemFeature(name) }.getOrDefault(false)
}
