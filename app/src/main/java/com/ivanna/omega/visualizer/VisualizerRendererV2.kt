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
 * VisualizerRendererV2 — Renderer adaptativo con protección contra congelamiento.
 *
 * [FIX-FREEZE-5.0] Implementa GLTextureView.AdaptiveRenderer:
 *   - onFrameDropDetected(): activa modo simplificado (menos iteraciones en shader).
 *   - onFrameDropRecovered(): vuelve a calidad normal.
 *
 * [FIX-FREEZE-5.1] Zero-allocation path crítico:
 *   - Reusa FloatArray curBands/prevBands (no crea por frame).
 *   - sample() rellena array pre-allocado, no devuelve nuevo.
 *
 * [FIX-FREEZE-5.2] Shader quality uniforms:
 *   - u_quality: 1.0 = normal, 0.5 = simplificado (menos octavas de noise).
 *   - El shader lee u_quality y ajusta loops dinámicamente.
 */
class VisualizerRendererV2(context: Context) : GLSurfaceView.Renderer, 
    GLTextureView.AdaptiveRenderer {

    private val appContext: Context = context.applicationContext ?: context

    private var program = 0
    private var locBands = -1
    private var locBandsPrev = -1
    private var locFramePhase = -1
    private var locTime = -1
    private var locRes = -1
    private var locQuality = -1

    private var width = 1
    private var height = 1
    private val startNanos = System.nanoTime()

    private val prevBands = FloatArray(IvannaVisualizerBridgeV2.BAND_COUNT)
    private val curBands = FloatArray(IvannaVisualizerBridgeV2.BAND_COUNT)
    private val sampleBuffer = FloatArray(IvannaVisualizerBridgeV2.BAND_COUNT)

    @Volatile private var qualityLevel = 1.0f
    private var dropStreak = 0

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
                throw RuntimeException("Link error: $log")
            }
        }
        GLES30.glDeleteShader(vs)
        GLES30.glDeleteShader(fs)

        locBands = GLES30.glGetUniformLocation(program, "u_bands")
        locBandsPrev = GLES30.glGetUniformLocation(program, "u_bands_prev")
        locFramePhase = GLES30.glGetUniformLocation(program, "u_frame_phase")
        locTime = GLES30.glGetUniformLocation(program, "u_time")
        locRes = GLES30.glGetUniformLocation(program, "u_resolution")
        locQuality = GLES30.glGetUniformLocation(program, "u_quality")

        GLES30.glClearColor(0f, 0f, 0f, 1f)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_CULL_FACE)

        java.util.Arrays.fill(prevBands, 0f)
        java.util.Arrays.fill(curBands, 0f)
    }

    override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
        width = w; height = h
        GLES30.glViewport(0, 0, w, h)
    }

    override fun onDrawFrame(gl: GL10?) {
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(program)

        IvannaVisualizerBridgeV2.sampleInto(sampleBuffer)
        val n = IvannaVisualizerBridgeV2.BAND_COUNT

        for (i in 0 until n) {
            val v = sampleBuffer[i]
            curBands[i] = if (v.isNaN() || v.isInfinite()) 0f
                          else v.coerceIn(0f, 4f)
        }

        val nowNanos = System.nanoTime()
        val timeSec = (nowNanos - startNanos) / 1_000_000_000f
        val framePhase = 1.0f

        GLES30.glUniform1fv(locBands, n, curBands, 0)
        GLES30.glUniform1fv(locBandsPrev, n, prevBands, 0)
        GLES30.glUniform1f(locFramePhase, framePhase)
        GLES30.glUniform1f(locTime, timeSec)
        GLES30.glUniform2f(locRes, width.toFloat(), height.toFloat())
        GLES30.glUniform1f(locQuality, qualityLevel)

        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)

        System.arraycopy(curBands, 0, prevBands, 0, n)
    }

    override fun onFrameDropDetected(droppedFrames: Int) {
        dropStreak++
        if (dropStreak >= 2) {
            qualityLevel = 0.5f
        }
    }

    override fun onFrameDropRecovered() {
        dropStreak = 0
        qualityLevel = 1.0f
    }

    private fun loadShaderAsset(): String {
        appContext.assets.open(SHADER_ASSET_PATH).use { stream ->
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
            throw RuntimeException("Compile error ($type): $log")
        }
        return shader
    }
}
