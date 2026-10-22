package com.ivanna.omega.fotoholo

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.SystemClock
import android.view.Gravity
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.google.ar.core.ArCoreApk
import com.google.ar.core.Camera
import com.google.ar.core.Config
import com.google.ar.core.Coordinates2d
import com.google.ar.core.Frame
import com.google.ar.core.PointCloud
import com.google.ar.core.Session
import com.google.ar.core.TrackingState
import com.google.ar.core.exceptions.NotYetAvailableException
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import org.json.JSONArray
import org.json.JSONObject
import com.ivanna.omega.safmus.SafmusUniversalKernel
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge

/**
 * Escáner espacial ARCore. Los puntos y las dimensiones son estimaciones visuales,
 * no una medición acústica ni un plano arquitectónico certificado.
 */
class RoomArCaptureActivity : Activity(), GLSurfaceView.Renderer {
    @Volatile private var session: Session? = null
    private lateinit var glView: GLSurfaceView
    private lateinit var statusView: TextView
    private lateinit var startButton: Button
    private lateinit var finishButton: Button
    private lateinit var exportButton: Button
    private lateinit var lockRp88Button: Button
    private lateinit var materialButton: Button
    private var selectedMaterialIndex = 1
    private var latestCalibration: AcousticRoomCalibration? = null
    private var latestCalibrationApplied = false
    private var latestCalibrationLockRequested = false
    private var latestSpatialWidthReadback = Float.NaN
    private var latestSpatialAngleReadback = Float.NaN
    private var latestRealityIntensityReadback = Float.NaN
    private var latestAntiDolbyIntensityReadback = Float.NaN
    private var latestOcclusionReadback = Float.NaN
    private var latestWallAbsorptionReadback = Float.NaN
    private var latestPhysicalSceneReadbackConfirmed = false
    private var installRequested = false
    // ARCore exige setDisplayGeometry(rotación, ancho, alto) antes de update(); sin ello la imagen de
    // cámara sale estirada/rotada (aspecto "horrible" justo tras conceder el permiso). Se aplica en el
    // hilo GL cuando cambia la superficie o la rotación de pantalla.
    @Volatile private var viewportWidth = 0
    @Volatile private var viewportHeight = 0
    @Volatile private var viewportChanged = true
    @Volatile private var sessionConfiguredDepth = false
    private var lastSnapshot: CaptureSnapshot? = null
    private var pendingExport: CaptureSnapshot? = null
    private var cameraTextureId = -1
    private var textureSession: Session? = null
    private var shaderProgram = 0
    private var positionHandle = 0
    private var texCoordHandle = 0
    private var textureHandle = 0
    private val captureEnabled = AtomicBoolean(false)
    private val pointsLock = Any()
    private val points = ArrayList<Point3>(16_384)
    private val poseSamples = ArrayList<PoseSample>()
    private var depthPointCount = 0
    private var sparsePointCount = 0
    private var captureStartedNs = 0L
    private var lastDepthSampleNs = 0L
    private var lastPoseSampleNs = 0L
    private var lastUiStatusMs = 0L
    @Volatile private var lastTrackingState = "Esperando cámara"
    // FIX (vista en blanco sin aviso): session.update() puede fallar en CADA frame (cámara ocupada por
    // otra app, aún no entregó el primer frame, HAL caída) y antes el catch solo actualizaba statusView
    // cuando captureEnabled era true — antes de pulsar "Iniciar captura" el error quedaba invisible: la
    // pantalla se veía gris/negra sin ninguna pista. Ahora el error SIEMPRE se muestra, y si pasan >1.5 s
    // sin un solo frame real tras resume() se avisa aunque no haya excepción (cámara entregando pero la
    // textura externa nunca se vincula, p. ej.).
    @Volatile private var framesRendered = 0L
    private var resumedAtNs = 0L
    private var lastErrorUiMs = 0L
    private val quadVertices = floatBuffer(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f))
    private val cameraTexCoords = floatBuffer(FloatArray(8))

    data class Point3(val x: Float, val y: Float, val z: Float)
    data class PoseSample(val timeMs: Long, val x: Float, val y: Float, val z: Float, val qx: Float, val qy: Float, val qz: Float, val qw: Float)
    data class CaptureSnapshot(
        val points: List<Point3>,
        val poses: List<PoseSample>,
        val depthPointCount: Int,
        val sparsePointCount: Int,
        val depthSupported: Boolean,
        val durationMs: Long,
        val createdAt: String,
        val trackingState: String,
        val optimizationRejectedCount: Int = 0,
        val optimizationReducedCount: Int = 0,
        val optimizationTruncatedCount: Int = 0
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(3, 8, 18)
        window.navigationBarColor = Color.rgb(3, 8, 18)
        buildUi()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA), REQUEST_CAMERA)
        }
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(3, 8, 18))
            setPadding(dp(12), dp(10), dp(12), dp(12))
        }
        val title = TextView(this).apply {
            text = "AR & ESCÁNER RP-88 · 6-DoF"
            setTextColor(Color.rgb(111, 243, 255))
            textSize = 16f
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(6), 0, dp(8))
        }
        statusView = TextView(this).apply {
            text = "Esperando permiso de cámara…"
            setTextColor(Color.WHITE)
            textSize = 12f
            setBackgroundColor(Color.argb(170, 3, 8, 18))   // legible sobre cualquier imagen de cámara
            setPadding(dp(10), dp(8), dp(10), dp(8))
        }
        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(2)
            preserveEGLContextOnPause = true
            setRenderer(this@RoomArCaptureActivity)
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        val cameraFrame = FrameLayout(this).apply {
            addView(glView, FrameLayout.LayoutParams(-1, -1))
            addView(statusView, FrameLayout.LayoutParams(-1, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP))
        }
        val controls = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            setPadding(0, dp(8), 0, 0)
        }
        val calibrationControls = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            setPadding(0, dp(4), 0, 0)
        }
        startButton = Button(this).apply { text = "Iniciar captura"; setOnClickListener { beginCapture() } }
        finishButton = Button(this).apply { text = "Finalizar"; isEnabled = false; setOnClickListener { finishCapture() } }
        exportButton = Button(this).apply { text = "Exportar ZIP"; isEnabled = false; setOnClickListener { requestExport() } }
        materialButton = Button(this).apply {
            text = "Material: ${AcousticRoomCalibrationModel.materials[selectedMaterialIndex].label}"
            setOnClickListener {
                selectedMaterialIndex = (selectedMaterialIndex + 1) % AcousticRoomCalibrationModel.materials.size
                text = "Material: ${AcousticRoomCalibrationModel.materials[selectedMaterialIndex].label}"
                latestCalibration = lastSnapshot?.let { estimateCalibration(it) }
                latestCalibrationApplied = false
                latestCalibrationLockRequested = false
                latestSpatialWidthReadback = Float.NaN
                latestSpatialAngleReadback = Float.NaN
                latestRealityIntensityReadback = Float.NaN
                latestAntiDolbyIntensityReadback = Float.NaN
                latestOcclusionReadback = Float.NaN
                latestWallAbsorptionReadback = Float.NaN
                latestPhysicalSceneReadbackConfirmed = false
            }
        }
        lockRp88Button = Button(this).apply {
            text = "LOCK RP-88"
            isEnabled = false
            setOnClickListener { lockRp88Calibration() }
        }
        controls.addView(startButton, LinearLayout.LayoutParams(0, dp(52), 1f))
        controls.addView(finishButton, LinearLayout.LayoutParams(0, dp(52), 1f))
        controls.addView(exportButton, LinearLayout.LayoutParams(0, dp(52), 1f))
        calibrationControls.addView(materialButton, LinearLayout.LayoutParams(0, dp(48), 1.1f))
        calibrationControls.addView(lockRp88Button, LinearLayout.LayoutParams(0, dp(48), 1f))
        root.addView(title)
        root.addView(cameraFrame, LinearLayout.LayoutParams(-1, 0, 1f))
        root.addView(controls)
        root.addView(calibrationControls)
        root.addView(TextView(this).apply {
            text = "SAF/RP-88: Sabine/Eyring son estimaciones geométricas; LOCK envía parámetros al motor nativo si está disponible. No mide RIR/RT60 real."
            setTextColor(Color.rgb(180, 194, 214))
            textSize = 10f
            setPadding(dp(4), dp(8), dp(4), 0)
        })
        setContentView(root)
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_CAMERA) {
            if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
                // onResume() se ejecuta justo después de este callback y es quien crea/reanuda la sesión;
                // hacerlo aquí también duplicaba resume() y glView.onResume().
                statusView.text = "Permiso concedido. Preparando ARCore…"
            } else {
                statusView.text = "Permiso de cámara denegado. Actívalo en Ajustes para escanear."
                startButton.isEnabled = false
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            ensureSession()
            try {
                session?.resume()
                glView.onResume()
                armStuckPreviewCheck()
            } catch (e: Exception) {
                statusView.text = arErrorMessage(e)
            }
        }
    }

    /** Reinicia el contador de fotogramas y programa el diagnóstico de "pantalla en blanco" 1.6 s después. */
    private fun armStuckPreviewCheck() {
        viewportChanged = true
        framesRendered = 0L
        resumedAtNs = SystemClock.elapsedRealtimeNanos()
        statusView.postDelayed({ checkStuckPreview() }, 1_600L)
    }

    private fun ensureSession() {
        if (session != null || isFinishing) return
        try {
            when (ArCoreApk.getInstance().requestInstall(this, !installRequested)) {
                ArCoreApk.InstallStatus.INSTALL_REQUESTED -> {
                    installRequested = true
                    statusView.text = "Instala/actualiza Google Play Services for AR y vuelve a abrir."
                    return
                }
                ArCoreApk.InstallStatus.INSTALLED -> Unit
            }
            val created = Session(this)
            val config = Config(created)
            sessionConfiguredDepth = created.isDepthModeSupported(Config.DepthMode.AUTOMATIC)
            if (sessionConfiguredDepth) config.depthMode = Config.DepthMode.AUTOMATIC
            config.updateMode = Config.UpdateMode.LATEST_CAMERA_IMAGE
            created.configure(config)
            session = created
            glView.queueEvent { textureSession = null }
            statusView.text = if (sessionConfiguredDepth) {
                "ARCore listo · profundidad disponible. Inicia y recorre el lugar lentamente."
            } else {
                "ARCore listo · sin profundidad automática; se usarán puntos visuales dispersos."
            }
            startButton.isEnabled = true
        } catch (e: Exception) {
            statusView.text = arErrorMessage(e)
            startButton.isEnabled = false
        }
    }

    /** Mensaje accionable para cada fallo conocido de ARCore (nunca el texto crudo del SDK salvo desconocidos). */
    private fun arErrorMessage(e: Exception): String = when (e) {
        is com.google.ar.core.exceptions.UnavailableDeviceNotCompatibleException -> "Este dispositivo no es compatible con ARCore."
        is com.google.ar.core.exceptions.UnavailableArcoreNotInstalledException -> "Falta Google Play Services for AR."
        is com.google.ar.core.exceptions.UnavailableApkTooOldException -> "Actualiza Google Play Services for AR."
        is com.google.ar.core.exceptions.UnavailableSdkTooOldException -> "La versión de ARCore necesita actualizarse."
        is com.google.ar.core.exceptions.UnavailableUserDeclinedInstallationException -> "Rechazaste instalar Google Play Services for AR. Instálalo para escanear."
        is com.google.ar.core.exceptions.CameraNotAvailableException -> "La cámara está en uso por otra app. Ciérrala y vuelve a abrir."
        else -> "ARCore no disponible: ${e.message ?: e.javaClass.simpleName}"
    }

    override fun onPause() {
        resumedAtNs = 0L   // invalida cualquier checkStuckPreview() ya encolado con postDelayed
        glView.onPause()
        try { session?.pause() } catch (_: Exception) {}
        super.onPause()
    }

    override fun onDestroy() {
        try { session?.close() } catch (_: Exception) {}
        session = null
        super.onDestroy()
    }

    private fun beginCapture() {
        synchronized(pointsLock) {
            points.clear(); poseSamples.clear(); depthPointCount = 0; sparsePointCount = 0
        }
        captureStartedNs = SystemClock.elapsedRealtimeNanos()
        lastDepthSampleNs = 0L
        lastPoseSampleNs = 0L
        lastSnapshot = null
        captureEnabled.set(true)
        startButton.isEnabled = false
        finishButton.isEnabled = true
        exportButton.isEnabled = false
        statusView.text = "Capturando… mueve el teléfono lentamente y barre paredes, esquinas y suelo."
    }

    private fun finishCapture() {
        captureEnabled.set(false)
        val rawSnapshot = synchronized(pointsLock) {
            CaptureSnapshot(
                points = points.toList(),
                poses = poseSamples.toList(),
                depthPointCount = depthPointCount,
                sparsePointCount = sparsePointCount,
                depthSupported = sessionConfiguredDepth,
                durationMs = ((SystemClock.elapsedRealtimeNanos() - captureStartedNs) / 1_000_000L).coerceAtLeast(0L),
                createdAt = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }.format(Date()),
                trackingState = lastTrackingState
            )
        }
        val optimizedCloud = SafmusUniversalKernel.optimizeSpatialPointCloud(
            rawSnapshot.points.map { SafmusUniversalKernel.SpatialPoint3(it.x, it.y, it.z) }
        )
        val snapshot = rawSnapshot.copy(
            points = optimizedCloud.points.map { Point3(it.x, it.y, it.z) },
            optimizationRejectedCount = optimizedCloud.rejectedInvalidCount,
            optimizationReducedCount = optimizedCloud.reducedByVoxelCount,
            optimizationTruncatedCount = optimizedCloud.truncatedCount
        )
        val safmusAssessment = assessSafmus(snapshot)
        lastSnapshot = snapshot
        startButton.isEnabled = session != null
        finishButton.isEnabled = false
        exportButton.isEnabled = snapshot.points.isNotEmpty()
        latestCalibration = estimateCalibration(snapshot)
        lockRp88Button.isEnabled = latestCalibration != null && snapshot.points.isNotEmpty()
        latestCalibrationApplied = false
        latestCalibrationLockRequested = false
        latestSpatialWidthReadback = Float.NaN
        latestSpatialAngleReadback = Float.NaN
        latestRealityIntensityReadback = Float.NaN
        latestAntiDolbyIntensityReadback = Float.NaN
        latestOcclusionReadback = Float.NaN
        latestWallAbsorptionReadback = Float.NaN
        latestPhysicalSceneReadbackConfirmed = false
        val b = bounds(snapshot.points)
        statusView.text = if (snapshot.points.isEmpty()) {
            "Captura vacía: apunta a superficies con textura y buena luz, y vuelve a intentar."
        } else {
            "Captura lista · ${snapshot.points.size} puntos · profundidad ${snapshot.depthPointCount} · ${snapshot.poses.size} poses\n" +
                "Extensión observada aprox.: ${"%.2f".format(Locale.US, b[3] - b[0])} × ${"%.2f".format(Locale.US, b[4] - b[1])} × ${"%.2f".format(Locale.US, b[5] - b[2])} m. No es plano certificado.\n" +
                "SAFMUS · ${safmusAssessment.quality}: ${safmusAssessment.reason}\n" +
                "Optimización de nube: -${snapshot.optimizationRejectedCount} inválidos, -${snapshot.optimizationReducedCount} redundantes, ${snapshot.points.size} puntos finales\n" +
                (latestCalibration?.let { "RT60 estimado Eyring ${"%.2f".format(Locale.US, it.eyringRt60Seconds)} s · V ${"%.1f".format(Locale.US, it.volumeM3)} m³ · material ${it.materialPreset}" } ?: "Geometría insuficiente para estimar acústica")
        }
    }

    private fun requestExport() {
        val snapshot = lastSnapshot ?: return
        val assessment = assessSafmus(snapshot)
        if (!assessment.exportAllowed) {
            statusView.text = "SAFMUS bloqueó la exportación: ${assessment.reason}"
            return
        }
        pendingExport = snapshot
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/zip"
            putExtra(Intent.EXTRA_TITLE, "ivanna-room-capture-${System.currentTimeMillis()}.zip")
        }
        startActivityForResult(intent, REQUEST_EXPORT)
    }

    @Deprecated("Activity result is retained for compatibility with the app's current activity setup.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_EXPORT) return
        val snapshot = pendingExport
        pendingExport = null
        if (resultCode != Activity.RESULT_OK || data?.data == null || snapshot == null) {
            statusView.text = "Exportación cancelada."
            return
        }
        try {
            contentResolver.openOutputStream(data.data!!)?.use { writeArchive(it, snapshot) }
                ?: error("No se pudo abrir el destino de exportación")
            statusView.text = "ZIP exportado: point_cloud.ply + room_capture.json."
        } catch (e: Exception) {
            statusView.text = "Falló la exportación: ${e.message ?: e.javaClass.simpleName}"
        }
    }

    private fun writeArchive(output: OutputStream, snapshot: CaptureSnapshot) {
        ZipOutputStream(output).use { zip ->
            zip.putNextEntry(ZipEntry("point_cloud.ply")); zip.write(plyBytes(snapshot.points)); zip.closeEntry()
            zip.putNextEntry(ZipEntry("room_capture.json")); zip.write(metadataBytes(snapshot)); zip.closeEntry()
            zip.putNextEntry(ZipEntry("rp88_calibration.json")); zip.write(calibrationJson(snapshot).toString(2).toByteArray(Charsets.UTF_8)); zip.closeEntry()
        }
    }

    private fun plyBytes(data: List<Point3>): ByteArray {
        val out = StringBuilder()
        out.append("ply\nformat ascii 1.0\ncomment Generated by IVANNA OMEGA SUPREME ARCore capture\n")
        out.append("element vertex ${data.size}\nproperty float x\nproperty float y\nproperty float z\nend_header\n")
        data.forEach { out.append(it.x).append(' ').append(it.y).append(' ').append(it.z).append('\n') }
        return out.toString().toByteArray(Charsets.UTF_8)
    }

    private fun metadataBytes(snapshot: CaptureSnapshot): ByteArray {
        val b = bounds(snapshot.points)
        val json = JSONObject()
            .put("schemaVersion", 1)
            .put("createdAtUtc", snapshot.createdAt)
            .put("captureDurationMs", snapshot.durationMs)
            .put("pointCount", snapshot.points.size)
            .put("depthPointCount", snapshot.depthPointCount)
            .put("sparseFeaturePointCount", snapshot.sparsePointCount)
            .put("arcoreDepthApiEnabled", snapshot.depthSupported)
            .put("lastTrackingState", snapshot.trackingState)
            .put("safmusPointCloudOptimization", JSONObject()
                .put("method", "20mm voxel thinning; invalid coordinates rejected; output capped at 100000")
                .put("rejectedInvalidCount", snapshot.optimizationRejectedCount)
                .put("reducedByVoxelCount", snapshot.optimizationReducedCount)
                .put("truncatedCount", snapshot.optimizationTruncatedCount))
            .put("safmusAssessment", JSONObject().apply {
                val assessment = assessSafmus(snapshot)
                put("exportAllowed", assessment.exportAllowed)
                put("quality", assessment.quality)
                put("reason", assessment.reason)
                put("evidenceKind", assessment.evidenceKind)
                put("acousticCalibrationReady", assessment.acousticCalibrationReady)
            })
            .put("rp88AcousticCalibration", calibrationJson(snapshot))
            .put("coordinateSystem", "ARCore world coordinates; scale is an estimate from visual-inertial tracking")
            .put("boundsMeters", JSONObject()
                .put("minX", b[0]).put("minY", b[1]).put("minZ", b[2])
                .put("maxX", b[3]).put("maxY", b[4]).put("maxZ", b[5])
                .put("widthX", b[3] - b[0]).put("heightY", b[4] - b[1]).put("depthZ", b[5] - b[2]))
            .put("limitations", JSONArray()
                .put("Observed point-cloud bounds are not a certified room measurement.")
                .put("This capture is not a closed mesh or a photorealistic reconstruction.")
                .put("RT60 and Schroeder frequency in rp88AcousticCalibration are geometry/material-prior estimates, not measured values.")
                .put("No impulse response (RIR) or acoustic sweep has been measured.")
                .put("Surface texture, lighting, motion and device depth support affect quality."))
            .put("cameraPoses", JSONArray().apply {
                snapshot.poses.forEach { p ->
                    put(JSONObject().put("timeMs", p.timeMs).put("translation", JSONArray(listOf(p.x, p.y, p.z)))
                        .put("rotationQuaternionXYZW", JSONArray(listOf(p.qx, p.qy, p.qz, p.qw))))
                }
            })
        return json.toString(2).toByteArray(Charsets.UTF_8)
    }

    private fun estimateCalibration(snapshot: CaptureSnapshot): AcousticRoomCalibration? {
        if (snapshot.points.isEmpty()) return null
        val b = bounds(snapshot.points)
        val yawDeg = snapshot.poses.lastOrNull()?.let { p ->
            val siny = 2.0 * (p.qw.toDouble() * p.qy + p.qx.toDouble() * p.qz)
            val cosy = 1.0 - 2.0 * (p.qy.toDouble() * p.qy + p.qz.toDouble() * p.qz)
            Math.toDegrees(kotlin.math.atan2(siny, cosy))
        } ?: 0.0
        return AcousticRoomCalibrationModel.estimate(
            widthM = (b[3] - b[0]).toDouble(),
            heightM = (b[4] - b[1]).toDouble(),
            depthM = (b[5] - b[2]).toDouble(),
            material = AcousticRoomCalibrationModel.materials[selectedMaterialIndex],
            azimuthDeg = yawDeg
        )
    }

    private fun calibrationJson(snapshot: CaptureSnapshot): JSONObject {
        val estimate = estimateCalibration(snapshot)
        val result = estimate?.toJson() ?: JSONObject()
            .put("evidenceKind", "INSUFFICIENT_GEOMETRY")
            .put("measuredRirAvailable", false)
            .put("reason", "AR point-cloud bounds are empty or outside the supported physical range.")
        result.put("rp88LockRequested", latestCalibrationLockRequested)
            .put("spatialWidthReadbackConfirmed", latestCalibrationApplied)
            .put("rp88LockFullyConfirmed", false)
            .put("fullLockConfirmationUnavailable", "Spatial angle read-back confirms the finite clamped command only; measured acoustic response/RIR is unavailable, so a full physical lock is not claimed.")
            .put("physicalSceneReadbackConfirmed", latestPhysicalSceneReadbackConfirmed)
            .put("physicalSceneOcclusionReadback", if (latestOcclusionReadback.isFinite()) latestOcclusionReadback.toDouble() else JSONObject.NULL)
            .put("physicalSceneWallAbsorptionReadback", if (latestWallAbsorptionReadback.isFinite()) latestWallAbsorptionReadback.toDouble() else JSONObject.NULL)
            .put("physicalSceneWallAbsorptionReadbackConfirmed", estimate != null && latestWallAbsorptionReadback.isFinite() && kotlin.math.abs(latestWallAbsorptionReadback - estimate.absorptionCoefficient.toFloat()) <= 0.02f)
            .put("nativeLibraryAvailable", IvannaNativeLib.isLoaded && NativeBridge.isLoaded)
            .put("spatialWidthReadback", if (latestSpatialWidthReadback.isFinite()) latestSpatialWidthReadback.toDouble() else JSONObject.NULL)
            .put("spatialAngleCommandReadbackRad", if (latestSpatialAngleReadback.isFinite()) latestSpatialAngleReadback.toDouble() else JSONObject.NULL)
            .put("spatialAngleCommandReadbackConfirmed", estimate != null && latestSpatialAngleReadback.isFinite() && kotlin.math.abs(latestSpatialAngleReadback - Math.toRadians(estimate.spatialAngleDeg.toDouble()).toFloat()) <= 0.02f)
            .put("realityIntensityCommandReadback", if (latestRealityIntensityReadback.isFinite()) latestRealityIntensityReadback.toDouble() else JSONObject.NULL)
            .put("antiDolbyCommandReadback", if (latestAntiDolbyIntensityReadback.isFinite()) latestAntiDolbyIntensityReadback.toDouble() else JSONObject.NULL)
            .put("realityIntensityCommandReadbackConfirmed", latestRealityIntensityReadback.isFinite() && kotlin.math.abs(latestRealityIntensityReadback - 0.88f) <= 0.02f)
            .put("antiDolbyCommandReadbackConfirmed", latestAntiDolbyIntensityReadback.isFinite() && kotlin.math.abs(latestAntiDolbyIntensityReadback - 0.88f) <= 0.02f)
            .put("commandValues", JSONObject()
                .put("realityIntensity", 0.88)
                .put("antiDolbyIntensity", 0.88)
                .put("spatialWidth", estimate?.spatialWidth ?: JSONObject.NULL)
                .put("spatialAngleDeg", estimate?.spatialAngleDeg ?: JSONObject.NULL))
        return result
    }

    private fun lockRp88Calibration() {
        val snapshot = lastSnapshot
        val calibration = snapshot?.let { estimateCalibration(it) }
        if (snapshot == null || calibration == null) {
            statusView.text = "LOCK RP-88 rechazado: geometría insuficiente o no física. Captura más superficies."
            return
        }
        latestCalibrationLockRequested = true
        var applied = false
        var readback = Float.NaN
        var angleReadback = Float.NaN
        var realityReadback = Float.NaN
        var antiDolbyReadback = Float.NaN
        var occlusionReadback = Float.NaN
        var wallAbsorptionReadback = Float.NaN
        try {
            if (IvannaNativeLib.isLoaded && NativeBridge.isLoaded) {
                NativeBridge.safeSetRealityIntensity(0.88f)
                IvannaNativeLib.nativeSetAntiDolbyIntensity(0.88f)
                IvannaNativeLib.nativeSetSpatialWidthDirect(calibration.spatialWidth)
                IvannaNativeLib.nativeSetSpatialAngleRad(Math.toRadians(calibration.spatialAngleDeg.toDouble()).toFloat())
                NativeBridge.safeSetPhysicalSceneParams(
                    occlusionFactor = 0.0f,
                    wallAbsorption = calibration.absorptionCoefficient.toFloat()
                )
                readback = IvannaNativeLib.nativeGetSpatialWidthDirect()
                angleReadback = IvannaNativeLib.nativeGetSpatialAngleRad()
                realityReadback = NativeBridge.safeGetRealityIntensity()
                antiDolbyReadback = IvannaNativeLib.nativeGetAntiDolbyIntensity()
                NativeBridge.safeGetPhysicalSceneSnapshot()?.let { scene ->
                    if (scene.size >= 2) {
                        occlusionReadback = scene[0]
                        wallAbsorptionReadback = scene[1]
                    }
                }
                applied = readback.isFinite() && kotlin.math.abs(readback - calibration.spatialWidth) <= 0.15f
            }
        } catch (t: Throwable) {
            android.util.Log.w("IVANNA.RP88", "Native acoustic calibration command failed", t)
            applied = false
        }
        latestCalibration = calibration
        latestCalibrationApplied = applied
        latestSpatialWidthReadback = readback
        latestSpatialAngleReadback = angleReadback
        latestRealityIntensityReadback = realityReadback
        latestAntiDolbyIntensityReadback = antiDolbyReadback
        latestOcclusionReadback = occlusionReadback
        latestWallAbsorptionReadback = wallAbsorptionReadback
        latestPhysicalSceneReadbackConfirmed = occlusionReadback.isFinite() && wallAbsorptionReadback.isFinite() &&
            kotlin.math.abs(occlusionReadback) <= 0.02f &&
            kotlin.math.abs(wallAbsorptionReadback - calibration.absorptionCoefficient.toFloat()) <= 0.02f
        getSharedPreferences("ivanna_rp88_acoustic_calibration", MODE_PRIVATE).edit()
            .putString("calibration_json", calibration.toJson().toString())
            .putBoolean("lock_requested", true)
            .putBoolean("spatial_width_readback_confirmed", applied)
            .putBoolean("rp88_lock_fully_confirmed", false)
            .putFloat("spatial_width_readback", if (readback.isFinite()) readback else -1f)
            .putFloat("spatial_angle_command_readback_rad", if (angleReadback.isFinite()) angleReadback else -999f)
            .putBoolean("spatial_angle_command_readback_confirmed", angleReadback.isFinite() && kotlin.math.abs(angleReadback - Math.toRadians(calibration.spatialAngleDeg.toDouble()).toFloat()) <= 0.02f)
            .putFloat("reality_intensity_command_readback", if (realityReadback.isFinite()) realityReadback else -1f)
            .putFloat("anti_dolby_command_readback", if (antiDolbyReadback.isFinite()) antiDolbyReadback else -1f)
            .putFloat("physical_scene_occlusion_readback", if (occlusionReadback.isFinite()) occlusionReadback else -1f)
            .putFloat("physical_scene_wall_absorption_readback", if (wallAbsorptionReadback.isFinite()) wallAbsorptionReadback else -1f)
            .putBoolean("physical_scene_readback_confirmed", latestPhysicalSceneReadbackConfirmed)
            .putBoolean("reality_intensity_command_readback_confirmed", realityReadback.isFinite() && kotlin.math.abs(realityReadback - 0.88f) <= 0.02f)
            .putBoolean("anti_dolby_command_readback_confirmed", antiDolbyReadback.isFinite() && kotlin.math.abs(antiDolbyReadback - 0.88f) <= 0.02f)
            .putLong("updated_at_ms", System.currentTimeMillis())
            .apply()
        statusView.text = if (applied) {
            "RP-88 PARCIAL · ancho DSP=${String.format(Locale.US, "%.2f", readback)}, realidad DSP=${if (realityReadback.isFinite()) String.format(Locale.US, "%.2f", realityReadback) else "sin lectura"}, Anti-Dolby DSP=${if (antiDolbyReadback.isFinite()) String.format(Locale.US, "%.2f", antiDolbyReadback) else "sin lectura"}, absorción DSP=${if (latestPhysicalSceneReadbackConfirmed) String.format(Locale.US, "%.2f", wallAbsorptionReadback) else "sin lectura confirmada"}. Son valores de control, no medición acústica; RT60/RIR estimados."
        } else {
            "RP-88: estimación guardada, pero el motor nativo no confirmó el ancho solicitado. No se declara LOCK efectivo. Exporta rp88_calibration.json para inspección."
        }
        exportButton.isEnabled = snapshot.points.isNotEmpty()
    }

    private fun assessSafmus(snapshot: CaptureSnapshot): SafmusUniversalKernel.SpatialCaptureAssessment =
        SafmusUniversalKernel.assessSpatialCapture(
            pointCount = snapshot.points.size,
            depthPointCount = snapshot.depthPointCount,
            sparsePointCount = snapshot.sparsePointCount,
            poseCount = snapshot.poses.size,
            durationMs = snapshot.durationMs,
            trackingState = snapshot.trackingState,
            coordinatesFinite = snapshot.points.all { it.x.isFinite() && it.y.isFinite() && it.z.isFinite() },
            posesFinite = snapshot.poses.all { p ->
                listOf(p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw).all { it.isFinite() }
            }
        )

    private fun bounds(data: List<Point3>): FloatArray {
        if (data.isEmpty()) return floatArrayOf(0f, 0f, 0f, 0f, 0f, 0f)
        var minX = Float.POSITIVE_INFINITY; var minY = Float.POSITIVE_INFINITY; var minZ = Float.POSITIVE_INFINITY
        var maxX = Float.NEGATIVE_INFINITY; var maxY = Float.NEGATIVE_INFINITY; var maxZ = Float.NEGATIVE_INFINITY
        data.forEach { p ->
            if (p.x.isFinite() && p.y.isFinite() && p.z.isFinite()) {
                minX = minOf(minX, p.x); minY = minOf(minY, p.y); minZ = minOf(minZ, p.z)
                maxX = maxOf(maxX, p.x); maxY = maxOf(maxY, p.y); maxZ = maxOf(maxZ, p.z)
            }
        }
        if (!minX.isFinite()) return floatArrayOf(0f, 0f, 0f, 0f, 0f, 0f)
        return floatArrayOf(minX, minY, minZ, maxX, maxY, maxZ)
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        GLES20.glClearColor(0f, 0f, 0f, 1f)
        cameraTextureId = createExternalTexture()
        shaderProgram = createProgram(VERTEX_SHADER, FRAGMENT_SHADER)
        positionHandle = GLES20.glGetAttribLocation(shaderProgram, "aPosition")
        texCoordHandle = GLES20.glGetAttribLocation(shaderProgram, "aTexCoord")
        textureHandle = GLES20.glGetUniformLocation(shaderProgram, "sTexture")
        textureSession = null
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        GLES20.glViewport(0, 0, width, height)
        viewportWidth = width
        viewportHeight = height
        viewportChanged = true
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        viewportChanged = true
    }

    override fun onDrawFrame(gl: GL10?) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        val current = session ?: return
        try {
            if (textureSession !== current) {
                current.setCameraTextureNames(intArrayOf(cameraTextureId))
                textureSession = current
            }
            if (viewportChanged && viewportWidth > 0 && viewportHeight > 0) {
                @Suppress("DEPRECATION")
                val rotation = windowManager.defaultDisplay.rotation
                current.setDisplayGeometry(rotation, viewportWidth, viewportHeight)
                viewportChanged = false
            }
            val frame = current.update()
            drawCameraBackground(frame)
            framesRendered++
            val camera = frame.camera
            lastTrackingState = camera.trackingState.name
            if (captureEnabled.get() && camera.trackingState == TrackingState.TRACKING) {
                val nowNs = frame.timestamp
                if (nowNs != 0L && nowNs - lastPoseSampleNs >= 250_000_000L) {
                    collectPose(camera)
                    lastPoseSampleNs = nowNs
                }
                if (nowNs != 0L && nowNs - lastDepthSampleNs >= 350_000_000L) {
                    val gotDepth = if (sessionConfiguredDepth) collectDepth(frame, camera) else false
                    if (!gotDepth) collectSparsePoints(frame)
                    lastDepthSampleNs = nowNs
                }
            }
            val nowMs = SystemClock.elapsedRealtime()
            if (nowMs - lastUiStatusMs > 650L) {
                lastUiStatusMs = nowMs
                val count = synchronized(pointsLock) { points.size }
                val poseHud = poseHud(camera)
                runOnUiThread {
                    if (captureEnabled.get()) statusView.text =
                        "CAPTURANDO · seguimiento ${camera.trackingState.name} · puntos $count\n6-DoF: $poseHud\nBarre despacio; evita giros bruscos y superficies sin textura."
                }
            }
        } catch (e: Exception) {
            // Antes solo se mostraba durante la captura (captureEnabled); un fallo ANTES de pulsar
            // "Iniciar captura" quedaba totalmente invisible (pantalla en blanco sin ninguna pista).
            val nowMs = SystemClock.elapsedRealtime()
            if (nowMs - lastErrorUiMs > 500L) {
                lastErrorUiMs = nowMs
                val msg = e.message ?: e.javaClass.simpleName
                runOnUiThread {
                    statusView.text = if (captureEnabled.get())
                        "Seguimiento temporalmente interrumpido: $msg"
                    else
                        "Cámara sin imagen: $msg\nCierra otras apps que usen la cámara y vuelve a intentar."
                }
            }
        }
    }

    /** Diagnóstico de "pantalla en blanco silenciosa": ARCore entrega frames sin lanzar excepción pero
     *  la escena nunca se dibuja (p. ej. la textura externa nunca llega a vincularse a un frame real). */
    private fun checkStuckPreview() {
        val startedNs = resumedAtNs
        if (startedNs == 0L || framesRendered > 0L) return
        if (SystemClock.elapsedRealtimeNanos() - startedNs < 1_500_000_000L) return
        if (!captureEnabled.get()) runOnUiThread {
            statusView.text = "La cámara no entrega imagen (0 fotogramas en 1.5 s). Reabre la pantalla o reinicia la app."
        }
    }

    /** Live 6-DoF pose HUD; coordinates are ARCore world-frame estimates, not survey-grade measurements. */
    private fun poseHud(camera: Camera): String {
        val pose = camera.pose
        val t = pose.translation
        val q = pose.rotationQuaternion
        val x = q[0].toDouble(); val y = q[1].toDouble()
        val z = q[2].toDouble(); val w = q[3].toDouble()
        val roll = Math.toDegrees(kotlin.math.atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)))
        val pitchArg = (2.0 * (w * y - z * x)).coerceIn(-1.0, 1.0)
        val pitch = Math.toDegrees(kotlin.math.asin(pitchArg))
        val yaw = Math.toDegrees(kotlin.math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)))
        return String.format(Locale.US, "x %.2f y %.2f z %.2f m · yaw %.0f° pitch %.0f° roll %.0f°", t[0], t[1], t[2], yaw, pitch, roll)
    }

    private fun collectPose(camera: Camera) {
        val pose = camera.pose
        val t = pose.translation
        val q = pose.rotationQuaternion
        val elapsed = ((SystemClock.elapsedRealtimeNanos() - captureStartedNs).coerceAtLeast(0L)) / 1_000_000L
        val poseValues = listOf(t[0], t[1], t[2], q[0], q[1], q[2], q[3])
        val quaternionNorm = kotlin.math.sqrt(q.sumOf { (it * it).toDouble() }).toFloat()
        if (poseValues.any { !it.isFinite() } || !quaternionNorm.isFinite() || quaternionNorm < 0.5f || quaternionNorm > 1.5f) return
        synchronized(pointsLock) {
            if (poseSamples.size < MAX_POSES) poseSamples.add(PoseSample(elapsed, t[0], t[1], t[2], q[0], q[1], q[2], q[3]))
        }
    }

    private fun collectDepth(frame: Frame, camera: Camera): Boolean {
        val image = try { frame.acquireDepthImage16Bits() } catch (_: NotYetAvailableException) { return false } catch (_: Exception) { return false }
        image.use { depth ->
            val plane = depth.planes[0]
            val buffer = plane.buffer.order(ByteOrder.LITTLE_ENDIAN)
            val width = depth.width
            val height = depth.height
            val intrinsics = camera.imageIntrinsics
            val dims = intrinsics.imageDimensions
            if (dims.size < 2 || dims[0] <= 0 || dims[1] <= 0) return false
            val scaleX = width.toFloat() / dims[0]
            val scaleY = height.toFloat() / dims[1]
            val focal = intrinsics.focalLength
            val principal = intrinsics.principalPoint
            val fx = focal[0] * scaleX
            val fy = focal[1] * scaleY
            val cx = principal[0] * scaleX
            val cy = principal[1] * scaleY
            if (fx <= 0f || fy <= 0f) return false
            val step = maxOf(10, maxOf(width, height) / 70)
            val local = ArrayList<Point3>(minOf((width / step) * (height / step), 5_000))
            var y = step / 2
            while (y < height) {
                var x = step / 2
                while (x < width) {
                    val offset = y * plane.rowStride + x * plane.pixelStride
                    if (offset + 1 < buffer.limit()) {
                        val millimetres = buffer.getShort(offset).toInt() and 0xFFFF
                        if (millimetres in 150..10_000) {
                            val d = millimetres / 1000f
                            val world = camera.pose.transformPoint(floatArrayOf((x - cx) * d / fx, (cy - y) * d / fy, -d))
                            if (world.all { it.isFinite() }) local.add(Point3(world[0], world[1], world[2]))
                        }
                    }
                    x += step
                }
                y += step
            }
            synchronized(pointsLock) {
                val take = minOf(MAX_POINTS - points.size, local.size)
                for (i in 0 until take) points.add(local[i])
                depthPointCount += take
            }
            return local.isNotEmpty()
        }
    }

    private fun collectSparsePoints(frame: Frame) {
        val cloud: PointCloud = try { frame.acquirePointCloud() } catch (_: Exception) { return }
        cloud.use {
            val data = it.points
            val local = ArrayList<Point3>(minOf(data.remaining() / 4, 1_500))
            var i = 0
            while (data.remaining() >= 4 && i < 6_000) {
                val x = data.get(); val y = data.get(); val z = data.get()
                data.get() // confidence
                if (x.isFinite() && y.isFinite() && z.isFinite()) local.add(Point3(x, y, z))
                i++
            }
            synchronized(pointsLock) {
                val take = minOf(MAX_POINTS - points.size, local.size)
                for (i in 0 until take) points.add(local[i])
                sparsePointCount += take
            }
        }
    }

    private fun drawCameraBackground(frame: Frame) {
        quadVertices.position(0); cameraTexCoords.position(0)
        frame.transformCoordinates2d(Coordinates2d.OPENGL_NORMALIZED_DEVICE_COORDINATES, quadVertices,
            Coordinates2d.TEXTURE_NORMALIZED, cameraTexCoords)
        quadVertices.position(0); cameraTexCoords.position(0)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(shaderProgram)
        GLES20.glEnableVertexAttribArray(positionHandle)
        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, quadVertices)
        GLES20.glEnableVertexAttribArray(texCoordHandle)
        GLES20.glVertexAttribPointer(texCoordHandle, 2, GLES20.GL_FLOAT, false, 0, cameraTexCoords)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, cameraTextureId)
        GLES20.glUniform1i(textureHandle, 0)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(positionHandle)
        GLES20.glDisableVertexAttribArray(texCoordHandle)
    }

    private fun createExternalTexture(): Int {
        val textures = IntArray(1)
        GLES20.glGenTextures(1, textures, 0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textures[0])
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        return textures[0]
    }

    private fun createProgram(vertexSource: String, fragmentSource: String): Int {
        val vertex = compileShader(GLES20.GL_VERTEX_SHADER, vertexSource)
        val fragment = compileShader(GLES20.GL_FRAGMENT_SHADER, fragmentSource)
        val program = GLES20.glCreateProgram()
        GLES20.glAttachShader(program, vertex); GLES20.glAttachShader(program, fragment); GLES20.glLinkProgram(program)
        val status = IntArray(1)
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, status, 0)
        check(status[0] == GLES20.GL_TRUE) { "No se pudo enlazar el shader de cámara" }
        GLES20.glDeleteShader(vertex); GLES20.glDeleteShader(fragment)
        return program
    }

    private fun compileShader(type: Int, source: String): Int {
        val shader = GLES20.glCreateShader(type)
        GLES20.glShaderSource(shader, source); GLES20.glCompileShader(shader)
        val status = IntArray(1)
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, status, 0)
        check(status[0] == GLES20.GL_TRUE) { GLES20.glGetShaderInfoLog(shader) ?: "Error de shader" }
        return shader
    }

    private fun floatBuffer(values: FloatArray): FloatBuffer =
        ByteBuffer.allocateDirect(values.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(values); position(0) }

    companion object {
        private const val REQUEST_CAMERA = 7201
        private const val REQUEST_EXPORT = 7202
        private const val MAX_POINTS = 100_000
        private const val MAX_POSES = 1_200
        private const val VERTEX_SHADER = """
            attribute vec4 aPosition;
            attribute vec2 aTexCoord;
            varying vec2 vTexCoord;
            void main() { gl_Position = aPosition; vTexCoord = aTexCoord; }
        """
        private const val FRAGMENT_SHADER = """
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            uniform samplerExternalOES sTexture;
            varying vec2 vTexCoord;
            void main() { gl_FragColor = texture2D(sTexture, vTexCoord); }
        """
    }
}
