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

    @Volatile private var rendererHandle: Long = 0L
    @Volatile var ready: Boolean = false
        private set
    @Volatile var activeSubject: String = "none"
        private set

    // Buffers para el hot-path: direct FloatBuffers reutilizables (sin allocations por frame)
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
    fun init(context: Context, headWidthMm: Double? = null,
             headDepthMm: Double? = null, sex: String? = null) {
        Thread(Runnable {
            try {
                val handle = IvannaSpatialNative.nativeObjectRendererCreate(
                    SAMPLE_RATE.toFloat(), BLOCK_SIZE)
                if (handle == 0L) {
                    Log.e(TAG, "nativeObjectRendererCreate devolvió 0"); return@Runnable
                }
                val subject = HrtfSubjectSelector.activate(
                    context, handle, headWidthMm, headDepthMm, sex)
                synchronized(lock) {
                    rendererHandle = handle
                    activeSubject  = subject
                    ready = true
                }
                Log.i(TAG, "Spatial manager listo — sujeto HRTF: $subject")
            } catch (e: Exception) {
                Log.e(TAG, "Error init SpatialManager: ${e.message}", e)
            }
        }, "IvannaSpatialMgrInit").start()
    }

    fun release() {
        synchronized(lock) {
            val h = rendererHandle
            if (h != 0L) {
                runCatching { IvannaSpatialNative.nativeObjectRendererDestroy(h) }
            }
            rendererHandle = 0L
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
            IvannaSpatialNative.nativeObjectRendererRenderBlock(
                h,
                inLBuf,   // objectsBuffer / entrada direct
                0,        // numObjects
                outLBuf,  // outLeftBuffer (direct)
                outRBuf,  // outRightBuffer (direct)
                n
            )
        } catch (e: UnsatisfiedLinkError) {
            // Si la función nativa no está disponible con esta firma, no rompa el hilo
            return
        }

        // Extraer salida desde FloatBuffers directos a arrays y reinterleaving
        outLBuf.position(0)
        outRBuf.position(0)
        outLBuf.get(outL, 0, n)
        outRBuf.get(outR, 0, n)

        i = 0
        while (i < n) {
            buffer[i * 2]     = outL[i]
            buffer[i * 2 + 1] = outR[i]
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
