#version 320 es
// wallpaper_v2.glsl — PSYCHEDELIC AURORA / KALEIDOSCOPIC PLASMA
// ============================================================================
// [PSYCHEDELIC-1.0] Rediseño psicodélico manteniendo el contrato de uniforms
//   y la interfaz de bandas del bridge (13 bandas, u_bands + u_bands_prev,
//   u_frame_phase, u_time, u_resolution, u_quality). Cero cambios en Kotlin.
//
// Estética:
//   - Plasma multicolor con warping fractal (domain warping en 2 niveles).
//   - Simetría caleidoscópica de 6-12 sectores modulada por bajos.
//   - Cromatismo IRIS: paleta arco iris que rota con u_time y latidos de bajo.
//   - Anillos de energía por banda (13 nodos orbitales pulsantes).
//   - Auroras psicodélicas que "respiran" con los medios.
//   - Chromatic aberration sutil en los brillos altos.
//
// [FIX-FREEZE-5.2] Shader adaptativo: u_quality controla nivel de detalle.
//   u_quality = 1.0: calidad completa (5 octavas fbm, 12 sectores, 13 nodos)
//   u_quality = 0.5: modo rendimiento (3 octavas, 6 sectores, 8 nodos)
// ============================================================================
precision highp float;

uniform float u_bands[13];
uniform float u_bands_prev[13];
uniform float u_frame_phase;
uniform float u_time;
uniform vec2  u_resolution;
uniform float u_quality;

out vec4 fragColor;

// ── Utils ──────────────────────────────────────────────────────────────────
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

int getOctaves() {
    return (u_quality > 0.7) ? 5 : 3;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float maxValue = 0.0;
    int octaves = getOctaves();
    for (int i = 0; i < 5; ++i) {
        if (i >= octaves) break;
        value += amplitude * noise(p * frequency);
        maxValue += amplitude;
        amplitude *= 0.55;
        frequency *= 2.02;
    }
    return value / maxValue;
}

// Domain warping fractal — el corazón del look psicodélico.
float warpedField(vec2 p, float t, float bass, float mids, float highs) {
    float speed = 0.20 + bass * 0.75;
    vec2 q = vec2(
        fbm(p + vec2(0.0, 0.0) + t * 0.045 * speed),
        fbm(p + vec2(5.2, 1.3) - t * 0.038 * speed)
    );
    // Segundo warp (más chaótico con highs)
    vec2 r = vec2(
        fbm(p + (2.4 + mids * 1.6) * q + vec2(1.7, 9.2) + t * 0.075),
        fbm(p + (2.4 + mids * 1.6) * q + vec2(8.3, 2.8) - t * 0.065)
    );
    // Modulación de highs para "crispación"
    r *= 1.0 + highs * 0.85;
    return fbm(p + (2.8 + bass * 1.5) * r);
}

// ── Paleta arco iris psicodélica ────────────────────────────────────────────
// Basado en la fórmula de Iñigo Quilez, con desplazamiento animado.
vec3 iridescent(float t) {
    vec3 a = vec3(0.5, 0.5, 0.5);
    vec3 b = vec3(0.5, 0.5, 0.5);
    vec3 c = vec3(1.0, 1.0, 1.0);
    // Fases desincronizadas → sensación cromática viva
    vec3 d = vec3(0.00, 0.33, 0.67);
    return a + b * cos(6.28318 * (c * t + d));
}

// Paleta "neón fluorescente" — para nodos pulsantes de altas frecuencias.
vec3 neonPalette(float t) {
    vec3 magenta = vec3(1.00, 0.15, 0.85);
    vec3 cyan    = vec3(0.10, 0.95, 1.00);
    vec3 lime    = vec3(0.55, 1.00, 0.20);
    vec3 orange  = vec3(1.00, 0.55, 0.10);
    float tt = fract(t);
    if (tt < 0.333)       return mix(magenta, cyan,   tt * 3.0);
    else if (tt < 0.666)  return mix(cyan,    lime,   (tt - 0.333) * 3.0);
    else                  return mix(lime,    orange, (tt - 0.666) * 3.0);
    // (por completitud: cierra el ciclo naturalmente con la siguiente frac)
}

// ── Kaleidoscopio ───────────────────────────────────────────────────────────
// Repliega el plano en N sectores angulares para crear simetría psicodélica.
vec2 kaleidoscope(vec2 uv, float sectors, float t) {
    float r = length(uv);
    float a = atan(uv.y, uv.x);
    // Sector angular
    float sector = 6.28318 / sectors;
    a = mod(a, sector);
    a = abs(a - sector * 0.5); // mirror en cada sector
    // Rotación global lenta con u_time
    a += t * 0.06;
    return vec2(cos(a), sin(a)) * r;
}

// ── Tonemap suave (Reinhard extendido, más colorido que filmic) ─────────────
vec3 tonemap(vec3 x) {
    // Reinhard con boost cromático
    return x / (1.0 + x * 0.85);
}

// ── Aberración cromática ────────────────────────────────────────────────────
// Desplaza R/G/B ligeramente hacia afuera desde el centro → brillos irisados.
vec3 chromaticAberration(vec2 uv, float intensity, vec3 baseColor) {
    vec2 dir = uv;
    float dist = length(uv);
    vec3 shift = vec3(
        dist * intensity * 0.008,
        0.0,
       -dist * intensity * 0.008
    );
    // Retorna el color base modulado por el desplazamiento (aproximación
    // pantalla-espacio sin sample de textura, seguro en shader procedural).
    return baseColor + shift * baseColor.gbr;
}

// ── Campo psicodélico principal ─────────────────────────────────────────────
vec3 generatePsychedelicField(vec2 uv, float bass, float mids, float highs) {
    // Sectores caleidoscópicos: 6 en bajo quality, 12 en alto, modulado por bass
    float sectors = (u_quality > 0.7) ? (8.0 + bass * 4.0) : 6.0;
    vec2 kuv = kaleidoscope(uv * 1.4, sectors, u_time);

    // Field warpeado, animado
    float field = warpedField(kuv * 1.2, u_time, bass, mids, highs);

    // Color IRIS que rota con el tiempo + reacciona a bajos
    float hue = field * 1.15 + u_time * 0.045 + bass * 0.32;
    vec3 baseColor = iridescent(hue);

    // Segunda capa: field diferente para dar profundidad
    float field2 = warpedField(kuv * 2.3 + vec2(3.7, 1.9), u_time * 1.35, bass, mids, highs);
    vec3 layerColor = iridescent(field2 * 0.85 - u_time * 0.028 + mids * 0.42);

    // Combinar por energía media
    vec3 color = mix(baseColor, layerColor, 0.42 + 0.22 * sin(u_time * 0.35));

    // Boost de saturación con bajos — el "trip"
    float sat = 1.0 + bass * 0.75;
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, sat);

    // Ondas concéntricas de bajos (respiración)
    float r = length(uv);
    float breath = sin(r * 12.0 - u_time * 2.5 + bass * 8.0) * 0.5 + 0.5;
    breath *= exp(-r * 0.9); // atenúa en los bordes
    color += neonPalette(u_time * 0.12 + bass * 0.4) * breath * (0.10 + bass * 0.30);

    // "Auroras" filamentosas horizontales — moduladas por medios
    float aurora = sin(uv.x * 4.0 + field * 5.0 + u_time * 0.7) *
                   sin(uv.y * 2.5 - u_time * 0.35) * 0.5 + 0.5;
    aurora *= exp(-abs(uv.y) * 1.6);
    aurora *= smoothstep(0.0, 1.0, mids);
    color += iridescent(u_time * 0.08 + aurora * 0.9) * aurora * 0.35;

    // Vignette psicodélico invertido: bordes más oscuros con matiz púrpura
    float vig = 1.0 - smoothstep(0.55, 1.35, length(uv * vec2(0.95, 1.05)));
    color *= mix(vec3(0.35, 0.15, 0.55), vec3(1.0), vig);

    // Fondo base para evitar zonas totalmente negras
    color += vec3(0.020, 0.008, 0.040) * (1.0 - vig);

    return color;
}

// ── Nodos orbitales (13 bandas del bridge) ──────────────────────────────────
vec3 renderBandNodes(vec2 uv, float baseTime) {
    int nodeCount = (u_quality > 0.7) ? 13 : 8;
    vec3 nodeGlow = vec3(0.0);
    float sparkAccum = 0.0;

    // Cada banda gira en una órbita distinta con una fase distinta
    for (int i = 0; i < 13; ++i) {
        if (i >= nodeCount) break;

        float band = mix(u_bands_prev[i], u_bands[i], u_frame_phase);
        band = clamp(band, 0.0, 1.8);
        float t = float(i) / 12.0;

        // Órbita: radio depende de la frecuencia (bajos al centro, altos afuera)
        float radius = 0.15 + t * 0.55;
        // Velocidad angular: bandas altas rotan más rápido
        float angSpeed = 0.12 + t * 0.45;
        // Fase inicial distinta por banda
        float angle = t * 6.28318 * 1.7 + baseTime * angSpeed + sin(baseTime * 0.3 + t * 9.0);

        vec2 nodePos = vec2(cos(angle), sin(angle)) * radius;
        // "Wobble" psicodélico
        nodePos += 0.06 * vec2(
            sin(baseTime * 0.7 + t * 12.0),
            cos(baseTime * 0.55 - t * 7.5)
        );

        float d = length(uv - nodePos);
        // Spark núcleo — muy nítido
        float sparkSharpness = max(120.0, 260.0 - band * 90.0);
        float spark = exp(-d * d * sparkSharpness);
        // Halo suave — se expande con la energía de la banda
        float haloSharpness  = max(10.0, 24.0 - band * 8.0);
        float halo  = exp(-d * d * haloSharpness) * band;
        // Anillo (ring) alrededor del nodo — se expande con la banda
        float ringR = 0.02 + band * 0.05;
        float ring = exp(-abs(d - ringR) * 45.0) * band * 0.55;

        // Color: bajos = magenta/rojo, medios = verde/amarillo, altos = cyan/violeta
        vec3 nodeColor = neonPalette(t + baseTime * 0.05);

        nodeGlow += nodeColor * spark * (0.35 + band * 0.85);
        nodeGlow += nodeColor * halo  * 0.25;
        nodeGlow += nodeColor * ring;

        if (i >= 9) sparkAccum += spark * band;
    }

    // Acumulación de brillo blanco en los altos (efecto "estrellas")
    nodeGlow += vec3(1.0, 0.95, 0.85) * (sparkAccum * 0.12);

    return nodeGlow;
}

// ── Main ────────────────────────────────────────────────────────────────────
void main() {
    // uv centrado, aspect-corrected
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution) / u_resolution.y;

    // Bajos: 4 primeras bandas
    float bassEnergy = 0.0;
    for (int i = 0; i < 4; ++i) bassEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    bassEnergy *= 0.25;
    bassEnergy = clamp(bassEnergy, 0.0, 1.5);

    // Medios: bandas 4..8
    float midEnergy = 0.0;
    for (int i = 4; i < 9; ++i) midEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    midEnergy *= 0.20;
    midEnergy = clamp(midEnergy, 0.0, 1.5);

    // Altos: bandas 9..12
    float highEnergy = 0.0;
    for (int i = 9; i < 13; ++i) highEnergy += mix(u_bands_prev[i], u_bands[i], u_frame_phase);
    highEnergy *= 0.25;
    highEnergy = clamp(highEnergy, 0.0, 1.5);

    // Rotación global del plano — modulada por bajos (mareo psicodélico)
    float globalRot = u_time * 0.035 + bassEnergy * 0.18;
    uv = rot(globalRot) * uv;

    // Zoom respiratorio con medios
    float zoom = 1.0 - midEnergy * 0.08 * (0.5 + 0.5 * sin(u_time * 0.9));
    uv *= zoom;

    // Campo psicodélico base
    vec3 color = generatePsychedelicField(uv, bassEnergy, midEnergy, highEnergy);

    // Nodos por banda
    color += renderBandNodes(uv, u_time);

    // Aberración cromática con altos
    color = chromaticAberration(uv, highEnergy * 1.4, color);

    // Boost final de contraste con bajos
    float contrast = 1.0 + bassEnergy * 0.30;
    color = (color - 0.5) * contrast + 0.5;

    // Tonemap y gamma
    color = tonemap(color * 1.35);
    color = pow(max(color, 0.0), vec3(0.88));

    fragColor = vec4(color, 1.0);
}
