package com.ivanna.omega.visualizer

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.os.Handler
import android.os.HandlerThread
import android.util.AttributeSet
import android.view.Choreographer
import android.view.TextureView
import java.util.concurrent.atomic.AtomicBoolean

/**
 * GLTextureView — respaldo TextureView para el wallpaper GL, en vez de
 * GLSurfaceView (SurfaceView).
 *
 * [FIX-AURORA-BG-2] GLSurfaceView es un SurfaceView: tiene su PROPIO surface
 * a nivel de SurfaceFlinger, compuesto por delante (zOrderOnTop=true) o por
 * detrás (false) de TODA la ventana — nunca "entre" Views normales. Para que
 * quedara detrás del panel de Compose sin taparlo, la única forma con
 * SurfaceView es hacer la ventana entera translúcida (window.setFormat +
 * background transparente), lo cual obliga a recomponer con alpha blending
 * la ventana completa en cada vsync. Combinado con el render continuo del
 * wallpaper, eso saturó el hilo principal en el Moto G85 — de ahí que hasta
 * el scroll se congelara.
 *
 * TextureView, en cambio, es una View normal: su contenido se dibuja dentro
 * de la jerarquía de Views de la app, en el orden en que aparece — se
 * compone detrás o delante de otros elementos exactamente como cualquier
 * ImageView, sin surface propio ni necesidad de ventana translúcida.
 *
 * [FIX-AURORA-BG-3] Correcciones al render loop:
 *
 *  1. setDefaultBufferSize(): sin esto el SurfaceTexture entregado por
 *     Android arranca con un buffer 1×1 (o el previo si se recicla el
 *     TextureView). El eglCreateWindowSurface toma ese tamaño y OpenGL
 *     acaba escribiendo en una geometría equivocada — el compositor
 *     entonces muestra fragmentos de buffers viejos "pegados" en cuadrantes
 *     (2 cuadros negros + 2 con contenido antiguo). Hay que fijarlo antes
 *     de crear el EGL window surface, y volverlo a fijar en cada resize.
 *
 *  2. Sincronización con Choreographer en lugar de Thread.sleep(16L):
 *     un sleep fijo NO está alineado con vsync, produce jitter y compite
 *     con el Choreographer del compositor. En un Moto G85 eso se traduce
 *     en frames que caen en el vsync equivocado y bloquean el paso de
 *     otras apps por SurfaceFlinger — de ahí que "se empezaran a congelar
 *     todas las apps". Usamos Choreographer.postFrameCallback desde un
 *     HandlerThread para pedirle al sistema el próximo vsync REAL, y
 *     hacemos swap sólo cuando el compositor está listo.
 *
 *  3. Al cambiar de tamaño hay que recrear el EGL window surface — no basta
 *     con re-llamar renderer.onSurfaceChanged, porque el back buffer sigue
 *     con la geometría vieja. Además fijamos glViewport(0,0,w,h) explícito.
 *
 *  4. Liberamos el SurfaceTexture con release() al destruirse — evita fugas
 *     de GraphicBuffer que también contribuyen a estrangular SurfaceFlinger.
 */
class GLTextureView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : TextureView(context, attrs), TextureView.SurfaceTextureListener {

    private var renderThread: RenderThread? = null
    private var pendingRenderer: GLSurfaceView.Renderer? = null

    init {
        surfaceTextureListener = this
        isOpaque = false
    }

    fun setRenderer(renderer: GLSurfaceView.Renderer) {
        pendingRenderer = renderer
    }

    override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
        val renderer = pendingRenderer ?: return
        // [FIX-AURORA-BG-3 #1] Fijar el tamaño del buffer ANTES de que el hilo
        // GL cree el EGL window surface. Sin esto el buffer arranca 1×1.
        surface.setDefaultBufferSize(width, height)
        renderThread = RenderThread(surface, renderer, width, height).apply { start() }
    }

    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
        // Mantener el buffer del SurfaceTexture alineado con el nuevo tamaño
        // de la vista — igual de crítico que en onSurfaceTextureAvailable.
        surface.setDefaultBufferSize(width, height)
        renderThread?.onSizeChanged(width, height)
    }

    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        renderThread?.shutdownAndJoin()
        renderThread = null
        // [FIX-AURORA-BG-3 #4] Devolvemos true para que el framework NO
        // reutilice este SurfaceTexture. Se libera aquí — evita mantener
        // vivos GraphicBuffers que estrangulan SurfaceFlinger.
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) = Unit

    /**
     * Hilo GL propio, EGL14 manual. Nunca toca el hilo principal.
     *
     * Sustituimos Thread.sleep(16L) por sincronización real con vsync vía
     * Choreographer sobre un HandlerThread — se pide un frame, se dibuja y
     * se hace swap; el propio SurfaceFlinger nos regula la cadencia y ya
     * no competimos con él por el vsync.
     */
    private class RenderThread(
        private val surfaceTexture: SurfaceTexture,
        private val renderer: GLSurfaceView.Renderer,
        initialWidth: Int,
        initialHeight: Int
    ) : Thread("IvannaGLTextureViewThread") {

        private val running = AtomicBoolean(true)
        @Volatile private var width = initialWidth
        @Volatile private var height = initialHeight
        @Volatile private var sizeChanged = true

        private var eglDisplay: EGLDisplay = EGL14.EGL_NO_DISPLAY
        private var eglContext: EGLContext = EGL14.EGL_NO_CONTEXT
        private var eglSurface: EGLSurface = EGL14.EGL_NO_SURFACE
        private var eglConfig: EGLConfig? = null

        private var vsyncThread: HandlerThread? = null
        private var vsyncHandler: Handler? = null
        @Volatile private var choreographer: Choreographer? = null

        // Se dispara cuando llega un frame de vsync — en el HandlerThread.
        private val frameCallback = object : Choreographer.FrameCallback {
            override fun doFrame(frameTimeNanos: Long) {
                if (!running.get()) return
                // Volvemos a pedir el siguiente frame primero — así aunque
                // el draw tarde, la cadena de vsyncs no se interrumpe.
                choreographer?.postFrameCallback(this)
                drawOneFrame()
            }
        }

        fun onSizeChanged(w: Int, h: Int) {
            width = w
            height = h
            sizeChanged = true
        }

        fun shutdownAndJoin() {
            running.set(false)
            // Detenemos el vsync antes de esperar el hilo — si no, el
            // callback seguiría posteándose y bloquearía la salida.
            vsyncHandler?.post {
                choreographer?.removeFrameCallback(frameCallback)
            }
            vsyncThread?.quitSafely()
            vsyncThread = null
            vsyncHandler = null
            try {
                join(500L)
            } catch (e: InterruptedException) {
                // best-effort, no bloquear el hilo de UI indefinidamente
            }
        }

        override fun run() {
            if (!initEGL()) return
            renderer.onSurfaceCreated(null, eglConfig)

            // Arrancamos un HandlerThread aparte SÓLO para hospedar el
            // Choreographer (necesita un Looper). El draw sigue corriendo
            // en este hilo GL — el HandlerThread nos "empuja" cuando el
            // sistema tiene un vsync disponible.
            val ht = HandlerThread("IvannaGLVsync").also { it.start() }
            vsyncThread = ht
            val handler = Handler(ht.looper)
            vsyncHandler = handler
            handler.post {
                choreographer = Choreographer.getInstance()
                choreographer?.postFrameCallback(frameCallback)
            }

            // Loop de mantenimiento: dormimos hasta que shutdown nos avise.
            // El draw real lo hace frameCallback en respuesta a vsync.
            try {
                while (running.get()) {
                    try {
                        sleep(50L)
                    } catch (e: InterruptedException) {
                        break
                    }
                }
            } finally {
                releaseEGL()
                // Liberamos el SurfaceTexture — ver [FIX-AURORA-BG-3 #4].
                try {
                    surfaceTexture.release()
                } catch (_: Throwable) {
                    // ya liberado por el framework, ignorar
                }
            }
        }

        @Synchronized
        private fun drawOneFrame() {
            if (!running.get()) return
            if (eglDisplay == EGL14.EGL_NO_DISPLAY || eglSurface == EGL14.EGL_NO_SURFACE) return

            // Asegurar contexto actual — otro thread podría haber tocado EGL.
            if (!EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) return

            if (sizeChanged) {
                // [FIX-AURORA-BG-3 #3] Recrear el EGL window surface con la
                // nueva geometría. El SurfaceTexture ya recibió su
                // setDefaultBufferSize() en el hilo UI.
                recreateWindowSurface()
                GLES20.glViewport(0, 0, width, height)
                renderer.onSurfaceChanged(null, width, height)
                sizeChanged = false
            }

            renderer.onDrawFrame(null)
            EGL14.eglSwapBuffers(eglDisplay, eglSurface)
        }

        private fun recreateWindowSurface(): Boolean {
            val cfg = eglConfig ?: return false
            if (eglSurface != EGL14.EGL_NO_SURFACE) {
                EGL14.eglMakeCurrent(
                    eglDisplay,
                    EGL14.EGL_NO_SURFACE,
                    EGL14.EGL_NO_SURFACE,
                    EGL14.EGL_NO_CONTEXT
                )
                EGL14.eglDestroySurface(eglDisplay, eglSurface)
                eglSurface = EGL14.EGL_NO_SURFACE
            }
            val surfaceAttribs = intArrayOf(EGL14.EGL_NONE)
            eglSurface = EGL14.eglCreateWindowSurface(
                eglDisplay, cfg, surfaceTexture, surfaceAttribs, 0
            )
            if (eglSurface == EGL14.EGL_NO_SURFACE) return false
            return EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)
        }

        private fun initEGL(): Boolean {
            eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            if (eglDisplay == EGL14.EGL_NO_DISPLAY) return false

            val version = IntArray(2)
            if (!EGL14.eglInitialize(eglDisplay, version, 0, version, 1)) return false

            val configAttribs = intArrayOf(
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT or 0x40, // ES3_BIT_KHR = 0x40
                EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_GREEN_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_ALPHA_SIZE, 8,
                EGL14.EGL_NONE
            )
            val configs = arrayOfNulls<EGLConfig>(1)
            val numConfigs = IntArray(1)
            if (!EGL14.eglChooseConfig(eglDisplay, configAttribs, 0, configs, 0, 1, numConfigs, 0) ||
                numConfigs[0] <= 0
            ) return false
            val config = configs[0] ?: return false
            eglConfig = config

            val contextAttribs = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE)
            eglContext = EGL14.eglCreateContext(eglDisplay, config, EGL14.EGL_NO_CONTEXT, contextAttribs, 0)
            if (eglContext == EGL14.EGL_NO_CONTEXT) return false

            val surfaceAttribs = intArrayOf(EGL14.EGL_NONE)
            eglSurface = EGL14.eglCreateWindowSurface(eglDisplay, config, surfaceTexture, surfaceAttribs, 0)
            if (eglSurface == EGL14.EGL_NO_SURFACE) return false

            return EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)
        }

        private fun releaseEGL() {
            if (eglDisplay != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(eglDisplay, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
                if (eglSurface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(eglDisplay, eglSurface)
                if (eglContext != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(eglDisplay, eglContext)
                EGL14.eglTerminate(eglDisplay)
            }
            eglDisplay = EGL14.EGL_NO_DISPLAY
            eglContext = EGL14.EGL_NO_CONTEXT
            eglSurface = EGL14.EGL_NO_SURFACE
            eglConfig = null
        }
    }
}
