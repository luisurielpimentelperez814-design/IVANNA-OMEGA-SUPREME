package com.ivanna.omega.spatial

import android.content.Context
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
    @Volatile var reverbLevel: Float = 0.25f
        private set
    private val upmixerEnabledAtomic = java.util.concurrent.atomic.AtomicBoolean(true)
    val isUpmixerEnabled: Boolean
        get() = upmixerEnabledAtomic.get()
    private var upmixXfadeGain: Float = 1.0f

    // Desired scene pose is retained before native initialization and applied to
    // the four active stems when the upmixer becomes available.
    @Volatile private var sceneAzimuthRad: Float = 0f
    @Volatile private var sceneElevationRad: Float = 0f
    @Volatile private var sceneDistanceScale: Float = 1f

    private val baseStemPositions = SpatialScenePoseMath.defaultStems

    // Buffers para el hot-path: direct FloatBuffers reutilizables (sin allocations por frame)
    private val stereoInBuf: FloatBuffer = ByteBuffer
        .allocateDirect(BLOCK_SIZE * 2 * java.lang.Float.BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    private val stemsOutBuf: FloatBuffer = ByteBuffer
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
                        val poseConfirmed = applyScenePoseToUpmixer(
                            upmixer, sceneAzimuthRad, sceneElevationRad, sceneDistanceScale
                        )
                        if (!poseConfirmed) {
                            Log.w(TAG, "pose espacial inicial escrita sin read-back confirmado")
                        }
                        IvannaSpatialNative.nativeObjectRendererSyncStemObjects(handle, upmixer)
                    }.onFailure { Log.w(TAG, "upmixer no sincronizado: ${it.message}") }
                }

                val subject = runCatching {
                    HrtfSubjectSelector.activate(context, handle, headWidthMm, headDepthMm, sex)
                }.getOrElse {
                    Log.w(TAG, "selección de sujeto HRTF falló, se usa el predeterminado: ${it.message}")
                    HrtfSubjectSelector.DEFAULT_SUBJECT
                }
                synchronized(lock) {
                    rendererHandle = handle
                    upmixerHandle = upmixer
                    headTracker = tracker
                    activeSubject  = subject
                    ready = true
                }
                initError = null
                Log.i(TAG, "Spatial manager listo — sujeto HRTF: $subject")
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
                Log.w(TAG, "HRTF no confirmado: solicitado=$resolved, activo=$actual, loaded=$loaded")
                false
            } else {
                activeSubject = actual
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
        val h = rendererHandle
        val u = upmixerHandle
        if (ready && h != 0L && u != 0L) {
            runCatching {
                IvannaSpatialNative.nativeUpmixerSetStemPosition(u, stemType, x, y, z, width)
                IvannaSpatialNative.nativeObjectRendererSyncStemObjects(h, u)
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
    fun setScenePose(azimuthRad: Float, elevationRad: Float, distanceScale: Float = sceneDistanceScale): Boolean {
        if (!azimuthRad.isFinite() || !elevationRad.isFinite() || !distanceScale.isFinite()) return false
        if (azimuthRad !in -Math.PI.toFloat()..Math.PI.toFloat()) return false
        if (elevationRad !in -Math.PI.toFloat() / 4f..Math.PI.toFloat() / 4f) return false
        if (distanceScale !in 0.5f..2f) return false

        sceneAzimuthRad = azimuthRad
        sceneElevationRad = elevationRad
        sceneDistanceScale = distanceScale

        val h = rendererHandle
        val u = upmixerHandle
        if (!ready || h == 0L || u == 0L) return false
        return runCatching {
            if (!applyScenePoseToUpmixer(u, azimuthRad, elevationRad, distanceScale)) return@runCatching false
            IvannaSpatialNative.nativeObjectRendererSyncStemObjects(h, u)
            true
        }.getOrDefault(false)
    }

    private fun applyScenePoseToUpmixer(
        upmixer: Long,
        azimuthRad: Float,
        elevationRad: Float,
        distanceScale: Float
    ): Boolean {
        if (upmixer == 0L) return false
        val positions = FloatArray(baseStemPositions.size * 4)
        for (stem in baseStemPositions.indices) {
            val pose = SpatialScenePoseMath.transform(
                baseStemPositions[stem], azimuthRad, elevationRad, distanceScale
            ) ?: return false
            val offset = stem * 4
            positions[offset] = pose.x
            positions[offset + 1] = pose.y
            positions[offset + 2] = pose.z
            positions[offset + 3] = pose.width
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
        if (!ready || h == 0L || frames <= 0) return
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
            val u = upmixerHandle
            if (u != 0L) {
                stereoInBuf.clear()
                stereoInBuf.put(buffer, 0, n * 2)
                stereoInBuf.position(0)
                stemsOutBuf.clear()
                IvannaSpatialNative.nativeUpmixerProcess(u, stereoInBuf, stemsOutBuf, n)
                stemsOutBuf.position(0)
                IvannaSpatialNative.nativeObjectRendererRenderBlock(
                    h,
                    stemsOutBuf,
                    4,
                    outLBuf,
                    outRBuf,
                    n
                )
            } else {
                IvannaSpatialNative.nativeObjectRendererRenderBlock(
                    h,
                    inLBuf,
                    0,
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
        activeSubject = subject
        Log.i(TAG, "HRTF recargado — sujeto: $subject")
    }
}
