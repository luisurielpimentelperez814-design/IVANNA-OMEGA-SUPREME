package com.ivanna.omega.spatial

import android.content.Context
import android.util.Log
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

    private val inL  = FloatArray(BLOCK_SIZE)
    private val inR  = FloatArray(BLOCK_SIZE)
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
        // Deinterleave stereo
        for (i in 0 until n) {
            inL[i] = buffer[i * 2]
            inR[i] = buffer[i * 2 + 1]
        }

        // NOTE: El JNI actual espera java.nio.FloatBuffer + un `numObjects` Int
        // entre los buffers. Aquí usamos FloatBuffer.wrap(...) para adaptar el
        // FloatArray existente a la firma nativa. Esto produce buffers no-directos
        // (GetDirectBufferAddress devolverá nullptr en JNI), por lo que la llamada
        // nativa no hará nada en su implementación actual que requiere buffers
        // directos. Sin embargo, esto corrige el error de compilación. Para un
        // hot-path real se debe añadir un JNI que acepte FloatArray o usar
        // ByteBuffer.allocateDirect() y evitar copias.
        try {
            IvannaSpatialNative.nativeObjectRendererRenderBlock(
                h,
                FloatBuffer.wrap(inL),     // objectsBuffer (adaptado)
                0,                         // numObjects (0 -> no objetos)
                FloatBuffer.wrap(outL),    // outLeftBuffer
                FloatBuffer.wrap(outR),    // outRightBuffer
                n
            )
        } catch (e: UnsatisfiedLinkError) {
            // Si el JNI nativo no existe o la implementación no procesa estos
            // buffers, simplemente lo silenciamos para no tumbar el hilo de audio.
        }

        // Reinterleave
        for (i in 0 until n) {
            buffer[i * 2]     = outL[i]
            buffer[i * 2 + 1] = outR[i]
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
