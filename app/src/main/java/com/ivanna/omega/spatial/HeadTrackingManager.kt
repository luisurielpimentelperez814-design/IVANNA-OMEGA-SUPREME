package com.ivanna.omega.spatial

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.SystemClock
import android.util.Log

class HeadTrackingManager(private val context: Context, private val trackerHandle: Long) : SensorEventListener {

    // FIX (crash de inicialización): mismo patrón que IvannaHeadTracker —
    // getSystemService/getDefaultSensor en la declaración del campo = se
    // ejecutan en el constructor. Si el contexto de Activity muere antes
    // del primer start(), el SensorManager queda colgado y el siguiente
    // registerListener revienta. Perezoso + applicationContext.
    private val appContext = context.applicationContext
    private val sensorManager: SensorManager by lazy {
        appContext.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    }
    private val rotationSensor: Sensor? by lazy {
        sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
            ?: sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
    }
    private val gyroSensor: Sensor? by lazy {
        sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE)
    }
    
    private val filter = OrientationFilter()
    private val predictor = OrientationPredictor()

    private var isTracking = false
    private val rotationMatrix = FloatArray(9)
    private val quaternion = FloatArray(4)

    fun start() {
        if (isTracking) return
        rotationSensor?.let {
            sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_FASTEST)
        }
        gyroSensor?.let {
            sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_FASTEST)
        }
        isTracking = true
        Log.i("IvannaHeadTracker", "Head tracking started")
    }

    fun stop() {
        if (!isTracking) return
        sensorManager.unregisterListener(this)
        isTracking = false
        Log.i("IvannaHeadTracker", "Head tracking stopped")
    }
    
    fun recenter() {
        filter.reset()
        predictor.reset()
        if (trackerHandle != 0L) {
            runCatching { IvannaSpatialNative.nativeHeadTrackerReset(trackerHandle) }
        }
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (event == null || trackerHandle == 0L) return
        
        val timestampMs = SystemClock.elapsedRealtimeNanos() / 1_000_000f

        if (event.sensor.type == Sensor.TYPE_ROTATION_VECTOR || event.sensor.type == Sensor.TYPE_GAME_ROTATION_VECTOR) {
            SensorManager.getQuaternionFromVector(quaternion, event.values)
            
            // x, y, z, w
            val filteredQ = filter.filter(quaternion[1], quaternion[2], quaternion[3], quaternion[0], timestampMs)
            val predictedQ = predictor.predict(filteredQ[0], filteredQ[1], filteredQ[2], filteredQ[3], timestampMs)
            
            IvannaSpatialNative.nativeHeadTrackerUpdate(
                trackerHandle, 
                predictedQ[0], predictedQ[1], predictedQ[2], predictedQ[3], 
                timestampMs
            )
        } else if (event.sensor.type == Sensor.TYPE_GYROSCOPE) {
            predictor.updateGyro(event.values[0], event.values[1], event.values[2], timestampMs)
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
}

class OrientationFilter {
    // One Euro Filter implementation for quaternions (simplified low-pass for stability)
    private var lastX = 0f
    private var lastY = 0f
    private var lastZ = 0f
    private var lastW = 1f
    private var isFirst = true

    fun reset() {
        isFirst = true
    }

    fun filter(x: Float, y: Float, z: Float, w: Float, timestampMs: Float): FloatArray {
        if (isFirst) {
            lastX = x; lastY = y; lastZ = z; lastW = w
            isFirst = false
            return floatArrayOf(x, y, z, w)
        }
        
        // Simple SLERP approximation / Exponential smoothing (Alpha = 0.8)
        val alpha = 0.8f
        
        // Ensure shortest path
        var dot = lastX * x + lastY * y + lastZ * z + lastW * w
        var tx = x
        var ty = y
        var tz = z
        var tw = w
        
        if (dot < 0) {
            tx = -tx; ty = -ty; tz = -tz; tw = -tw
        }

        lastX = lastX * (1 - alpha) + tx * alpha
        lastY = lastY * (1 - alpha) + ty * alpha
        lastZ = lastZ * (1 - alpha) + tz * alpha
        lastW = lastW * (1 - alpha) + tw * alpha
        
        // Normalize
        val len = Math.sqrt((lastX*lastX + lastY*lastY + lastZ*lastZ + lastW*lastW).toDouble()).toFloat()
        if (len > 0) {
            lastX /= len; lastY /= len; lastZ /= len; lastW /= len
        }

        return floatArrayOf(lastX, lastY, lastZ, lastW)
    }
}

class OrientationPredictor {
    // Dead reckoning prediction using gyroscope (to hide audio buffer latency)
    private var gyroX = 0f
    private var gyroY = 0f
    private var gyroZ = 0f
    private var lastGyroTime = 0f
    
    // Look-ahead prediction time in ms (e.g. 10ms for audio buffer)
    private val predictionTimeMs = 10f

    fun reset() {
        gyroX = 0f; gyroY = 0f; gyroZ = 0f
    }

    fun updateGyro(gx: Float, gy: Float, gz: Float, timestampMs: Float) {
        gyroX = gx
        gyroY = gy
        gyroZ = gz
        lastGyroTime = timestampMs
    }

    fun predict(qx: Float, qy: Float, qz: Float, qw: Float, timestampMs: Float): FloatArray {
        if (Math.abs(timestampMs - lastGyroTime) > 100f) {
            // Gyro data too old, return unmodified
            return floatArrayOf(qx, qy, qz, qw)
        }
        
        // Small angle approximation for quaternion integration
        val dt = predictionTimeMs / 1000f // seconds
        
        val dqX = gyroX * dt * 0.5f
        val dqY = gyroY * dt * 0.5f
        val dqZ = gyroZ * dt * 0.5f
        
        // Multiply quaternions: Q_new = Q_old * dQ
        val nx = qw * dqX + qx * 1f + qy * dqZ - qz * dqY
        val ny = qw * dqY - qx * dqZ + qy * 1f + qz * dqX
        val nz = qw * dqZ + qx * dqY - qy * dqX + qz * 1f
        val nw = qw * 1f - qx * dqX - qy * dqY - qz * dqZ
        
        // Normalize
        val len = Math.sqrt((nx*nx + ny*ny + nz*nz + nw*nw).toDouble()).toFloat()
        if (len > 0) {
            return floatArrayOf(nx/len, ny/len, nz/len, nw/len)
        }
        
        return floatArrayOf(qx, qy, qz, qw)
    }
}
