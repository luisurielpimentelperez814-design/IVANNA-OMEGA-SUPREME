package com.ivanna.omega.visualizer

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import android.view.Choreographer
import android.view.TextureView
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * GLTextureView — respaldo TextureView para el wallpaper GL, en vez de
 * GLSurfaceView (SurfaceView).
 *
 * [FIX-AURORA-BG-2] GLSurfaceView es un SurfaceView: tiene su PROPIO surface
 * a nivel de SurfaceFlinger, compuesto por delante (zOrderOnTop=true) o por
 * detrás (false) de TODA la ventana — nunca "entre" Views normales. Para que
 * quedara detrás del panel de Compose sin taparlo, la única forma con
 * SurfaceView es hacer la ventana entera translúcida, lo cual obliga a
 * recomponer con alpha blending la ventana completa en cada vsync. Combinado
 * con el render continuo del wallpaper, eso saturó el hilo principal en el
 * Moto G85 — de ahí que hasta el scroll se congelara.
 *
 * TextureView, en cambio, es una View normal: su contenido se dibuja dentro
 * de la jerarquía de Views de la app, en el orden en que aparece.
 *
 * [FIX-AURORA-BG-3] setDefaultBufferSize() en cada resize para que el
 * compositor no reutilice tiles viejos.
 *
 * [FIX-AURORA-BG-4] Correcciones definitivas contra congelamiento del sistema:
 *   1. isOpaque = true. El shader escribe alpha=1.0 en cada píxel; forzar
 *      blending traslúcido sobre TODA la textura hacía que SurfaceFlinger
 *      recompusiera la ventana completa con alpha por cada frame — costoso
 *      y culpable del congelamiento observado en Moto G85 (SoC gama media).
 *      El panel de Compose se dibuja EN una View aparte por encima; no hace
 *      falta transparencia en el wallpaper.
 *   2. Sincronización vsync vía Choreographer en lugar de Thread.sleep(16),
 *      atada al display real del dispositivo — evita tearing, drops y consumo
 *      continuo cuando el compositor no está listo.
 *   3. Pausa real cuando la View no es visible (onVisibilityChanged /
 *      onDetachedFromWindow) — antes el hilo GL seguía renderizando aunque
 *      la Activity estuviera en background, robándole GPU/CPU a otras apps
 *      y quemando batería.
 *   4. Ciclo de vida explícito: onPause() / onResume() públicos para que la
 *      Activity los llame desde su onPause/onResume.
 *   5. Cierre EGL/hilo determinista con timeout — nunca bloquea al hilo de
 *      UI más de 500 ms.
 */
class GLTextureView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : TextureView(context, attrs), TextureView.SurfaceTextureListener {

    private var renderThread: RenderThread? = null
    private var pendingRenderer: GLSurfaceView.Renderer? = null
    private var externallyPaused = false

    init {
        surfaceTextureListener = this
        // [FIX-AURORA-BG-4.1] TextureView opaco. El shader ya rellena alpha=1.
        // Marcarlo transparente forzaba blending por-píxel sobre la ventana
        // entera en cada vsync, lo que congela apps encima en gama media.
        isOpaque = true
    }

    fun setRenderer(renderer: GLSurfaceView.Renderer) {
        pendingRenderer = renderer
    }

    /** Llamar desde Activity.onPause() para detener el render en background. */
    fun onPause() {
        externallyPaused = true
        renderThread?.setPaused(true)
    }

    /** Llamar desde Activity.onResume() para reanudar el render. */
    fun onResume() {
        externallyPaused = false
        renderThread?.setPaused(false)
    }

    override fun onVisibilityChanged(changedView: android.view.View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        // [FIX-AURORA-BG-4.3] Pausa cuando la View no es visible.
        val visible = visibility == VISIBLE && !externallyPaused
        renderThread?.setPaused(!visible)
    }

    override fun onDetachedFromWindow() {
        renderThread?.shutdownAndJoin()
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
        renderThread?.shutdownAndJoin()
        renderThread = null
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) = Unit

    /**
     * Hilo GL propio, EGL14 manual. Nunca toca el hilo principal.
     *
     * [FIX-AURORA-BG-4.2] La cadencia de frames se pide vía Choreographer del
     * hilo principal (postFrameCallback) para sincronizar con el vsync real
     * del display. Cuando llega el callback, se libera un semáforo que
     * desbloquea al hilo GL para dibujar exactamente un frame. Esto:
     *   - evita quemar CPU con sleep(16) fijo,
     *   - respeta la tasa real (60/90/120 Hz según el panel),
     *   - se pausa automáticamente si el compositor no pide frames
     *     (p.ej. pantalla apagada o activity ocluida por otra app).
     */
    private class RenderThread(
        private val surfaceTexture: SurfaceTexture,
        private val renderer: GLSurfaceView.Renderer,
        initialWidth: Int,
        initialHeight: Int
    ) : Thread("IvannaGLTextureViewThread") {

        @Volatile private var running = true
        @Volatile private var paused = false
        @Volatile private var width = initialWidth
        @Volatile private var height = initialHeight
        @Volatile private var sizeChanged = true

        // Semáforo tick de vsync. Un frame por notificación; sin acumulación.
        private val vsyncLock = ReentrantLock()
        private val vsyncCond = vsyncLock.newCondition()
        @Volatile private var vsyncTicked = false

        private val choreographer: Choreographer by lazy {
            // Choreographer se obtiene desde el hilo principal
            Choreographer.getInstance()
        }

        private val frameCallback = object : Choreographer.FrameCallback {
            override fun doFrame(frameTimeNanos: Long) {
                if (!running) return
                signalVsync()
                if (!paused) {
                    // Volvemos a postearnos para el próximo frame.
                    choreographer.postFrameCallback(this)
                }
            }
        }

        private var eglDisplay: EGLDisplay = EGL14.EGL_NO_DISPLAY
        private var eglContext: EGLContext = EGL14.EGL_NO_CONTEXT
        private var eglSurface: EGLSurface = EGL14.EGL_NO_SURFACE

        fun onSizeChanged(w: Int, h: Int) {
            width = w
            height = h
            sizeChanged = true
        }

        fun setPaused(p: Boolean) {
            if (paused == p) return
            paused = p
            if (!p) {
                // Reanudar: repostear el callback en el hilo principal.
                android.os.Handler(android.os.Looper.getMainLooper()).post {
                    if (running && !paused) {
                        choreographer.removeFrameCallback(frameCallback)
                        choreographer.postFrameCallback(frameCallback)
                    }
                }
                // Desbloquear al hilo GL por si estaba esperando.
                signalVsync()
            }
        }

        fun shutdownAndJoin() {
            running = false
            // Quitar el callback del hilo principal.
            android.os.Handler(android.os.Looper.getMainLooper()).post {
                try {
                    choreographer.removeFrameCallback(frameCallback)
                } catch (_: Throwable) { /* best-effort */ }
            }
            signalVsync()
            interrupt()
            try {
                join(500L)
            } catch (_: InterruptedException) {
                // best-effort, no bloquear el hilo de UI indefinidamente
            }
        }

        private fun signalVsync() {
            vsyncLock.withLock {
                vsyncTicked = true
                vsyncCond.signalAll()
            }
        }

        private fun awaitVsync(): Boolean {
            vsyncLock.withLock {
                var waited = 0L
                while (!vsyncTicked && running) {
                    // timeout de seguridad: si por alguna razón no llega vsync
                    // en 500 ms (pantalla apagada, etc.) simplemente pausamos.
                    val ok = vsyncCond.await(500L, java.util.concurrent.TimeUnit.MILLISECONDS)
                    if (!ok) {
                        waited += 500L
                        if (paused || !running) return false
                        // Si llevamos >2s sin vsync, dormimos hasta que setPaused(false)
                        // nos reactive.
                        if (waited >= 2000L) return false
                    }
                }
                val ticked = vsyncTicked
                vsyncTicked = false
                return ticked && running
            }
        }

        override fun run() {
            if (!initEGL()) return
            renderer.onSurfaceCreated(null, null)

            // Arrancar el callback de vsync desde el hilo principal.
            android.os.Handler(android.os.Looper.getMainLooper()).post {
                if (running && !paused) {
                    choreographer.postFrameCallback(frameCallback)
                }
            }

            try {
                while (running) {
                    if (paused) {
                        // Esperar hasta que nos reactiven.
                        try {
                            vsyncLock.withLock {
                                while (paused && running) {
                                    vsyncCond.await(1000L, java.util.concurrent.TimeUnit.MILLISECONDS)
                                }
                                vsyncTicked = false
                            }
                        } catch (_: InterruptedException) {
                            break
                        }
                        continue
                    }

                    // Esperar señal de vsync del Choreographer.
                    if (!awaitVsync()) continue
                    if (!running) break

                    if (sizeChanged) {
                        renderer.onSurfaceChanged(null, width, height)
                        sizeChanged = false
                    }
                    renderer.onDrawFrame(null)
                    if (!EGL14.eglSwapBuffers(eglDisplay, eglSurface)) {
                        // Surface muerto (rotación / detach). Salir.
                        break
                    }
                }
            } finally {
                releaseEGL()
            }
        }

        private fun initEGL(): Boolean {
            eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            if (eglDisplay == EGL14.EGL_NO_DISPLAY) return false

            val version = IntArray(2)
            if (!EGL14.eglInitialize(eglDisplay, version, 0, version, 1)) return false

            // [FIX-AURORA-BG-4.1] Config opaco (sin alpha). isOpaque=true en el
            // TextureView + config sin canal alpha = compositor rápido.
            val configAttribs = intArrayOf(
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT or 0x40, // ES3_BIT_KHR = 0x40
                EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_GREEN_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_ALPHA_SIZE, 0,
                EGL14.EGL_DEPTH_SIZE, 0,
                EGL14.EGL_STENCIL_SIZE, 0,
                EGL14.EGL_NONE
            )
            val configs = arrayOfNulls<android.opengl.EGLConfig>(1)
            val numConfigs = IntArray(1)
            if (!EGL14.eglChooseConfig(eglDisplay, configAttribs, 0, configs, 0, 1, numConfigs, 0) ||
                numConfigs[0] <= 0
            ) return false
            val config = configs[0] ?: return false

            val contextAttribs = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE)
            eglContext = EGL14.eglCreateContext(eglDisplay, config, EGL14.EGL_NO_CONTEXT, contextAttribs, 0)
            if (eglContext == EGL14.EGL_NO_CONTEXT) return false

            val surfaceAttribs = intArrayOf(EGL14.EGL_NONE)
            eglSurface = EGL14.eglCreateWindowSurface(eglDisplay, config, surfaceTexture, surfaceAttribs, 0)
            if (eglSurface == EGL14.EGL_NO_SURFACE) return false

            if (!EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) return false

            // [FIX-AURORA-BG-4.2b] Swap interval = 1 (vsync). El Choreographer
            // ya nos limita a la tasa del display; esto solo asegura que el
            // driver no adelante frames.
            EGL14.eglSwapInterval(eglDisplay, 1)
            return true
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
        }
    }
}
