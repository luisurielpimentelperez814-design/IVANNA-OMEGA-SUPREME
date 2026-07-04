#version 320 es
// wallpaper_v2.glsl
// ============================================================================
// IVANNA — Wallpaper dinámico audio-reactivo v2 (OpenGL ES 3.2, PBR)
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Diferencia con el shader v1 embebido en VisualizerRenderer.kt: v1 recibe
// 3 uniforms agregados (u_bass_pulse/u_mid_flow/u_high_flicker). v2 recibe
// las 13 bandas crudas del gammatone lattice (u_bands[13]) y dibuja un
// anillo de 13 nodos PBR posicionados por frecuencia (grave→agudo = interior
// →exterior), cada uno modulando su propio radio/rugosidad/brillo. Reutiliza
// las mismas funciones GGX/Smith/Fresnel del shader v1 (misma base física,
// sin reinventar), extendidas a múltiples nodos en vez de uno solo.
// ============================================================================
precision highp float;

uniform float u_bands[13];       // energía normalizada [0,1] por banda ERB, grave→agudo
uniform float u_bands_prev[13];  // snapshot del bloque anterior, para interpolar sin popping
uniform float u_frame_phase;     // [0,1] fase dentro del intervalo de vsync (igual que v1)
uniform float u_time;
uniform vec2  u_resolution;

out vec4 fragColor;

mat2 rot(float a) { float s = sin(a), c = cos(a); return mat2(c, -s, s, c); }

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
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

// Misma bifurcación dieléctrico/metal que v1: evita promediar F0 linealmente
// entre obsidiana (F0≈0.04) y cromo (F0 espectral), que no tiene base física.
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
    float distCenter = length(uv);
    float angle = atan(uv.y, uv.x);

    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(vec3(0.4, 0.6, 0.7));

    vec3 color = vec3(0.0);

    // 13 nodos dispuestos radialmente, grave (banda 0) al centro, agudo
    // (banda 12) en el borde exterior — mapea directamente el eje ERB del
    // filterbank al espacio visual, sin colapsar a un solo "bass pulse".
    for (int i = 0; i < 13; ++i) {
        float bandCurrent = u_bands[i];
        float bandPrev = u_bands_prev[i];
        float band = mix(bandPrev, bandCurrent, u_frame_phase);

        float t = float(i) / 12.0;
        float nodeRadius = 0.12 + t * 0.75;
        float nodeAngle = t * 6.28318 * 2.0 + u_time * (0.08 + 0.04 * t);

        vec2 nodePos = nodeRadius * vec2(cos(nodeAngle), sin(nodeAngle));
        float d = length(uv - nodePos);

        float nodeSize = 0.02 + 0.05 * band;
        float glow = (1.0 - smoothstep(0.0, nodeSize, d)) * band;

        vec2 warp = (uv - nodePos) * rot(u_time * 0.1 + t * 3.0);
        vec3 N = normalize(vec3(warp.x * 2.0, warp.y * 2.0, 1.0));
        float chromeMix = clamp(band * 1.3, 0.0, 1.0);
        vec3 shaded = shadePBR(N, V, L, chromeMix) * glow;

        // Tinte por rango de frecuencia: graves cálidos, agudos fríos —
        // consistente con el mapeo perceptual grave→cálido usado en v1
        // (u_bass_pulse sumaba un tinte azulado/cálido según banda).
        vec3 tint = mix(vec3(1.0, 0.55, 0.25), vec3(0.35, 0.65, 1.0), t);
        color += shaded * tint * 1.6;
        color += tint * glow * 0.35; // halo aditivo, evita nodos "apagados" en rugosidad alta
    }

    // Anillo base tenue para dar contexto espacial a los 13 nodos aun en silencio.
    float baseRing = smoothstep(0.9, 0.88, distCenter) * 0.02;
    color += vec3(0.02, 0.02, 0.03) * baseRing;

    color = color / (1.0 + color);
    color = pow(color, vec3(1.0 / 2.2));

    fragColor = vec4(color, 1.0);
}
