// Circular Spectrum Shader
uniform float uTime;
uniform float uAudioData[64];
uniform vec2 uResolution;

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / min(uResolution.x, uResolution.y);
    float r = length(uv);
    float angle = atan(uv.y, uv.x);
    float band = floor(angle / 6.2832 * 64.0);
    float intensity = uAudioData[int(band)] * 2.0;
    float brightness = smoothstep(r, r + 0.1, intensity * 0.5);
    vec3 color = vec3(0.0, brightness * 0.8, brightness);
    gl_FragColor = vec4(color, 1.0);
}
