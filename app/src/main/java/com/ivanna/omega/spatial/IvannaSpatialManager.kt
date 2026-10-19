package com.ivanna.omega.spatial

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * IvannaSpatialManager — singleton que gestiona el ObjectRenderer nativo
 * con HRTFs medidos (CIPIC). Reemplaza IvannaSpatialEngine.processStereoInput()
 * en el hot-path con convolución HRTF real (Overlap-Save + crossfade).
 *
 * Uso:
 *   // En IVANNAApplication.onCreate():
 *   IvannaSpatialManager.init(applicationContext)
 *
 *   // En PlaybackCaptureService processingLoop:
 *   if (IvannaSpatialManager.ready) {
 *       IvannaSpatialManager.renderBlock(buffer, frames, SAMPLE_RATE)
 *   }
 */
object IvannaSpatialManager {

    private const val TAG = "IVANNA.SpatialMgr"
    private const val SAMPLE_RATE = 48000
    private const val BLOCK_SIZE  = 512   // debe coincidir con BLOCK en hrtf_convolver.hpp

    @Volatile internal var rendererHandle: Long = 0L
    @Volatile internal var upmixerHandle: Long = 0L
    private var headTracker: IvannaHeadTracker? = null
    @Volatile var ready: Boolean = false
        private set
    @Volatile var activeSubject: String = "none"
        private set
    @Volatile var hrtfStatus: String = "NO CONFIRMADO"
        private set
    @Volatile var reverbLevel: Float = 0.25f
        private set
    private val upmixerEnabledAtomic = java.util.concurrent.atomic.AtomicBoolean(true)
    val isUpmixerEnabled: Boolean
        get() = upmixerEnabledAtomic.get()
    private var upmixXfadeGain: Float = 1.0f

    // Desired scene pose is retained before native initialization and applied to
    // the four active stems when the upmixer becomes available.
    private data class RequestedScenePose(
        val azimuthRad: Float = 0f,
        val elevationRad: Float = 0f,
        val distanceScale: Float = 1f
    )
    private val requestedScenePose = java.util.concurrent.atomic.AtomicReference(RequestedScenePose())
    @Volatile private var lastAppliedScenePose: RequestedScenePose? = null
    @Volatile private var lastAppliedScenePoseAtEpochMs: Long = 0L
    @Volatile private var lastScenePoseWriteConfirmed: Boolean = false

    data class ScenePoseStatus(
        val requestedAzimuthRad: Float,
        val requestedElevationRad: Float,
        val requestedDistanceScale: Float,
        val effectiveAzimuthRad: Float?,
        val effectiveElevationRad: Float?,
        val effectiveDistanceScale: Float?,
        val engineConfirmed: Boolean,
        val updatedAtEpochMs: Long
    )

    data class ReverbReadback(
        val requestedLevel: Float,
        val effectiveLevel: Float?,
        val engineConfirmed: Boolean,
        val measuredRirActive: Boolean,
        val measuredRt60Seconds: Float?
    )

    private val baseStemPositions = SpatialScenePoseMath.defaultStems

    fun scenePoseStatus(): ScenePoseStatus {
        val requested = requestedScenePose.get()
        val applied = lastAppliedScenePose
        val confirmed = lastScenePoseWriteConfirmed && applied != null &&
            applied == requested && ready && rendererHandle != 0L && upmixerHandle != 0L
        return ScenePoseStatus(
            requestedAzimuthRad = requested.azimuthRad,
            requestedElevationRad = requested.elevationRad,
            requestedDistanceScale = requested.distanceScale,
            effectiveAzimuthRad = applied?.azimuthRad,
            effectiveElevationRad = applied?.elevationRad,
            effectiveDistanceScale = applied?.distanceScale,
            engineConfirmed = confirmed,
            updatedAtEpochMs = lastAppliedScenePoseAtEpochMs
        )
    }

    fun isHrtfConfirmed(): Boolean {
        val h = rendererHandle
        if (!ready || h == 0L || activeSubject == "none") return false
        return runCatching {
            IvannaSpatialNative.nativeObjectRendererIsHrtfLoaded(h) &&
                IvannaSpatialNative.nativeObjectRendererGetCurrentSubject(h) == activeSubject
        }.getOrDefault(false)
    }

    fun isHeuristicStemSeparationConfirmed(): Boolean {
        val u = upmixerHandle
        if (!ready || u == 0L) return false
        return runCatching { IvannaSpatialNative.nativeUpmixerIsEnabled(u) }.getOrDefault(false)
    }

    /**
     * True only if the same ObjectRenderer handle used by renderBlock has
     * completed non-empty blocks recently. Settings/read-back alone do not
     * prove that playback is reaching this engine.
     */
    fun isRenderPathActive(maxStaleMs: Long = 1_500L): Boolean {
        val h = rendererHandle
        val u = upmixerHandle
        if (!ready || h == 0L || u == 0L) return false
        val count = runCatching {
            IvannaSpatialNative.nativeObjectRendererGetProcessedBlockCount(h)
        }.getOrDefault(0L)
        val now = SystemClock.elapsedRealtime()
        val previous = lastObservedRenderBlockCount.getAndSet(count)
        if (count > previous) lastRenderProgressElapsedMs.set(now)
        val last = lastRenderProgressElapsedMs.get()
        return count > 0L && last > 0L && now >= last && now - last <= maxStaleMs
    }

    /** true only when the Android rotation-vector sensor is registered and fresh data reached JNI. */
    fun isHeadTrackingFresh(): Boolean = headTracker?.isTrackingFresh == true

    fun reverbReadback(): ReverbReadback {
        val h = rendererHandle
        val expected = reverbLevel
        if (!ready || h == 0L) {
            return ReverbReadback(expected, null, false, false, null)
        }
        return runCatching {
            val actual = IvannaSpatialNative.nativeObjectRendererGetReverbLevel(h)
            val rirActive = IvannaSpatialNative.nativeObjectRendererIsRirActive(h)
            val rt60 = IvannaSpatialNative.nativeObjectRendererGetRoomRT60(h)
            val finiteRt60 = rt60.takeIf { it.isFinite() && it > 0f }
            ReverbReadback(
                requestedLevel = expected,
                effectiveLevel = actual.takeIf { it.isFinite() && it in 0f..1f },
                engineConfirmed = actual.isFinite() && kotlin.math.abs(actual - expected) <= 0.01f,
                measuredRirActive = rirActive,
                measuredRt60Seconds = finiteRt60
            )
        }.getOrElse {
            ReverbReadback(expected, null, false, false, null)
        }
    }

    // Buffers para el hot-path: direct FloatBuffers reutilizables (sin allocations por frame)
    private val stereoInBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * 2 * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    private val stemsOutBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * 8 * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    // ObjectRenderer expects object-major planar stereo:
    // [object0-L frames, object0-R frames, object1-L frames, object1-R frames, ...].
    // NeuralUpmixer emits frame-major interleaved stems, so renderBlock converts
    // into this separate direct buffer before crossing JNI.
    private val objectsInBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * 8 * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    private val inLBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    private val inRBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    private val outLBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    private val outRBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()

    // Arrays auxiliares para reinterleaving (evitan acceder a FloatBuffer por índice)
    private val outL = FloatArray(BLOCK_SIZE)
    private val outR = FloatArray(BLOCK_SIZE)

    private val lock = Any()

    // Native render path progress is sampled from control/UI threads, never
    // polled from the real-time audio callback.
    private val lastObservedRenderBlockCount = java.util.concurrent.atomic.AtomicLong(-1L)
    private val lastRenderProgressElapsedMs = java.util.concurrent.atomic.AtomicLong(0L)

    private val movementLock = Any()
    private val movementGeneration = java.util.concurrent.atomic.AtomicLong(0L)
    @Volatile var sceneMovementActive: Boolean = false
        private set
    @Volatile private var lastMovementTickConfirmed: Boolean = false
    @Volatile private var lastMovementTickAtEpochMs: Long = 0L
    private var movementThread: HandlerThread? = null
    private var movementHandler: Handler? = null
    private var movementRunnable: Runnable? = null

    data class MovementStatus(
        val active: Boolean,
        val latestTickConfirmed: Boolean,
        val updatedAtEpochMs: Long
    )

    fun movementStatus(): MovementStatus = MovementStatus(
        active = sceneMovementActive,
        latestTickConfirmed = lastMovementTickConfirmed,
        updatedAtEpochMs = lastMovementTickAtEpochMs
    )

    /**
     * Moves the entire 4-stem scene along a bounded constant-velocity trajectory.
     * Updates run on a dedicated control thread (never the audio callback) and each
     * tick is applied through the same native coordinate read-back as static poses.
     */
    fun startSceneMovement(
        azimuthVelocityRadPerSec: Float,
        elevationVelocityRadPerSec: Float,
        distanceScaleVelocityPerSec: Float = 0f
    ): Boolean {
        if (!azimuthVelocityRadPerSec.isFinite() ||
            !elevationVelocityRadPerSec.isFinite() ||
            !distanceScaleVelocityPerSec.isFinite()) return false
        if (kotlin.math.abs(azimuthVelocityRadPerSec) > 1.5f ||
            kotlin.math.abs(elevationVelocityRadPerSec) > 0.5f ||
            kotlin.math.abs(distanceScaleVelocityPerSec) > 0.25f) return false
        if (!ready || rendererHandle == 0L || upmixerHandle == 0L) return false
        if (kotlin.math.abs(azimuthVelocityRadPerSec) < 1e-4f &&
            kotlin.math.abs(elevationVelocityRadPerSec) < 1e-4f &&
            kotlin.math.abs(distanceScaleVelocityPerSec) < 1e-4f) return false

        stopSceneMovement()
        val base = requestedScenePose.get()
        val startedAtNs = SystemClock.elapsedRealtimeNanos()
        val generation = movementGeneration.incrementAndGet()
        val thread = HandlerThread("Ivanna10DSceneMotion").also { it.start() }
        val handler = Handler(thread.looper)
        val task = object : Runnable {
            override fun run() {
                if (!sceneMovementActive || generation != movementGeneration.get()) return
                val seconds = (SystemClock.elapsedRealtimeNanos() - startedAtNs) / 1_000_000_000f
                val azimuth = (base.azimuthRad + azimuthVelocityRadPerSec * seconds)
                    .coerceIn(-Math.PI.toFloat(), Math.PI.toFloat())
                val elevation = (base.elevationRad + elevationVelocityRadPerSec * seconds)
                    .coerceIn(-Math.PI.toFloat() / 4f, Math.PI.toFloat() / 4f)
                val distance = (base.distanceScale + distanceScaleVelocityPerSec * seconds)
                    .coerceIn(0.5f, 2f)

                val confirmed = setScenePose(azimuth, elevation, distance)
                lastMovementTickConfirmed = confirmed
                lastMovementTickAtEpochMs = System.currentTimeMillis()

                val hitAzimuthLimit =
                    (azimuthVelocityRadPerSec > 0f && azimuth >= Math.PI.toFloat()) ||
                    (azimuthVelocityRadPerSec < 0f && azimuth <= -Math.PI.toFloat())
                val hitElevationLimit =
                    (elevationVelocityRadPerSec > 0f && elevation >= Math.PI.toFloat() / 4f) ||
                    (elevationVelocityRadPerSec < 0f && elevation <= -Math.PI.toFloat() / 4f)
                val hitDistanceLimit =
                    (distanceScaleVelocityPerSec > 0f && distance >= 2f) ||
                    (distanceScaleVelocityPerSec < 0f && distance <= 0.5f)

                if (hitAzimuthLimit || hitElevationLimit || hitDistanceLimit) {
                    stopSceneMovement()
                } else if (sceneMovementActive && generation == movementGeneration.get()) {
                    handler.postDelayed(this, 20L)
                }
            }
        }
        synchronized(movementLock) {
            movementThread = thread
            movementHandler = handler
            movementRunnable = task
            lastMovementTickConfirmed = false
            lastMovementTickAtEpochMs = 0L
            sceneMovementActive = true
            handler.post(task)
        }
        return true
    }

    fun stopSceneMovement() {
        synchronized(movementLock) {
            movementGeneration.incrementAndGet()
            sceneMovementActive = false
            movementRunnable?.let { movementHandler?.removeCallbacks(it) }
            movementRunnable = null
            movementHandler = null
            movementThread?.quitSafely()
            movementThread = null
            lastMovementTickConfirmed = false
        }
    }

    // ── Inicialización ────────────────────────────────────────────────────────
    /** Motivo del último fallo de inicialización (null si no hubo o si ya está listo). La UI lo muestra. */
    @Volatile var initError: String? = null
        private set
    private val initInFlight = java.util.concurrent.atomic.AtomicBoolean(false)

    /**
     * Idempotente: una segunda llamada mientras hay una en curso, o cuando ya está listo, no hace
     * nada; tras un fallo se puede reintentar. Antes un fallo dejaba `ready=false` PARA SIEMPRE y
     * sin motivo visible ("No inicializado · sujeto none"): el hilo solo capturaba `Exception`
     * (un `UnsatisfiedLinkError`/`LinkageError` lo mataba en silencio), y el head tracker y el
     * upmixer —opcionales— podían impedir que el renderer, que ya existía, se publicara como listo.
     */
    fun init(context: Context, headWidthMm: Double? = null,
             headDepthMm: Double? = null, sex: String? = null) {
        if (ready || !initInFlight.compareAndSet(false, true)) return
        Thread(Runnable {
            var handle = 0L
            try {
                if (!IvannaSpatialNative.isLoaded) {
                    initError = "biblioteca nativa no cargada"
                    Log.e(TAG, "init: biblioteca nativa no cargada"); return@Runnable
                }
                handle = IvannaSpatialNative.nativeObjectRendererCreate(
                    SAMPLE_RATE.toFloat(), BLOCK_SIZE)
                if (handle != 0L) {
                    // Initialize the native wet value from the value the UI reports.
                    // Otherwise the first read-back would compare 0.25 (Kotlin) to
                    // the renderer's 0.30 native default and truthfully fail.
                    IvannaSpatialNative.nativeObjectRendererSetReverb(handle, reverbLevel)
                }
                if (handle == 0L) {
                    initError = "nativeObjectRendererCreate devolvió 0"
                    Log.e(TAG, "nativeObjectRendererCreate devolvió 0"); return@Runnable
                }

                // Opcionales: su fallo degrada (sin head tracking / sin upmix) pero NO impide el renderer.
                val tracker = IvannaHeadTracker(context)
                runCatching {
                    tracker.init()
                    tracker.start()
                    IvannaSpatialNative.nativeObjectRendererSetHeadTracker(handle, tracker.nativeHandle)
                }.onFailure { Log.w(TAG, "head tracker no disponible (sin seguimiento de cabeza): ${it.message}") }

                // Inicializar NeuralUpmixer (4 stems) y sincronizar objetos activos [FIX-SILENCE]
                val upmixer = runCatching {
                    IvannaSpatialNative.nativeUpmixerCreate("", SAMPLE_RATE.toFloat(), BLOCK_SIZE)
                }.getOrDefault(0L)
                if (upmixer != 0L) {
                    runCatching {
                        IvannaSpatialNative.nativeUpmixerSetEnabled(upmixer, true)
                    }.onFailure { Log.w(TAG, "upmixer no pudo activarse: ${it.message}") }
                }

                val requestedSubject = runCatching {
                    HrtfSubjectSelector.activate(context, handle, headWidthMm, headDepthMm, sex)
                }.getOrElse {
                    Log.w(TAG, "selección de sujeto HRTF falló: ${it.message}")
                    HrtfSubjectSelector.DEFAULT_SUBJECT
                }
                val loaded = runCatching {
                    IvannaSpatialNative.nativeObjectRendererIsHrtfLoaded(handle)
                }.getOrDefault(false)
                val actualSubject = runCatching {
                    IvannaSpatialNative.nativeObjectRendererGetCurrentSubject(handle)
                }.getOrDefault("none")
                val confirmedSubject = if (loaded && actualSubject == requestedSubject) actualSubject else "none"
                if (confirmedSubject == "none") {
                    Log.w(TAG, "HRTF no confirmado: solicitado=$requestedSubject, activo=$actualSubject, loaded=$loaded")
                }
                synchronized(lock) {
                    // Apply and verify a coherent initial scene before exposing the engine as ready.
                    val initialPose = requestedScenePose.get()
                    val poseConfirmed = if (upmixer != 0L) runCatching {
                        val accepted = applyScenePoseToUpmixer(
                            upmixer, initialPose.azimuthRad,
                            initialPose.elevationRad, initialPose.distanceScale
                        )
                        if (accepted) IvannaSpatialNative.nativeObjectRendererSyncStemObjects(handle, upmixer)
                        accepted && verifyRendererPose(
                            handle, initialPose.azimuthRad,
                            initialPose.elevationRad, initialPose.distanceScale
                        )
                    }.getOrDefault(false) else false

                    if (poseConfirmed) {
                        lastAppliedScenePose = initialPose
                        lastAppliedScenePoseAtEpochMs = System.currentTimeMillis()
                    } else {
                        lastAppliedScenePose = null
                        lastAppliedScenePoseAtEpochMs = 0L
                        Log.w(TAG, "pose espacial inicial no confirmada; se mantiene pendiente")
                    }
                    lastScenePoseWriteConfirmed = poseConfirmed
                    rendererHandle = handle
                    upmixerHandle = upmixer
                    headTracker = tracker
                    activeSubject = confirmedSubject
                    hrtfStatus = if (confirmedSubject != "none") "CARGADO · $confirmedSubject" else "NO CARGADO / NO CONFIRMADO"
                    ready = true
                }
                initError = null
                Log.i(TAG, "Spatial manager listo — HRTF: $hrtfStatus")
            } catch (t: Throwable) {   // Throwable: los fallos de JNI son Error, no Exception
                initError = "${t.javaClass.simpleName}: ${t.message}"
                Log.e(TAG, "Error init SpatialManager", t)
                if (handle != 0L && !ready) runCatching { IvannaSpatialNative.nativeObjectRendererDestroy(handle) }
            } finally {
                initInFlight.set(false)
            }
        }, "IvannaSpatialMgrInit").start()
    }

    // ── FASE 2: control HRTF desde UI (flujo Compose→Manager→JNI→DSP) ────
    fun setHrtfSubject(subjectId: String): Boolean {
        val h = rendererHandle
        if (!ready || h == 0L) return false
        // FIX (auditoría 2026-08-26): el JNI construye la ruta literal
        // ".../hrtf/" + subj + ".ihr1" SIN normalizar. Una etiqueta legible
        // ("MIT KEMAR", "CIPIC", "TU-Berlin") enviada desde un panel hacía
        // fallar loadHrtfDatasetFromFile en las 3 rutas candidatas, en
        // silencio — el usuario veía el sujeto seleccionado pero el motor
        // seguía con el dataset anterior. Normalizamos SIEMPRE al ID de
        // archivo real antes de cruzar el JNI.
        val resolved = HrtfSubjectSelector.resolveSubjectId(subjectId)
        if (resolved == activeSubject && isHrtfDatasetLoaded()) {
            return true
        }
        return runCatching {
            IvannaSpatialNative.nativeObjectRendererSetHrtfSubject(h, resolved)
            val loaded = IvannaSpatialNative.nativeObjectRendererIsHrtfLoaded(h)
            val actual = IvannaSpatialNative.nativeObjectRendererGetCurrentSubject(h)
            if (!loaded || actual != resolved) {
                hrtfStatus = "NO CARGADO / NO CONFIRMADO"
                Log.w(TAG, "HRTF no confirmado: solicitado=$resolved, activo=$actual, loaded=$loaded")
                false
            } else {
                activeSubject = actual
                hrtfStatus = "CARGADO · $actual"
                true
            }
        }.getOrDefault(false)
    }

    /** Estado REAL del motor — no el último comando enviado. */
    fun isHrtfDatasetLoaded(): Boolean {
        val h = rendererHandle
        if (!ready || h == 0L) return false
        return runCatching { IvannaSpatialNative.nativeObjectRendererIsHrtfLoaded(h) }.getOrDefault(false)
    }

    fun currentHrtfSubject(): String {
        val h = rendererHandle
        if (!ready || h == 0L) return "none"
        return runCatching { IvannaSpatialNative.nativeObjectRendererGetCurrentSubject(h) }.getOrDefault(activeSubject)
    }

    /**
     * FASE 7 (control muerto): el checkbox "Activar Calibración" de
     * Phase7Screen sólo movía un mutableStateOf local — AutoEqManager
     * estaba importado pero nunca se llamaba, así que el AutoEQ nativo
     * jamás se activaba. Este es el puente que faltaba: UI → Manager →
     * AutoEqManager → JNI (nativeObjectRendererSetAutoEqEnabled/Band).
     *
     * Devuelve false si el renderer aún no está listo, para que la UI
     * pueda decir la verdad en vez de fingir que se aplicó.
     */
    fun setAutoEq(enabled: Boolean, profileName: String): Boolean {
        val h = rendererHandle
        if (!ready || h == 0L) return false
        return runCatching {
            if (enabled) AutoEqManager.applyProfile(h, profileName)
            else AutoEqManager.disable(h)
            true
        }.getOrDefault(false)
    }

    /**
     * AUDIT FIX (ABX Static vs Dynamic HRTF): control de head tracking sin
     * reiniciar el motor. pauseHeadTracking() congela la pose (el renderer
     * sigue con la última orientación conocida — HRTF estático);
     * resumeHeadTracking() reanuda el flujo de la IMU (HRTF dinámico).
     * IvannaHeadTracker.stop()/start() desuscriben/resuscriben el listener
     * del sensor; el renderer nativo nunca se destruye — el audio no se
     * interrumpe. Usado por AbxTestScreen.setScenario().
     */
    fun pauseHeadTracking() {
        headTracker?.stop()
    }

    fun resumeHeadTracking() {
        headTracker?.start()
    }

    fun setReverbLevel(level: Float) {
        if (!level.isFinite()) return
        val clamped = level.coerceIn(0f, 1f)
        reverbLevel = clamped
        val h = rendererHandle
        if (ready && h != 0L) {
            runCatching { IvannaSpatialNative.nativeObjectRendererSetReverb(h, clamped) }
        }
    }

    fun setUpmixerEnabled(enabled: Boolean) {
        upmixerEnabledAtomic.set(enabled)
        val u = upmixerHandle
        if (ready && u != 0L) {
            runCatching { IvannaSpatialNative.nativeUpmixerSetEnabled(u, enabled) }
        }
    }

    fun setStemPosition(stemType: Int, x: Float, y: Float, z: Float, width: Float) {
        synchronized(lock) {
            val h = rendererHandle
            val u = upmixerHandle
            if (ready && h != 0L && u != 0L && stemType in 0..3) {
                runCatching {
                    IvannaSpatialNative.nativeUpmixerSetStemPosition(u, stemType, x, y, z, width)
                    IvannaSpatialNative.nativeObjectRendererSyncStemObjects(h, u)
                    lastScenePoseWriteConfirmed = false
                }
            }
        }
    }

    /**
     * Rotate and scale the four heuristic stems as one scene, preserving their
     * relative layout. Success means every native position was read back and
     * matched; merely sending JNI commands is not treated as application.
     *
     * Coordinates follow the renderer's x/y horizontal plane and z vertical axis.
     * This controls source placement; it does not claim measured HRTF elevation.
     */
    fun setScenePose(
        azimuthRad: Float,
        elevationRad: Float,
        distanceScale: Float = requestedScenePose.get().distanceScale
    ): Boolean {
        if (!azimuthRad.isFinite() || !elevationRad.isFinite() || !distanceScale.isFinite()) return false
        if (azimuthRad !in -Math.PI.toFloat()..Math.PI.toFloat()) return false
        if (elevationRad !in -Math.PI.toFloat() / 4f..Math.PI.toFloat() / 4f) return false
        if (distanceScale !in 0.5f..2f) return false

        // Store one coherent request even if native initialization has not finished.
        requestedScenePose.set(RequestedScenePose(azimuthRad, elevationRad, distanceScale))
        return synchronized(lock) {
            val h = rendererHandle
            val u = upmixerHandle
            if (!ready || h == 0L || u == 0L) {
                lastScenePoseWriteConfirmed = false
                return@synchronized false
            }
            val poseToApply = requestedScenePose.get()
            val success = runCatching {
                val accepted = applyScenePoseToUpmixer(
                    u, poseToApply.azimuthRad, poseToApply.elevationRad, poseToApply.distanceScale
                )
                if (!accepted) return@runCatching false
                IvannaSpatialNative.nativeObjectRendererSyncStemObjects(h, u)
                verifyRendererPose(
                    h, poseToApply.azimuthRad, poseToApply.elevationRad, poseToApply.distanceScale
                )
            }.getOrDefault(false)
            if (success) {
                lastAppliedScenePose = poseToApply
                lastAppliedScenePoseAtEpochMs = System.currentTimeMillis()
            }
            lastScenePoseWriteConfirmed = success && requestedScenePose.get() == poseToApply
            lastScenePoseWriteConfirmed
        }
    }

    private fun verifyRendererPose(
        renderer: Long,
        azimuthRad: Float,
        elevationRad: Float,
        distanceScale: Float
    ): Boolean {
        if (renderer == 0L) return false
        for (index in baseStemPositions.indices) {
            val expected = SpatialScenePoseMath.transform(
                baseStemPositions[index], azimuthRad, elevationRad, distanceScale
            ) ?: return false
            val actual = IvannaSpatialNative.nativeObjectRendererGetObjectPosition(renderer, index)
            if (actual.size < 5 ||
                !actual[0].isFinite() || !actual[1].isFinite() || !actual[2].isFinite() ||
                kotlin.math.abs(actual[0] - expected.x) > 0.02f ||
                kotlin.math.abs(actual[1] - expected.y) > 0.02f ||
                kotlin.math.abs(actual[2] - expected.z) > 0.02f ||
                kotlin.math.abs(actual[3] - expected.width) > 0.02f ||
                kotlin.math.abs(actual[4] - expected.gain) > 0.02f
            ) return false
        }
        return true
    }

    private fun applyScenePoseToUpmixer(
        upmixer: Long,
        azimuthRad: Float,
        elevationRad: Float,
        distanceScale: Float
    ): Boolean {
        if (upmixer == 0L) return false
        val positions = FloatArray(baseStemPositions.size * 5)
        for (stem in baseStemPositions.indices) {
            val pose = SpatialScenePoseMath.transform(
                baseStemPositions[stem], azimuthRad, elevationRad, distanceScale
            ) ?: return false
            val offset = stem * 5
            positions[offset] = pose.x
            positions[offset + 1] = pose.y
            positions[offset + 2] = pose.z
            positions[offset + 3] = pose.width
            positions[offset + 4] = pose.gain
        }
        return IvannaSpatialNative.nativeUpmixerSetStemPositions(upmixer, positions)
    }

    fun reset() {
        val h = rendererHandle
        val u = upmixerHandle
        if (h != 0L) runCatching { IvannaSpatialNative.nativeObjectRendererReset(h) }
        if (u != 0L) runCatching { IvannaSpatialNative.nativeUpmixerReset(u) }
        val tracker: IvannaHeadTracker? = headTracker
        tracker?.reset()
    }

    fun isCochlearActive(): Boolean =
        com.ivanna.omega.core.NativeBridge.safeIsCochlearActive() ||
            (if (IvannaSpatialNative.isLoaded) runCatching { IvannaSpatialNative.nativeIsCochlearActive() }.getOrDefault(false) else false)

    fun release() {
        synchronized(lock) {
            val h = rendererHandle
            val u = upmixerHandle
            if (h != 0L) {
                runCatching { IvannaSpatialNative.nativeObjectRendererDestroy(h) }
            }
            if (u != 0L) {
                runCatching { IvannaSpatialNative.nativeUpmixerDestroy(u) }
            }
            headTracker?.release()
            headTracker = null
            rendererHandle = 0L
            upmixerHandle = 0L
            ready = false
            activeSubject = "none"
        }
    }

    // ── Hot path: llamado desde el processingLoop del hilo de audio ───────────
    /**
     * Renderiza un bloque estéreo interleaved a través del ObjectRenderer
     * con HRTFs medidos. Trabaja in-place sobre [buffer].
     * frames <= BLOCK_SIZE — si es mayor, procesa los primeros BLOCK_SIZE.
     */
    fun renderBlock(buffer: FloatArray, frames: Int) {
        val h = rendererHandle
        if (!ready || h == 0L || frames <= 0 || frames > buffer.size / 2) return
        val u = upmixerHandle
        if (u == 0L) {
            // The object renderer expects four planar objects; invoking it with
            // numObjects=0 zeros the output. If stem generation is unavailable,
            // preserve the input exactly rather than converting an optional
            // spatial failure into silence.
            return
        }
        val n = minOf(frames, BLOCK_SIZE)

        // Deinterleave directo hacia FloatBuffers directos (sin arrays temporales)
        inLBuf.clear()
        inRBuf.clear()
        var i = 0
        while (i < n) {
            inLBuf.put(buffer[i * 2])
            inRBuf.put(buffer[i * 2 + 1])
            i++
        }
        inLBuf.position(0)
        inRBuf.position(0)

        // Llamada nativa: buffers directos permiten GetDirectBufferAddress en JNI
        // Nota: la firma nativa actual interpreta el primer buffer como "objects" —
        // si la semántica cambia, ajustar aquí. Por ahora usamos numObjects=0
        // (sin objetos) y pasamos los buffers out directos para que el renderer
        // pueda escribir la salida binaural directamente.
        try {
            outLBuf.clear()
            outRBuf.clear()
            run {
                stereoInBuf.clear()
                stereoInBuf.put(buffer, 0, n * 2)
                stereoInBuf.position(0)
                stemsOutBuf.clear()
                IvannaSpatialNative.nativeUpmixerProcess(u, stereoInBuf, stemsOutBuf, n)

                // Convert [frame][stem][L,R] to [stem][L frames][R frames].
                // Passing stemsOutBuf directly was a real layout mismatch: JNI
                // interpreted frame-interleaved samples as planar objects, so
                // moving the 3D source positions could spatialize the wrong samples.
                objectsInBuf.clear()
                for (stem in 0 until 4) {
                    for (frame in 0 until n) {
                        val leftSource = SpatialStemBufferLayout.interleavedStemIndex(frame, stem, 0)
                        val rightSource = SpatialStemBufferLayout.interleavedStemIndex(frame, stem, 1)
                        val leftTarget = SpatialStemBufferLayout.planarObjectIndex(stem, frame, 0, n)
                        val rightTarget = SpatialStemBufferLayout.planarObjectIndex(stem, frame, 1, n)
                        objectsInBuf.put(leftTarget, stemsOutBuf.get(leftSource))
                        objectsInBuf.put(rightTarget, stemsOutBuf.get(rightSource))
                    }
                }
                objectsInBuf.position(0)
                IvannaSpatialNative.nativeObjectRendererRenderBlock(
                    h,
                    objectsInBuf,
                    4,
                    outLBuf,
                    outRBuf,
                    n
                )
            }
        } catch (e: UnsatisfiedLinkError) {
            // Si la función nativa no está disponible con esta firma, no rompa el hilo
            return
        }

        // Extraer salida desde FloatBuffers directos a arrays y reinterleaving con crossfade atómico
        outLBuf.position(0)
        outRBuf.position(0)
        outLBuf.get(outL, 0, n)
        outRBuf.get(outR, 0, n)

        val targetGain = if (upmixerEnabledAtomic.get()) 1.0f else 0.0f
        val rampStep = 1.0f / 512.0f
        i = 0
        while (i < n) {
            if (upmixXfadeGain < targetGain) {
                upmixXfadeGain = minOf(targetGain, upmixXfadeGain + rampStep)
            } else if (upmixXfadeGain > targetGain) {
                upmixXfadeGain = maxOf(targetGain, upmixXfadeGain - rampStep)
            }
            val w = upmixXfadeGain
            val drySampleL = buffer[i * 2]
            val drySampleR = buffer[i * 2 + 1]
            buffer[i * 2]     = drySampleL * (1.0f - w) + outL[i] * w
            buffer[i * 2 + 1] = drySampleR * (1.0f - w) + outR[i] * w
            i++
        }
    }

    /** Recarga el HRTF con un nuevo sujeto (p.ej. si el usuario actualiza su perfil). */
    fun reloadHrtf(context: Context, headWidthMm: Double? = null,
                   headDepthMm: Double? = null, sex: String? = null) {
        val h = rendererHandle
        if (h == 0L) { init(context, headWidthMm, headDepthMm, sex); return }
        val subject = HrtfSubjectSelector.activate(context, h, headWidthMm, headDepthMm, sex)
        val loaded = runCatching { IvannaSpatialNative.nativeObjectRendererIsHrtfLoaded(h) }.getOrDefault(false)
        val actual = runCatching { IvannaSpatialNative.nativeObjectRendererGetCurrentSubject(h) }.getOrDefault("none")
        if (loaded && actual == subject) {
            activeSubject = actual
            hrtfStatus = "CARGADO · $actual"
        } else {
            activeSubject = "none"
            hrtfStatus = "NO CARGADO / NO CONFIRMADO"
        }
        Log.i(TAG, "HRTF recargado — solicitado=$subject, activo=$actual, loaded=$loaded")
    }
}
