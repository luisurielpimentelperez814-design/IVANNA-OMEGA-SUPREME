#version 320 es
// wallpaper_v2.glsl — VERSIÓN "COLORES CON VIDA"
// ============================================================================
// IVANNA — Wallpaper dinámico audio-reactivo v2+ (OpenGL ES 3.2)
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [FIX-AURORA-LIVELY] La versión anterior era una escena oscura tipo
// obsidiana/cromo con acentos PBR pequeños y una aurora tenue de fondo —
// visualmente "apagada" comparada con la referencia (campo de color fluido,
// saturado, morfando en toda la pantalla, tipo plasma/líquido). Esta versión
// reemplaza la nebulosa oscura + aurora tenue por un campo de flujo de color
// de pantalla completa (domain warping FBM, paleta viva púrpura→magenta→
// naranja→cian→azul), que reacciona en velocidad/tono/brillo a las mismas
// 13 bandas Gammatone de siempre. Los 13 nodos PBR se mantienen como acento
// (para que se siga viendo la reactividad por banda de forma legible), pero
// ya no dominan la composición: el flujo de color vivo es el protagonista.
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
    for(int i = 0; i < 6; i++) {
        if(i >= octaves) break;
        value += amplitude * noise(p * frequency);
        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value / maxValue;
}

// Domain warping de 2 niveles (estilo Inigo Quilez): en vez de una sola capa
// de ruido, cada capa distorsiona el espacio de la siguiente -> el resultado
// fluye y se retuerce como líquido/plasma en vez de verse como "estática".
float flowField(vec2 p, float t) {
    vec2 q = vec2(
        fbm(p + vec2(0.0, 0.0) + t * 0.05, 4),
        fbm(p + vec2(5.2, 1.3) - t * 0.04, 4)
    );
    vec2 r = vec2(
        fbm(p + 3.2 * q + vec2(1.7, 9.2) + t * 0.09, 4),
        fbm(p + 3.2 * q + vec2(8.3, 2.8) - t * 0.07, 4)
    );
    return fbm(p + 3.2 * r, 5);
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

vec3 shadeGlassNode(vec3 N, vec3 V, vec3 L) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 1e-4);
    float roughness = 0.12;
    vec3 F0 = vec3(0.08);
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 1e-6);
    return spec * NdotL;
}

// Paleta "viva": azul profundo -> púrpura -> magenta/rosa -> naranja cálido
// -> cian/teal -> vuelve a azul. Ciclo continuo (t en 0..1 via fract),
// pensada para que SIEMPRE haya color saturado en pantalla, nunca negro.
vec3 livingPalette(float t) {
    vec3 c0 = vec3(0.16, 0.10, 0.55); // azul-violeta profundo
    vec3 c1 = vec3(0.55, 0.08, 0.75); // púrpura
    vec3 c2 = vec3(0.92, 0.16, 0.58); // magenta/rosa
    vec3 c3 = vec3(1.00, 0.46, 0.16); // naranja cálido
    vec3 c4 = vec3(0.15, 0.80, 0.75); // teal/cian
    vec3 c5 = vec3(0.20, 0.30, 0.92); // azul vivo
    float tt = fract(t);
    if (tt < 0.2)      return mix(c0, c1, tt / 0.2);
    else if (tt < 0.4) return mix(c1, c2, (tt - 0.2) / 0.2);
    else if (tt < 0.6) return mix(c2, c3, (tt - 0.4) / 0.2);
    else if (tt < 0.8) return mix(c3, c4, (tt - 0.6) / 0.2);
    else               return mix(c4, c5, (tt - 0.8) / 0.2);
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

// Campo de color vivo de pantalla completa: domain-warped FBM mapeado a
// livingPalette(), con velocidad/saturación/brillo modulados por graves
// (bass = "late" del flujo) y medios (mid = detalle/turbulencia extra).
vec3 generate_living_flow(vec2 uv, float bassEnergy, float midEnergy, float highEnergy) {
    float speed = 0.6 + bassEnergy * 1.4;
    vec2 p = uv * (1.4 + midEnergy * 0.6);
    float warped = flowField(p, u_time * speed);

    // segunda capa, más fina y más rápida, aporta "vida" de alta frecuencia
    // (detalle que titila con agudos, como espuma sobre el líquido).
    float detail = flowField(p * 2.6 + 4.0, u_time * (speed * 1.6 + highEnergy * 2.0));

    float hueShift = u_time * 0.035 + warped * 0.9 + detail * 0.25;
    vec3 col = livingPalette(hueShift);

    // Segunda muestra de paleta desfasada, mezclada por el detalle fino:
    // evita que se vea como un solo degradado monótono, da variedad tipo
    // "manchas de tinta" fluyendo.
    vec3 col2 = livingPalette(hueShift + 0.35 + detail * 0.5);
    col = mix(col, col2, smoothstep(0.35, 0.75, detail));

    float brightness = 0.75 + bassEnergy * 0.55 + midEnergy * 0.35 + detail * 0.25;
    return col * brightness;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution) / u_resolution.y;
    float distCenter = length(uv);
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(vec3(0.4, 0.6, 0.7));

    // Energía graves (0-3) / medios (4-8) / agudos (9-12) reales, con
    // interpolación u_frame_phase igual que el resto del shader.
    float bassEnergy = 0.0;
    for (int i = 0; i < 4; ++i) bassEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    bassEnergy *= 0.25;
    float midEnergy = 0.0;
    for (int i = 4; i < 9; ++i) midEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    midEnergy *= 0.2;
    float highEnergy = 0.0;
    for (int i = 9; i < 13; ++i) highEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    highEnergy *= 0.25;

    // El flujo de color vivo es la base de TODA la pantalla — nunca negro,
    // siempre saturado y en movimiento, como en la referencia.
    vec3 color = generate_living_flow(uv, bassEnergy, midEnergy, highEnergy);

    // 13 nodos de acento (uno por banda Gammatone): burbujas de cristal que
    // se iluminan con su propia banda, para que la reactividad por
    // frecuencia siga siendo legible sobre el flujo de color.
    for (int i = 0; i < 13; ++i) {
        float bandCurrent = u_bands[i];
        float bandPrev = u_bands_prev[i];
        float band = mix(bandPrev, bandCurrent, u_frame_phase);
        float t = float(i) / 12.0;
        float nodeRadius = 0.12 + t * 0.75;
        float nodeAngle = t * 6.28318 * 2.0 + u_time * (0.08 + 0.04 * t);
        vec2 nodePos = nodeRadius * vec2(cos(nodeAngle), sin(nodeAngle));
        float d = length(uv - nodePos);
        float nodeSize = 0.018 + 0.07 * band;
        float glow = (1.0 - smoothstep(0.0, nodeSize, d)) * band;
        float bloom = exp(-d * d * 7.0) * band;
        vec2 warp = (uv - nodePos) * rot(u_time * 0.1 + t * 3.0);
        vec3 N = normalize(vec3(warp.x * 2.0, warp.y * 2.0, 1.0));
        vec3 shaded = shadeGlassNode(N, V, L) * glow;
        vec3 spec_tint = spectrum_color_band(i, band);
        color += shaded * spec_tint * 1.4;
        color += spec_tint * bloom * 1.1;
    }

    // Viñeta suave — no oscurece tanto como antes, para no apagar el color vivo.
    vec2 vignette_uv = uv * (1.0 - uv);
    float vignette = pow(vignette_uv.x * vignette_uv.y * 4.0, 0.25);
    color *= mix(0.65, 1.0, vignette);

    color = tonemap_filmic(color * 1.35);
    color = pow(color, vec3(0.85));
    fragColor = vec4(color, 1.0);
}
