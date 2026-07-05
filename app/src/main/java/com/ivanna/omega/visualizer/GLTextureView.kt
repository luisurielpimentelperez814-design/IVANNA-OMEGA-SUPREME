package com.ivanna.omega.visualizer

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import android.view.TextureView

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
 * [FIX-AURORA-BG-3] El patrón de cuadrantes / contenido viejo "pegado" no se
 * debía al render loop sino a que nunca se fijaba el tamaño del buffer del
 * SurfaceTexture. Sin setDefaultBufferSize(), Android puede dejar el buffer en
 * 1×1 o reutilizar uno previo, y el compositor termina mostrando tiles viejos.
 *
 * La corrección buena es MINIMAL y segura:
 *  - fijar setDefaultBufferSize(width, height) antes de crear el EGL surface
 *  - volver a fijarlo en cada resize
 *  - conservar el loop GL estable que ya no congelaba el sistema
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
        if (width > 0 && height > 0) {
            surface.setDefaultBufferSize(width, height)
        }
        renderThread = RenderThread(surface, renderer, width, height).apply { start() }
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
     * Hilo GL propio, EGL14 manual. Nunca toca el hilo principal salvo para
     * publicar frames vía SwapBuffers del propio SurfaceTexture (fuera del
     * hilo de UI). Frame cap a ~60fps para no saturar CPU/GPU del Moto G85.
     */
    private class RenderThread(
        private val surfaceTexture: SurfaceTexture,
        private val renderer: GLSurfaceView.Renderer,
        initialWidth: Int,
        initialHeight: Int
    ) : Thread("IvannaGLTextureViewThread") {

        @Volatile private var running = true
        @Volatile private var width = initialWidth
        @Volatile private var height = initialHeight
        @Volatile private var sizeChanged = true

        private var eglDisplay: EGLDisplay = EGL14.EGL_NO_DISPLAY
        private var eglContext: EGLContext = EGL14.EGL_NO_CONTEXT
        private var eglSurface: EGLSurface = EGL14.EGL_NO_SURFACE

        fun onSizeChanged(w: Int, h: Int) {
            width = w
            height = h
            sizeChanged = true
        }

        fun shutdownAndJoin() {
            running = false
            interrupt()
            try {
                join(500L)
            } catch (_: InterruptedException) {
                // best-effort, no bloquear el hilo de UI indefinidamente
            }
        }

        override fun run() {
            if (!initEGL()) return
            renderer.onSurfaceCreated(null, null)
            try {
                while (running) {
                    if (sizeChanged) {
                        renderer.onSurfaceChanged(null, width, height)
                        sizeChanged = false
                    }
                    renderer.onDrawFrame(null)
                    EGL14.eglSwapBuffers(eglDisplay, eglSurface)
                    try {
                        sleep(16L)
                    } catch (_: InterruptedException) {
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

            val configAttribs = intArrayOf(
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT or 0x40, // ES3_BIT_KHR = 0x40
                EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_GREEN_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_ALPHA_SIZE, 8,
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
        }
    }
}
