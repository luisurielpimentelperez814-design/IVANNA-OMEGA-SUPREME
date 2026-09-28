// Wave Particles Shader
uniform float uTime;
uniform float uAudioData[64];
uniform vec2 uResolution;

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    float sum = 0.0;
    for (int i = 0; i < 64; i++) {
        sum += uAudioData[i];
    }
    float avg = sum / 64.0;
    float wave = sin(uv.x * 20.0 + uTime * 2.0 + uv.y * 10.0 * avg) * 0.5 + 0.5;
    vec3 color = vec3(wave * avg, wave * (1.0 - avg), avg);
    gl_FragColor = vec4(color, 1.0);
}
