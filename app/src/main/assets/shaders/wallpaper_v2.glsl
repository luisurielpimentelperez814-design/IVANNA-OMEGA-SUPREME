#version 320 es
// wallpaper_v2.glsl — VERSIÓN ÉPICA MEJORADA
// ============================================================================
// IVANNA — Wallpaper dinámico audio-reactivo v2+ (OpenGL ES 3.2, PBR Enhanced)
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// v2+ mejoras:
// - Nebulosa dinámica de fondo con FBM y estrellas
// - Paleta espectral completa: Rojo→Naranja→Verde→Cian→Azul→Violeta→Magenta
// - Bloom épico (aureolas luminosas) con tone mapping filmico
// - Destellos especulares pulsantes en 8 puntos para dramatismo
// - Mantiene arquitectura PBR con 13 nodos radiales (grave→agudo)
// ============================================================================
precision highp float;

uniform float u_bands[13];
uniform float u_bands_prev[13];
uniform float u_frame_phase;
uniform float u_time;
uniform vec2  u_resolution;

out vec4 fragColor;

mat2 rot(float a) { float s = sin(a), c = cos(a); return mat2(c, -s, s, c); }

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
    for(int i = 0; i < 5; i++) {
        if(i >= octaves) break;
        value += amplitude * noise(p * frequency);
        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value / maxValue;
}

vec3 tonemap_filmic(vec3 x) {
    float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;
    x = ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
    float whiteScale = 1.0 / (((W*(A*W+C*B)+D*E)/(W*(A*W+B)+D*F))-E/F);
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

vec3 computeF0(float chromeMix) {
    const vec3 dielectricF0 = vec3(0.04);
    const vec3 chromeF0 = vec3(0.57, 0.67, 0.84);
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
    vec3 obsidianAlbedo = vec3(0.015, 0.014, 0.018);
    float obsidianRough = 0.82;
    float chromeRough = 0.045;
    float roughness = mix(obsidianRough, chromeRough, chromeMix);
    vec3 F0 = computeF0(chromeMix);
    vec3 albedo = mix(obsidianAlbedo, vec3(0.0), chromeMix);
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 1e-6);
    vec3 kd = (1.0 - F) * (1.0 - chromeMix);
    vec3 diffuse = kd * albedo / 3.14159265;
    return (diffuse + spec) * NdotL;
}

vec3 spectrum_color_band(int bandIndex, float magnitude) {
    vec3 color = vec3(0.0);
    if(bandIndex < 2) {
        color = mix(vec3(0.8, 0.0, 0.0), vec3(1.0, 0.3, 0.1), float(bandIndex) * 0.5);
    } else if(bandIndex < 4) {
        color = mix(vec3(1.0, 0.6, 0.0), vec3(1.0, 1.0, 0.0), float(bandIndex - 2) * 0.5);
    } else if(bandIndex < 6) {
        color = mix(vec3(0.2, 1.0, 0.3), vec3(0.3, 1.0, 0.8), float(bandIndex - 4) * 0.5);
    } else if(bandIndex < 8) {
        color = mix(vec3(0.2, 0.7, 1.0), vec3(0.3, 0.5, 1.0), float(bandIndex - 6) * 0.5);
    } else if(bandIndex < 10) {
        color = mix(vec3(0.7, 0.0, 1.0), vec3(1.0, 0.0, 1.0), float(bandIndex - 8) * 0.5);
    } else {
        color = mix(vec3(1.0, 0.4, 1.0), vec3(1.0, 0.6, 0.9), float(bandIndex - 10) / 3.0);
    }
    return color * (0.7 + magnitude * 0.9);
}

vec3 generate_nebula(vec2 uv) {
    float nebula_base = fbm(uv * 2.5 + vec2(u_time * 0.03), 4);
    float nebula_detail = fbm(uv * 7.0 + vec2(u_time * 0.01), 3);
    nebula_base = mix(nebula_base, nebula_detail, 0.35);
    vec3 nebula_color = vec3(0.0);
    nebula_color = mix(nebula_color, vec3(0.25, 0.05, 0.35), smoothstep(0.0, 0.35, nebula_base));
    nebula_color = mix(nebula_color, vec3(0.08, 0.15, 0.40), smoothstep(0.35, 0.65, nebula_base));
    nebula_color = mix(nebula_color, vec3(0.15, 0.35, 0.45), smoothstep(0.65, 1.0, nebula_base));
    float stars = fract(sin(dot(floor(uv * 120.0), vec2(12.9898, 78.233))) * 43758.5453);
    stars = smoothstep(0.93, 1.0, stars) * 0.7;
    nebula_color += vec3(1.0) * stars * 0.4;
    return nebula_color;
}

// ── Aurora boreal audio-reactiva ─────────────────────────────────────────
// Cintas verticales de FBM desplazadas por bandas graves/medias (u_bands),
// coloreadas verde→cian→violeta (paleta aurora real), aditivas sobre la
// nebulosa de fondo. bassEnergy/midEnergy controlan intensidad y velocidad
// de ondulación para que reaccione a la música en vez de ser un loop fijo.
vec3 generate_aurora(vec2 uv, float bassEnergy, float midEnergy) {
    vec3 aurora = vec3(0.0);
    // uv_a: espacio propio para la aurora, con el "suelo" en la parte
    // inferior de la pantalla (banda superior más despejada, como en el
    // fondo de referencia).
    vec2 uv_a = uv;
    uv_a.y += 0.35;

    const int RIBBONS = 4;
    for (int r = 0; r < RIBBONS; ++r) {
        float fr = float(r);
        // Cada cinta ondula a su propia frecuencia/fase; la velocidad crece
        // con la energía grave para que "respire" con el beat.
        float speed = 0.05 + 0.06 * fr + bassEnergy * 0.12;
        float freq = 1.6 + fr * 0.9;
        float phase = u_time * speed + fr * 2.3;

        float wave = fbm(vec2(uv_a.x * freq + phase, fr * 5.0), 3);
        wave += 0.25 * sin(uv_a.x * (3.0 + fr) + u_time * (0.4 + 0.1 * fr));

        // Altura de la cinta: modulada por energía media (voces/instrumentos)
        float ribbonY = 0.55 - fr * 0.10 + wave * (0.18 + midEnergy * 0.12);
        float dist = abs(uv_a.y - ribbonY);

        // Cortina vertical difusa (más ancha abajo, se afina hacia arriba)
        float curtain = exp(-dist * dist * (9.0 - fr * 1.2));

        // Paleta aurora: verde profundo -> cian -> violeta, según altura y cinta
        vec3 auroraGreen  = vec3(0.10, 0.85, 0.45);
        vec3 auroraCyan   = vec3(0.15, 0.65, 0.85);
        vec3 auroraViolet = vec3(0.45, 0.25, 0.85);
        float mixT = clamp(wave * 0.5 + 0.5, 0.0, 1.0);
        vec3 ribbonColor = mix(auroraGreen, auroraCyan, mixT);
        ribbonColor = mix(ribbonColor, auroraViolet, fr / float(RIBBONS));

        float intensity = curtain * (0.35 + bassEnergy * 0.5 + midEnergy * 0.3);
        aurora += ribbonColor * intensity;
    }

    // Se desvanece hacia el horizonte inferior para no competir con los
    // 13 nodos PBR del centro/borde.
    float fade = smoothstep(-0.7, 0.55, uv.y);
    return aurora * fade;
}

vec3 generate_glints(vec2 uv) {
    vec3 glints = vec3(0.0);
    for(int i = 0; i < 5; i++) {
        float seed = float(i) * 1.618;
        vec2 gpos = vec2(
            fract(sin(seed) * 43758.5453) * 0.8 + 0.1,
            fract(cos(seed * 0.71) * 43758.5453) * 0.5 + 0.2
        );
        float dist = length(uv - gpos);
        int band_idx = int(gpos.x * 13.0);
        if(band_idx >= 13) band_idx = 12;
        float band_mag = u_bands[band_idx];
        float pulse = sin(u_time * 3.5 + seed) * 0.5 + 0.5;
        float gint = exp(-dist * 18.0) * pulse * band_mag * 0.8;
        float rays = abs(sin((atan(uv.y - gpos.y, uv.x - gpos.x)) * 4.0)) * exp(-dist * 22.0) * band_mag * 0.4;
        glints += (gint + rays) * vec3(1.0, 0.93, 0.88);
    }
    return glints;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution) / u_resolution.y;
    float distCenter = length(uv);
    float angle = atan(uv.y, uv.x);
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(vec3(0.4, 0.6, 0.7));
    vec3 color = generate_nebula(uv) * 0.8;

    // Energía graves (bandas 0-3) / medios (4-8) reales, con interpolación
    // u_frame_phase igual que el resto del shader — la aurora respira con
    // la misma música que los 13 nodos, no es un efecto decorativo aparte.
    float bassEnergy = 0.0;
    for (int i = 0; i < 4; ++i) bassEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    bassEnergy *= 0.25;
    float midEnergy = 0.0;
    for (int i = 4; i < 9; ++i) midEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    midEnergy *= 0.2;
    color += generate_aurora(uv, bassEnergy, midEnergy);

    for (int i = 0; i < 13; ++i) {
        float bandCurrent = u_bands[i];
        float bandPrev = u_bands_prev[i];
        float band = mix(bandPrev, bandCurrent, u_frame_phase);
        float t = float(i) / 12.0;
        float nodeRadius = 0.12 + t * 0.75;
        float nodeAngle = t * 6.28318 * 2.0 + u_time * (0.08 + 0.04 * t);
        vec2 nodePos = nodeRadius * vec2(cos(nodeAngle), sin(nodeAngle));
        float d = length(uv - nodePos);
        float nodeSize = 0.018 + 0.06 * band;
        float glow = (1.0 - smoothstep(0.0, nodeSize, d)) * band;
        float bloom = exp(-d * d * 8.0) * band * 0.9;
        vec2 warp = (uv - nodePos) * rot(u_time * 0.1 + t * 3.0);
        vec3 N = normalize(vec3(warp.x * 2.0, warp.y * 2.0, 1.0));
        float chromeMix = clamp(band * 1.5, 0.0, 1.0);
        vec3 shaded = shadePBR(N, V, L, chromeMix) * glow;
        vec3 spec_tint = spectrum_color_band(i, band);
        color += shaded * spec_tint * 1.8;
        color += spec_tint * (glow + bloom) * 0.5;
    }
    color += generate_glints(uv) * 0.6;
    float baseRing = smoothstep(0.92, 0.87, distCenter) * 0.025;
    color += vec3(0.08, 0.05, 0.15) * baseRing;
    vec2 vignette_uv = uv * (1.0 - uv);
    float vignette = pow(vignette_uv.x * vignette_uv.y * 4.0, 0.4);
    color *= mix(0.4, 1.0, vignette);
    color = tonemap_filmic(color * 1.6);
    color = pow(color, vec3(0.45));
    fragColor = vec4(color, 1.0);
}
