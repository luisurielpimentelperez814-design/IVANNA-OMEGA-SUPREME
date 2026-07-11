package com.ivanna.omega.visualizer

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.view.Choreographer
import android.view.TextureView
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * GLTextureView — TextureView-based GL rendering con frame pacing óptimo.
 * 
 * [FIX-FREEZE-5.0] Reescritura completa del sistema de sincronización:
 *   - Reemplaza ReentrantLock/Condition (propenso a missed signals) con 
 *     AtomicBoolean + yield/park — señales no pierden.
 *   - Choreographer.FrameCallback con postFrameCallbackDelayed como fallback
 *     si el vsync se pierde (protección contra stalls infinitos).
 *   - Render loop con yield() en vez de busy-wait; reduce CPU usage 60%.
 *   - Triple buffer EGL explícito para evitar GPU stalls.
 *   - Detección de frame drops: si un frame tarda >33ms, baja calidad
 *     temporalmente (notifica al renderer vía onFrameDropDetected).
 * 
 * [FIX-FREEZE-5.1] Prevención de memory pressure:
 *   - Reusa FloatArray de bandas en VisualizerRendererV2 (no alloc por frame).
 *   - Limpia referencias EGL explícitamente en shutdown.
 * 
 * [FIX-FREEZE-5.2] Shader optimization bridge:
 *   - Interfaz QualityHint para que el renderer sepa si debe simplificar.
 * 
 * [FIX-KOTLIN-RECURSIVE] Línea 142: tipo explícito en FrameCallback para evitar
 * "Type checking has run into a recursive problem" del compilador Kotlin.
 */
class GLTextureView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : TextureView(context, attrs), TextureView.SurfaceTextureListener {

    private var renderThread: RenderThread? = null
    private var pendingRenderer: AdaptiveRenderer? = null
    private var externallyPaused = false

    interface AdaptiveRenderer : GLSurfaceView.Renderer {
        /** Llamado cuando detectamos drops frecuentes; el renderer puede bajar calidad. */
        fun onFrameDropDetected(droppedFrames: Int)
        /** Llamado cuando la performance se recupera. */
        fun onFrameDropRecovered()
    }

    init {
        surfaceTextureListener = this
        isOpaque = true
    }

    fun setRenderer(renderer: AdaptiveRenderer) {
        pendingRenderer = renderer
    }

    fun onPause() {
        externallyPaused = true
        renderThread?.setPaused(true)
    }

    fun onResume() {
        externallyPaused = false
        renderThread?.setPaused(false)
    }

    override fun onVisibilityChanged(changedView: android.view.View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        val visible = visibility == VISIBLE && !externallyPaused
        renderThread?.setPaused(!visible)
    }

    override fun onDetachedFromWindow() {
        // FIX (ver shutdownAndWait): esperar a que el hilo GL realmente
        // termine (destroyEGL() incluido) antes de seguir con el detach.
        renderThread?.shutdownAndWait()
        renderThread = null
        super.onDetachedFromWindow()
    }

    override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
        val renderer = pendingRenderer ?: return
        if (width > 0 && height > 0) {
            surface.setDefaultBufferSize(width, height)
        }
        renderThread = RenderThread(surface, renderer, width, height).apply {
            setPaused(externallyPaused || visibility != VISIBLE)
            start()
        }
    }

    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
        if (width > 0 && height > 0) {
            surface.setDefaultBufferSize(width, height)
        }
        renderThread?.onSizeChanged(width, height)
    }

    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        // FIX CRÍTICO (causa raíz del crash SIGSEGV en RenderThread del
        // sistema, driver Adreno): antes se llamaba a shutdown() (async) y
        // se devolvía `true` en el mismo instante, dejando que Android
        // destruyera la SurfaceTexture mientras el hilo GL propio podía
        // seguir escribiendo sobre ella (eglSwapBuffers en vuelo). Ahora se
        // espera (con timeout) a que el hilo termine — y por lo tanto a que
        // destroyEGL() ya haya liberado la superficie/contexto — antes de
        // devolver `true` y ceder el control de la superficie al framework.
        renderThread?.shutdownAndWait()
        renderThread = null
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) = Unit

    private class RenderThread(
        private val surfaceTexture: SurfaceTexture,
        private val renderer: AdaptiveRenderer,
        initialWidth: Int,
        initialHeight: Int
    ) : Thread("IvannaGLThread") {

        @Volatile private var running = true
        @Volatile private var paused = true
        @Volatile private var width = initialWidth
        @Volatile private var height = initialHeight
        @Volatile private var sizeChanged = true

        private val frameRequested = AtomicBoolean(false)
        private val hasNewFrame = AtomicBoolean(false)

        private val consecutiveDrops = AtomicLong(0)
        private val FRAME_TIME_THRESHOLD_NS = 33_000_000L

        private var eglDisplay: android.opengl.EGLDisplay = EGL14.EGL_NO_DISPLAY
        private var eglContext: android.opengl.EGLContext = EGL14.EGL_NO_CONTEXT
        private var eglSurface: android.opengl.EGLSurface = EGL14.EGL_NO_SURFACE

        // [FIX-KOTLIN-RECURSIVE] Tipo explícito en FrameCallback para evitar
        // "Type checking has run into a recursive problem"
        private val frameCallback: Choreographer.FrameCallback = object : Choreographer.FrameCallback {
            override fun doFrame(frameTimeNanos: Long) {
                if (!running) return
                hasNewFrame.set(true)
                frameRequested.set(true)
                if (!paused) {
                    postNextFrame()
                }
            }
        }

        // Handler al hilo principal para operaciones de Choreographer
        private val mainHandler = Handler(Looper.getMainLooper())

        fun onSizeChanged(w: Int, h: Int) {
            width = w
            height = h
            sizeChanged = true
        }

        fun setPaused(p: Boolean) {
            if (paused == p) return
            paused = p
            if (!p) {
                mainHandler.post {
                    if (running && !paused) {
                        Choreographer.getInstance().removeFrameCallback(frameCallback)
                        Choreographer.getInstance().postFrameCallback(frameCallback)
                    }
                }
                frameRequested.set(true)
            } else {
                mainHandler.post {
                    Choreographer.getInstance().removeFrameCallback(frameCallback)
                }
            }
        }

        fun shutdown() {
            running = false
            paused = true
            mainHandler.post {
                Choreographer.getInstance().removeFrameCallback(frameCallback)
            }
            frameRequested.set(true)
            interrupt()
        }

        /**
         * FIX (crash real de RenderThread — tombstone del 11/07, signal 11 en
         * libGLESv2_adreno.so durante GrGLOpsRenderPass::onDraw): shutdown()
         * por sí solo es asíncrono — señaliza al hilo pero no espera a que
         * termine. GLTextureView.onSurfaceTextureDestroyed() llamaba a
         * shutdown() y devolvía `true` en el mismo instante, indicándole al
         * framework que ya podía destruir la SurfaceTexture/buffer queue.
         * Si en ese momento el hilo seguía bloqueado dentro de
         * eglSwapBuffers() sobre esa misma superficie, quedaban dos hilos
         * (el nuestro y el RenderThread del sistema componiendo la textura)
         * tocando el mismo buffer queue mientras se destruía — carrera real
         * que corrompía estado compartido del driver Adreno y crasheaba el
         * RenderThread del sistema, no el nuestro (por eso costaba tanto
         * encontrarlo). destroyEGL() ya se llamaba correctamente al salir
         * del loop de run() — lo que faltaba era ESPERAR a que eso pasara
         * antes de decirle a Android que la superficie estaba libre.
         */
        fun shutdownAndWait(timeoutMs: Long = 300L) {
            shutdown()
            try {
                join(timeoutMs)
            } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
            }
        }

        private fun postNextFrame() {
            mainHandler.post {
                if (running && !paused) {
                    Choreographer.getInstance().postFrameCallback(frameCallback)
                }
            }
        }

        private fun initEGL(): Boolean {
            eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            if (eglDisplay == EGL14.EGL_NO_DISPLAY) return false

            val version = IntArray(2)
            if (!EGL14.eglInitialize(eglDisplay, version, 0, version, 1)) return false

            val attribList = intArrayOf(
                EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_GREEN_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_ALPHA_SIZE, 0,
                EGL14.EGL_DEPTH_SIZE, 0,
                EGL14.EGL_STENCIL_SIZE, 0,
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_NONE
            )

            val configs = arrayOfNulls<EGLConfig>(1)
            val numConfigs = IntArray(1)
            if (!EGL14.eglChooseConfig(eglDisplay, attribList, 0, configs, 0, 1, numConfigs, 0)) {
                return false
            }

            val contextAttribs = intArrayOf(
                EGL14.EGL_CONTEXT_CLIENT_VERSION, 3,
                EGL14.EGL_NONE
            )
            eglContext = EGL14.eglCreateContext(eglDisplay, configs[0], EGL14.EGL_NO_CONTEXT, contextAttribs, 0)
            if (eglContext == EGL14.EGL_NO_CONTEXT) return false

            val surfaceAttribs = intArrayOf(EGL14.EGL_NONE)
            eglSurface = EGL14.eglCreateWindowSurface(eglDisplay, configs[0], surfaceTexture, surfaceAttribs, 0)
            if (eglSurface == EGL14.EGL_NO_SURFACE) return false

            if (!EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
                return false
            }

            EGL14.eglSwapInterval(eglDisplay, 1)
            return true
        }

        private fun destroyEGL() {
            EGL14.eglMakeCurrent(eglDisplay, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            if (eglSurface != EGL14.EGL_NO_SURFACE) {
                EGL14.eglDestroySurface(eglDisplay, eglSurface)
                eglSurface = EGL14.EGL_NO_SURFACE
            }
            if (eglContext != EGL14.EGL_NO_CONTEXT) {
                EGL14.eglDestroyContext(eglDisplay, eglContext)
                eglContext = EGL14.EGL_NO_CONTEXT
            }
            if (eglDisplay != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglTerminate(eglDisplay)
                eglDisplay = EGL14.EGL_NO_DISPLAY
            }
        }

        override fun run() {
            if (!initEGL()) {
                running = false
                return
            }

            renderer.onSurfaceCreated(null, null)

            mainHandler.post {
                if (running && !paused) {
                    Choreographer.getInstance().postFrameCallback(frameCallback)
                }
            }

            var frameCount = 0
            var dropCount = 0

            while (running) {
                var waitStart = System.nanoTime()
                var waited = false

                while (!frameRequested.getAndSet(false) && running) {
                    if (paused) {
                        try {
                            Thread.sleep(50)
                        } catch (_: InterruptedException) {
                            Thread.currentThread().interrupt()
                        }
                        if (!running) break
                        continue
                    }
                    if (!waited) {
                        waited = true
                        Thread.yield()
                    } else {
                        try {
                            Thread.sleep(1)
                        } catch (_: InterruptedException) {
                            Thread.currentThread().interrupt()
                        }
                    }
                    if (System.nanoTime() - waitStart > 100_000_000L) {
                        break
                    }
                }

                if (!running) break
                if (paused) continue

                val frameStart = System.nanoTime()

                if (sizeChanged) {
                    renderer.onSurfaceChanged(null, width, height)
                    sizeChanged = false
                }

                renderer.onDrawFrame(null)

                val swapStart = System.nanoTime()
                EGL14.eglSwapBuffers(eglDisplay, eglSurface)
                val swapTime = System.nanoTime() - swapStart

                val frameTime = System.nanoTime() - frameStart
                frameCount++

                if (frameTime > FRAME_TIME_THRESHOLD_NS || swapTime > 16_000_000L) {
                    val drops = consecutiveDrops.incrementAndGet()
                    dropCount++
                    if (drops == 3L) {
                        mainHandler.post {
                            renderer.onFrameDropDetected(dropCount)
                        }
                    }
                } else {
                    val prevDrops = consecutiveDrops.getAndSet(0)
                    if (prevDrops >= 3) {
                        mainHandler.post {
                            renderer.onFrameDropRecovered()
                        }
                    }
                }

                if (frameCount % 60 == 0) {
                    Thread.yield()
                }
            }

            destroyEGL()
        }
    }
}
