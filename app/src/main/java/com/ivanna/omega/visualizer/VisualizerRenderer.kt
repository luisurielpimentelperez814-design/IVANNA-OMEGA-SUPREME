package com.ivanna.omega.visualizer

import android.opengl.GLES30
import android.opengl.GLSurfaceView
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

private const val VERTEX_SRC = """#version 320 es
precision highp float;
// Full-screen triangle sin VBO — 3 vértices generados por gl_VertexID.
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
"""

private const val FRAGMENT_SRC = """#version 320 es
precision highp float;

uniform float u_bass_pulse;
uniform float u_mid_flow;
uniform float u_mid_flow_prev;
uniform float u_frame_phase;
uniform float u_high_flicker;
uniform float u_time;
uniform vec2  u_resolution;

out vec4 fragColor;

mat2 rot(float a) { float s = sin(a), c = cos(a); return mat2(c, -s, s, c); }

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 curlNoise(vec2 p) {
    float eps = 0.01;
    float n1 = hash21(p + vec2(0.0, eps));
    float n2 = hash21(p - vec2(0.0, eps));
    float n3 = hash21(p + vec2(eps, 0.0));
    float n4 = hash21(p - vec2(eps, 0.0));
    return vec2(n1 - n2, n4 - n3) / (2.0 * eps);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d + 1e-6);
}
float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / (NdotL * (1.0 - k) + k);
    return gV * gL;
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// F0 físicamente consistente: un dieléctrico (obsidiana, F0≈0.04) y un metal
// (cromo, F0 espectral ~0.57-0.94) no promedian linealmente en la realidad —
// mezclar sus F0 producía una rampa visual sin base física en chromeMix≈0.5.
// Aquí se bifurca: por debajo de 0.5 es dieléctrico puro, por encima interpola
// dentro del rango especular del metal real.
vec3 computeF0(float chromeMix) {
    const vec3 dielectricF0   = vec3(0.04);
    const vec3 chromeF0       = vec3(0.57, 0.67, 0.84);
    const vec3 chromeBrightF0 = vec3(0.94, 0.92, 0.90);

    if (chromeMix > 0.5) {
        return mix(chromeF0, chromeBrightF0, (chromeMix - 0.5) * 2.0);
    }
    return dielectricF0;
}

vec3 shadePBR(vec3 N, vec3 V, vec3 L, float chromeMix) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 1e-4);

    vec3  obsidianAlbedo = vec3(0.015, 0.014, 0.018);
    float obsidianRough  = 0.82;
    float chromeRough    = 0.045;

    float roughness = mix(obsidianRough, chromeRough, chromeMix);
    vec3  F0        = computeF0(chromeMix);
    // Metal puro no tiene componente difusa; se desvanece junto con chromeMix.
    vec3  albedo    = mix(obsidianAlbedo, vec3(0.0), chromeMix);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 1e-6);
    vec3 kd = (1.0 - F) * (1.0 - chromeMix);
    vec3 diffuse = kd * albedo / 3.14159265;

    return (diffuse + spec) * NdotL;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution) / u_resolution.y;

    float pulseRadius = 0.22 + 0.18 * u_bass_pulse;
    float distCenter = length(uv);
    float node = 1.0 - smoothstep(pulseRadius - 0.02, pulseRadius, distCenter);

    // Interpolación temporal de u_mid_flow: sin esto, un salto de 0.1→0.9
    // entre bloques de audio producía un salto de ~280px en el warp en un
    // solo frame (aliasing temporal). u_frame_phase avanza [0,1] dentro del
    // intervalo de vsync.
    float interpolatedMidFlow = mix(u_mid_flow_prev, u_mid_flow, u_frame_phase);

    vec2 flowUV = uv * rot(u_time * 0.05);
    vec2 warp = curlNoise(flowUV * 3.0 + u_time * 0.1) * interpolatedMidFlow * 0.35;
    vec2 warpedUV = uv + warp;
    float fieldLines = sin((warpedUV.x * 12.0 + warpedUV.y * 8.0) + u_time * 0.4);
    fieldLines = smoothstep(0.85, 1.0, abs(fieldLines)) * interpolatedMidFlow;

    float particleField = 0.0;
    for (int i = 0; i < 24; ++i) {
        vec2 seed = vec2(float(i) * 12.9898, float(i) * 78.233);
        vec2 pPos = vec2(hash21(seed), hash21(seed + 1.0)) * 2.0 - 1.0;
        pPos *= 0.9;
        float d = length(uv - pPos);
        float twinkle = hash21(seed + floor(u_time * 6.0));
        particleField += (1.0 - smoothstep(0.0, 0.018, d)) * twinkle * u_high_flicker;
    }

    vec3 N = normalize(vec3(warp.x * 1.5, warp.y * 1.5, 1.0));
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(vec3(0.4, 0.6, 0.7));

    float chromeMix = clamp(node * 1.4 + fieldLines * 0.6, 0.0, 1.0);
    vec3 base = shadePBR(N, V, L, chromeMix);

    vec3 color = base;
    color += vec3(1.0, 0.85, 0.6) * particleField;
    color += vec3(0.3, 0.5, 1.0) * node * u_bass_pulse * 0.4;

    color = color / (1.0 + color);
    color = pow(color, vec3(1.0 / 2.2));

    fragColor = vec4(color, 1.0);
}
"""

class VisualizerRenderer : GLSurfaceView.Renderer {
    private var program = 0
    private var locBass = -1
    private var locMid = -1
    private var locMidPrev = -1
    private var locFramePhase = -1
    private var locHigh = -1
    private var locTime = -1
    private var locRes = -1

    private var width = 1
    private var height = 1
    private val startNanos = System.nanoTime()
    private var prevMidFlow = 0f
    private var lastFrameNanos = 0L

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        val vs = compile(GLES30.GL_VERTEX_SHADER, VERTEX_SRC)
        val fs = compile(GLES30.GL_FRAGMENT_SHADER, FRAGMENT_SRC)
        program = GLES30.glCreateProgram().also {
            GLES30.glAttachShader(it, vs)
            GLES30.glAttachShader(it, fs)
            GLES30.glLinkProgram(it)
            val status = IntArray(1)
            GLES30.glGetProgramiv(it, GLES30.GL_LINK_STATUS, status, 0)
            if (status[0] == 0) {
                val log = GLES30.glGetProgramInfoLog(it)
                GLES30.glDeleteProgram(it)
                throw RuntimeException("Link error VisualizerRenderer: $log")
            }
        }
        GLES30.glDeleteShader(vs)
        GLES30.glDeleteShader(fs)

        // Cachear uniform locations — no por frame.
        locBass = GLES30.glGetUniformLocation(program, "u_bass_pulse")
        locMid = GLES30.glGetUniformLocation(program, "u_mid_flow")
        locMidPrev = GLES30.glGetUniformLocation(program, "u_mid_flow_prev")
        locFramePhase = GLES30.glGetUniformLocation(program, "u_frame_phase")
        locHigh = GLES30.glGetUniformLocation(program, "u_high_flicker")
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

        val u = IvannaVisualizerBridge.sample()
        val currentMidFlow = u.getOrElse(1) { 0f }
        GLES30.glUniform1f(locBass, u.getOrElse(0) { 0f })
        GLES30.glUniform1f(locMid, currentMidFlow)
        GLES30.glUniform1f(locMidPrev, prevMidFlow)
        GLES30.glUniform1f(locFramePhase, framePhase)
        GLES30.glUniform1f(locHigh, u.getOrElse(2) { 0f })
        GLES30.glUniform1f(locTime, (now - startNanos) / 1_000_000_000f)
        GLES30.glUniform2f(locRes, width.toFloat(), height.toFloat())

        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        prevMidFlow = currentMidFlow
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
            throw RuntimeException("Compile error VisualizerRenderer ($type): $log")
        }
        return shader
    }
}
