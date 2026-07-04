package com.ivanna.omega.visualizer

import android.content.Context
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import java.io.BufferedReader
import java.io.InputStreamReader
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

private const val VERTEX_SRC_V2 = """#version 320 es
precision highp float;
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
"""

private const val SHADER_ASSET_PATH = "shaders/wallpaper_v2.glsl"

/**
 * VisualizerRendererV2 — análogo a VisualizerRenderer (v1), pero:
 *   - carga el fragment shader desde assets/shaders/wallpaper_v2.glsl en vez
 *     de tenerlo embebido como string Kotlin (así queda como archivo .glsl
 *     independiente, editable sin recompilar Kotlin).
 *   - bindea un array de 13 uniforms (u_bands / u_bands_prev) en vez de 3
 *     escalares sueltos.
 *   - lee de IvannaVisualizerBridgeV2 (13 bandas), no de IvannaVisualizerBridge.
 *
 * No reemplaza VisualizerRenderer: la Activity/Service que arma la GLSurfaceView
 * decide cuál instanciar (o expone un toggle v1/v2 en Ajustes).
 */
class VisualizerRendererV2(private val context: Context) : GLSurfaceView.Renderer {
    private var program = 0
    private var locBands = -1
    private var locBandsPrev = -1
    private var locFramePhase = -1
    private var locTime = -1
    private var locRes = -1

    private var width = 1
    private var height = 1
    private val startNanos = System.nanoTime()
    private var lastFrameNanos = 0L
    private var prevBands = FloatArray(IvannaVisualizerBridgeV2.BAND_COUNT)

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        val fragmentSrc = loadShaderAsset()
        val vs = compile(GLES30.GL_VERTEX_SHADER, VERTEX_SRC_V2)
        val fs = compile(GLES30.GL_FRAGMENT_SHADER, fragmentSrc)
        program = GLES30.glCreateProgram().also {
            GLES30.glAttachShader(it, vs)
            GLES30.glAttachShader(it, fs)
            GLES30.glLinkProgram(it)
            val status = IntArray(1)
            GLES30.glGetProgramiv(it, GLES30.GL_LINK_STATUS, status, 0)
            if (status[0] == 0) {
                val log = GLES30.glGetProgramInfoLog(it)
                GLES30.glDeleteProgram(it)
                throw RuntimeException("Link error VisualizerRendererV2: $log")
            }
        }
        GLES30.glDeleteShader(vs)
        GLES30.glDeleteShader(fs)

        locBands = GLES30.glGetUniformLocation(program, "u_bands")
        locBandsPrev = GLES30.glGetUniformLocation(program, "u_bands_prev")
        locFramePhase = GLES30.glGetUniformLocation(program, "u_frame_phase")
        locTime = GLES30.glGetUniformLocation(program, "u_time")
        locRes = GLES30.glGetUniformLocation(program, "u_resolution")

        GLES30.glClearColor(0f, 0f, 0f, 1f)
        lastFrameNanos = System.nanoTime()
    }

    override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
        width = w; height = h
        GLES30.glViewport(0, 0, w, h)
    }

    override fun onDrawFrame(gl: GL10?) {
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(program)

        val now = System.nanoTime()
        val deltaMs = (now - lastFrameNanos) / 1_000_000f
        lastFrameNanos = now
        val vsyncIntervalMs = 16.67f
        val framePhase = ((deltaMs % vsyncIntervalMs) / vsyncIntervalMs).coerceIn(0f, 1f)

        val bands = IvannaVisualizerBridgeV2.sample()

        GLES30.glUniform1fv(locBands, IvannaVisualizerBridgeV2.BAND_COUNT, bands, 0)
        GLES30.glUniform1fv(locBandsPrev, IvannaVisualizerBridgeV2.BAND_COUNT, prevBands, 0)
        GLES30.glUniform1f(locFramePhase, framePhase)
        GLES30.glUniform1f(locTime, (now - startNanos) / 1_000_000_000f)
        GLES30.glUniform2f(locRes, width.toFloat(), height.toFloat())

        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        prevBands = bands
    }

    private fun loadShaderAsset(): String {
        context.assets.open(SHADER_ASSET_PATH).use { stream ->
            BufferedReader(InputStreamReader(stream)).use { reader ->
                return reader.readText()
            }
        }
    }

    private fun compile(type: Int, src: String): Int {
        val shader = GLES30.glCreateShader(type)
        GLES30.glShaderSource(shader, src)
        GLES30.glCompileShader(shader)
        val status = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0)
        if (status[0] == 0) {
            val log = GLES30.glGetShaderInfoLog(shader)
            GLES30.glDeleteShader(shader)
            throw RuntimeException("Compile error VisualizerRendererV2 ($type): $log")
        }
        return shader
    }
}
