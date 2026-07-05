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
 *   - carga el fragment shader desde assets/shaders/wallpaper_v2.glsl.
 *   - bindea un array de 13 uniforms (u_bands / u_bands_prev).
 *   - lee de IvannaVisualizerBridgeV2 (13 bandas).
 *
 * [FIX-AURORA-BG-4.4] Correcciones:
 *   1. prevBands se COPIA (no se aliasa) a partir del sample anterior. Antes
 *      hacíamos `prevBands = bands`, y como IvannaVisualizerBridgeV2.sample()
 *      devuelve un array recién creado desde JNI, prevBands y bands podían
 *      terminar apuntando al mismo buffer entre frames — dejando el
 *      mix(u_bands_prev, u_bands, u_frame_phase) del shader plano. Por eso
 *      el wallpaper se veía "estático" y no reaccionaba al audio con la
 *      fluidez del video de referencia.
 *   2. framePhase real: antes se calculaba como (delta % 16.67)/16.67, lo
 *      que producía 0 en un display de 60 Hz constante y saltos aleatorios
 *      en displays de 90/120 Hz. Ahora es simplemente 1.0: en cada frame
 *      queremos las bandas nuevas 100%, y el prev sirve para suavizar los
 *      picos entre samples del hilo de audio (que llega a otra tasa).
 *      El shader interpola entre "hace 1 frame" y "ahora", que es lo que
 *      se ve fluido y cinematográfico.
 *   3. Contexto guardado como applicationContext para evitar leak de Activity.
 *   4. Sanitizado de bandas: si el bridge devuelve NaN o valores desbocados
 *      antes de la primera muestra real, los clamp a [0, 4] para no romper
 *      el shader (exp() con exponentes negativos gigantes explota).
 */
class VisualizerRendererV2(context: Context) : GLSurfaceView.Renderer {
    private val appContext: Context = context.applicationContext ?: context

    private var program = 0
    private var locBands = -1
    private var locBandsPrev = -1
    private var locFramePhase = -1
    private var locTime = -1
    private var locRes = -1

    private var width = 1
    private var height = 1
    private val startNanos = System.nanoTime()
    private val prevBands = FloatArray(IvannaVisualizerBridgeV2.BAND_COUNT)
    private val curBands = FloatArray(IvannaVisualizerBridgeV2.BAND_COUNT)

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
        // Sin depth/stencil, sin blending: fullscreen pass, todo píxel se
        // escribe. Esto es lo que nos permite el TextureView opaco sin
        // artefactos.
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_CULL_FACE)

        // Reset estado local (por si se recrea contexto tras rotación).
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

        // [FIX-AURORA-BG-4.4.1] COPIAR el sample actual a curBands, sin alias.
        val sampled = IvannaVisualizerBridgeV2.sample()
        val n = IvannaVisualizerBridgeV2.BAND_COUNT
        // Salvaguarda: si el bridge no está listo, sampled puede venir de
        // tamaño distinto o con basura — sanitizamos.
        val src = if (sampled.size >= n) sampled else FloatArray(n)
        for (i in 0 until n) {
            val v = src[i]
            // clamp defensivo contra NaN / infinitos / valores desbocados.
            curBands[i] = if (v.isNaN() || v.isInfinite()) 0f
                          else v.coerceIn(0f, 4f)
        }

        val nowNanos = System.nanoTime()
        val timeSec = (nowNanos - startNanos) / 1_000_000_000f

        // [FIX-AURORA-BG-4.4.2] framePhase = 1.0 en cada draw. El shader hace
        //   mix(u_bands_prev, u_bands, u_frame_phase)
        // y con phase=1 usa siempre el sample más reciente, pero como
        // prevBands guarda el del frame ANTERIOR, el shader puede usarlo
        // internamente para efectos de tail/echo si quisiera. Lo importante
        // es que ahora prev y cur son buffers distintos, no aliases.
        val framePhase = 1.0f

        GLES30.glUniform1fv(locBands, n, curBands, 0)
        GLES30.glUniform1fv(locBandsPrev, n, prevBands, 0)
        GLES30.glUniform1f(locFramePhase, framePhase)
        GLES30.glUniform1f(locTime, timeSec)
        GLES30.glUniform2f(locRes, width.toFloat(), height.toFloat())

        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)

        // Copia cur → prev para el próximo frame (arraycopy, no alias).
        System.arraycopy(curBands, 0, prevBands, 0, n)
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
            throw RuntimeException("Compile error VisualizerRendererV2 ($type): $log")
        }
        return shader
    }
}
