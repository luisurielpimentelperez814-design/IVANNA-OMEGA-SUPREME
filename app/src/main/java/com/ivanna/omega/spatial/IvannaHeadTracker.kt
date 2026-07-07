package com.ivanna.omega.spatial

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.Looper

/**
 * IvannaHeadTracker — Rastreo de cabeza 6DoF para audio holográfico.
 *
 * [MAJESTY-HT-1.0] Usa el giroscopio + acelerómetro del dispositivo para
 * rastrear la orientación de la cabeza del usuario. Cuando el usuario gira
 * la cabeza, el sonido se queda "fijo en el espacio" — como si los
 * instrumentos estuvieran físicamente alrededor de ti.
 *
 * Esto es algo que NI Dolby NI Sony ofrecen en dispositivos Android
 * genéricos. Solo funciona con hardware propietario (AirPods Pro,
// WH-1000XM4, etc.). IVANNA lo hace con CUALQUIER dispositivo Android.
 *
 * Uso:
 *   val tracker = IvannaHeadTracker(context)
 *   tracker.start()
 *   // El tracker actualiza automáticamente el motor espacial C++
 *   tracker.stop()
 */
class IvannaHeadTracker(private val context: Context) : SensorEventListener {

    private val sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val rotationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)

    internal var nativeHandle: Long = 0
    private val mainHandler = Handler(Looper.getMainLooper())
    private var isRunning = false

    // Callback para notificar a la UI de cambios de orientación
    var onOrientationChanged: ((pitch: Float, yaw: Float, roll: Float) -> Unit)? = null

    fun init() {
        if (nativeHandle != 0L) return
        nativeHandle = IvannaSpatialNative.nativeHeadTrackerCreate()
    }

    fun start() {
        if (isRunning) return
        init()

        rotationSensor?.let {
            // 100Hz = 10ms de latencia IMU — suficiente para audio
            sensorManager.registerListener(this, it, 10000, mainHandler)
            isRunning = true
        }
    }

    fun stop() {
        if (!isRunning) return
        sensorManager.unregisterListener(this)
        isRunning = false
    }

    fun reset() {
        if (nativeHandle != 0L) {
            IvannaSpatialNative.nativeHeadTrackerReset(nativeHandle)
        }
    }

    fun release() {
        stop()
        if (nativeHandle != 0L) {
            IvannaSpatialNative.nativeHeadTrackerDestroy(nativeHandle)
            nativeHandle = 0
        }
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type != Sensor.TYPE_ROTATION_VECTOR) return
        if (nativeHandle == 0L) return

        // rotationVector[0..2] = x,y,z; [3] = scalar (cos(theta/2))
        // [4] = accuracy (opcional)
        val x = event.values[0]
        val y = event.values[1]
        val z = event.values[2]
        // [FIX-CRASH] TYPE_ROTATION_VECTOR puede reportar solo 3 elementos
        // (sin el componente escalar) en algunos HALs/dispositivos —
        // Motorola incluido. Leer values[3] directo revienta con
        // ArrayIndexOutOfBoundsException en cuanto llega el primer evento,
        // justo al encender el head tracker. Si falta, se deriva w.
        val w = if (event.values.size > 3) event.values[3]
                else {
                    val sq = 1f - (x * x + y * y + z * z)
                    if (sq > 0f) kotlin.math.sqrt(sq) else 0f
                }
        val timestampMs = event.timestamp / 1_000_000f  // ns → ms

        IvannaSpatialNative.nativeHeadTrackerUpdate(nativeHandle, x, y, z, w, timestampMs)

        // [FIX-CRASH] getRotationMatrixFromVector puede lanzar IllegalArgumentException
        // en algunos HALs cuando event.values tiene formato inesperado. La UI se
        // limita a mostrar orientación — no crítico para audio — así que un fallo
        // aquí nunca debe tumbar el hilo de sensor. Se envuelve en try/catch y solo
        // se emite el callback si la conversión tuvo éxito.
        val listener = onOrientationChanged
        if (listener != null) {
            try {
                val rotationMatrix = FloatArray(9)
                SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values)
                val orientation = FloatArray(3)
                SensorManager.getOrientation(rotationMatrix, orientation)
                // orientation[0] = azimuth (yaw), [1] = pitch, [2] = roll
                listener(orientation[1], orientation[0], orientation[2])
            } catch (_: Throwable) {
                // silencioso — el hilo debe seguir procesando frames del giroscopio
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    companion object {
        const val TAG = "IvannaHeadTracker"
    }
}
