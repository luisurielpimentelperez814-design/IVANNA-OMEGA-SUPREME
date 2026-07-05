#version 320 es
// wallpaper_v2.glsl — GEMINI DEPTH / AURORA GLASS
// ============================================================================
// IVANNA — Wallpaper dinámico audio-reactivo v2+ (OpenGL ES 3.2)
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [STYLE-GEMINI-DEPTH]
// La referencia del video no pide una pantalla saturada tipo plasma arcoíris,
// sino un fondo premium, limpio y profundo: negro/azul noche, un glow frío en
// la base, niebla auroral muy suave y transiciones fluidas al estilo Gemini.
//
// Objetivo visual de esta versión:
//   - base oscura y elegante, sin “lavar” el panel Compose;
//   - glow cian/azul en el tercio inferior, como luz difusa desde abajo;
//   - aurora y ondas muy sutiles, no un festival de color;
//   - respuesta al audio visible pero fina, cinematográfica, no agresiva;
//   - 13 acentos por banda integrados como destellos/vidrio, no como burbujas
//     protagonistas que rompan la limpieza del fondo.
//
// El FIX de congelamiento / cuadrantes vive en GLTextureView.kt. Aquí solo se
// pule la estética para que el wallpaper se parezca más al fondo del video.
// ============================================================================
precision highp float;

uniform float u_bands[13];
uniform float u_bands_prev[13];
uniform float u_frame_phase;
uniform float u_time;
uniform vec2  u_resolution;

out vec4 fragColor;

mat2 rot(float a) {
    float s = sin(a), c = cos(a);
    return mat2(c, -s, s, c);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n0 = hash(i);
    float n1 = hash(i + vec2(1.0, 0.0));
    float n2 = hash(i + vec2(0.0, 1.0));
    float n3 = hash(i + vec2(1.0, 1.0));
    float nx0 = mix(n0, n1, f.x);
    float nx1 = mix(n2, n3, f.x);
    return mix(nx0, nx1, f.y);
}

float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float maxValue = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) break;
        value += amplitude * noise(p * frequency);
        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value / maxValue;
}

float flowField(vec2 p, float t) {
    vec2 q = vec2(
        fbm(p + vec2(0.0, 0.0) + t * 0.035, 4),
        fbm(p + vec2(5.2, 1.3) - t * 0.030, 4)
    );
    vec2 r = vec2(
        fbm(p + 2.5 * q + vec2(1.7, 9.2) + t * 0.055, 4),
        fbm(p + 2.5 * q + vec2(8.3, 2.8) - t * 0.050, 4)
    );
    return fbm(p + 2.8 * r, 5);
}

vec3 tonemap_filmic(vec3 x) {
    float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;
    x = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    float whiteScale = 1.0 / (((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F);
    return x * whiteScale;
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

vec3 shadeGlassNode(vec3 N, vec3 V, vec3 L) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 1e-4);
    float roughness = 0.10;
    vec3 F0 = vec3(0.06, 0.08, 0.10);
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 1e-6);
    return spec * NdotL;
}

vec3 geminiPalette(float t) {
    vec3 c0 = vec3(0.010, 0.018, 0.040); // negro azulado profundo
    vec3 c1 = vec3(0.030, 0.060, 0.140); // azul noche
    vec3 c2 = vec3(0.080, 0.160, 0.320); // azul cobalt suave
    vec3 c3 = vec3(0.090, 0.280, 0.520); // azul eléctrico moderado
    vec3 c4 = vec3(0.160, 0.520, 0.760); // cian glow
    float tt = fract(t);
    if (tt < 0.25)      return mix(c0, c1, tt / 0.25);
    else if (tt < 0.50) return mix(c1, c2, (tt - 0.25) / 0.25);
    else if (tt < 0.75) return mix(c2, c3, (tt - 0.50) / 0.25);
    else                return mix(c3, c4, (tt - 0.75) / 0.25);
}

vec3 accentColor(float t, float magnitude) {
    vec3 icy = mix(vec3(0.46, 0.70, 1.00), vec3(0.82, 0.95, 1.00), t);
    vec3 lilac = mix(vec3(0.24, 0.36, 0.82), vec3(0.55, 0.48, 0.96), t * 0.6);
    vec3 c = mix(lilac, icy, 0.55 + 0.25 * sin(t * 6.28318 + u_time * 0.08));
    return c * (0.45 + magnitude * 0.75);
}

vec3 generateGeminiField(vec2 uv, float bassEnergy, float midEnergy, float highEnergy) {
    float speed = 0.16 + bassEnergy * 0.45;
    vec2 p = uv * vec2(1.10, 1.55);
    p += vec2(0.0, 0.08);

    float warpA = flowField(p * 1.15, u_time * speed);
    float warpB = flowField((p + vec2(2.7, -1.9)) * 1.95, u_time * (speed * 1.55 + highEnergy * 0.25));
    float field = mix(warpA, warpB, 0.42);

    // Cuerpo base: siempre oscuro, con respiración cromática muy lenta.
    vec3 deepBase = mix(
        vec3(0.004, 0.008, 0.018),
        geminiPalette(0.12 + field * 0.38 + u_time * 0.008),
        0.40 + midEnergy * 0.10
    );

    // Glow principal desde la base, como un foco difuso bajo el panel.
    vec2 glowPos = vec2(0.0, -0.78);
    float bottomGlow = exp(-length((uv - glowPos) * vec2(1.15, 2.35)) * (2.8 - bassEnergy * 0.9));
    vec3 glowColor = mix(vec3(0.03, 0.08, 0.20), vec3(0.18, 0.62, 0.95), 0.58 + 0.20 * field);

    // Aurora suave: columnas / velos verticales con poco contraste.
    float veil = flowField(vec2(uv.x * 1.3, uv.y * 0.55 + 1.5), u_time * (0.10 + midEnergy * 0.20));
    float auroraShape = smoothstep(0.26, 0.86, veil);
    auroraShape *= smoothstep(-0.92, -0.18, uv.y) * (1.0 - smoothstep(0.10, 0.88, uv.y));
    auroraShape *= 0.24 + highEnergy * 0.12;
    vec3 auroraColor = mix(vec3(0.02, 0.12, 0.24), vec3(0.12, 0.42, 0.72), auroraShape + 0.15);

    // Ondas horizontales muy finas, apenas perceptibles, para que haya vida.
    float wave = sin(uv.x * 7.5 + field * 6.0 - u_time * (0.16 + bassEnergy * 0.50));
    float waveMask = exp(-abs(uv.y + 0.38) * 7.0) * (0.5 + 0.5 * wave);
    vec3 waveColor = vec3(0.05, 0.18, 0.34) * waveMask * (0.18 + bassEnergy * 0.20);

    // Micro-bruma superior para evitar un negro plano totalmente muerto.
    float upperMist = exp(-abs(uv.y - 0.46) * 3.0) * (0.28 + 0.22 * warpB);
    vec3 mistColor = vec3(0.012, 0.026, 0.055) * upperMist;

    vec3 color = deepBase;
    color += glowColor * bottomGlow * (0.85 + bassEnergy * 0.65);
    color += auroraColor * auroraShape;
    color += waveColor;
    color += mistColor;

    // Foco central inferior extra: ayuda a recordar el “glow” del video.
    float core = exp(-dot(uv - vec2(0.0, -0.64), uv - vec2(0.0, -0.64)) * (8.5 - bassEnergy * 2.5));
    color += vec3(0.03, 0.16, 0.30) * core * (0.65 + bassEnergy * 0.40);

    return color;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution) / u_resolution.y;
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(vec3(0.28, 0.58, 0.76));

    float bassEnergy = 0.0;
    for (int i = 0; i < 4; ++i) bassEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    bassEnergy *= 0.25;

    float midEnergy = 0.0;
    for (int i = 4; i < 9; ++i) midEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    midEnergy *= 0.20;

    float highEnergy = 0.0;
    for (int i = 9; i < 13; ++i) highEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    highEnergy *= 0.25;

    vec3 color = generateGeminiField(uv, bassEnergy, midEnergy, highEnergy);

    // 13 acentos de cristal: siguen representando las bandas, pero ahora más
    // integrados y discretos, como chispas suspendidas en el campo.
    for (int i = 0; i < 13; ++i) {
        float band = mix(u_bands_prev[i], u_bands[i], u_frame_phase);
        float t = float(i) / 12.0;

        float layer = mix(0.14, 0.82, t);
        float drift = u_time * (0.045 + 0.020 * t);
        float angle = t * 6.28318 * 1.55 + drift;
        vec2 nodePos = vec2(cos(angle) * (0.10 + layer * 0.36), -0.30 + layer * 0.76 + sin(drift + t * 9.0) * 0.04);
        nodePos.x += sin(u_time * 0.10 + t * 17.0) * 0.05;

        float d = length(uv - nodePos);
        float nodeSize = 0.008 + 0.026 * band;
        float spark = exp(-d * d * (240.0 - band * 90.0));
        float halo = exp(-d * d * (28.0 - band * 10.0)) * band;

        vec2 warp = (uv - nodePos) * rot(u_time * 0.06 + t * 4.0);
        vec3 N = normalize(vec3(warp.x * 4.0, warp.y * 4.0, 1.0));
        vec3 spec = shadeGlassNode(N, V, L);
        vec3 tint = accentColor(t, band);

        color += tint * spark * (0.28 + band * 0.55);
        color += tint * halo * 0.16;
        color += spec * tint * spark * 0.55;
    }

    // Viñeta elegante: oscurece bordes y deja respirar el centro inferior.
    float vignette = smoothstep(1.30, 0.18, length(uv * vec2(0.92, 1.10)));
    color *= mix(0.52, 1.0, vignette);

    // Contraste muy suave, preservando el look premium oscuro.
    color = tonemap_filmic(color * 1.18);
    color = pow(max(color, 0.0), vec3(0.92));

    fragColor = vec4(color, 1.0);
}
