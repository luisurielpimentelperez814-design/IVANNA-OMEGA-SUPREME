#!/data/data/com.termux/files/usr/bin/bash

set -e

FILE="app/src/main/cpp/dsp/SafetyLimiter.cpp"

python3 - <<'PY'
from pathlib import Path

p = Path("app/src/main/cpp/dsp/SafetyLimiter.cpp")
s = p.read_text()

old = """    float gain = m_gainNow;

    for (int i = 0; i < frames; ++i) {
        const float inL = L[i];
        const float inR = R[i];

        // Clip detection (entrada, una sola vez por evento)
        if (std::fabs(inL) > ceil_ || std::fabs(inR) > ceil_) ++clips;

        // Envolvente: ataque instantáneo si hay que bajar, release exponencial
        const float need = std::min(computeGainForPeak(std::fabs(inL)),
                                    computeGainForPeak(std::fabs(inR)));
        if (need < gain) {
            gain = need;
        } else {
            gain = m_releaseCoef * gain + (1.0f - m_releaseCoef) * 1.0f;
        }
"""

new = """    float gain = m_gainNow;

    // FIX THD:
    // El gain NO puede seguir cada muestra de un seno.
    // Eso genera modulación de amplitud y armónicos.
    // Se calcula una sola reducción por bloque usando el peak detectado.
    const float blockGain = computeGainForPeak(peak);

    for (int i = 0; i < frames; ++i) {
        const float inL = L[i];
        const float inR = R[i];

        // Clip detection (entrada, una sola vez por evento)
        if (std::fabs(inL) > ceil_ || std::fabs(inR) > ceil_) ++clips;

        // Ataque rápido al gain del bloque + release suave.
        // Mantiene la forma de onda intacta.
        if (blockGain < gain) {
            gain = blockGain;
        } else {
            gain = m_releaseCoef * gain +
                   (1.0f - m_releaseCoef) * 1.0f;
        }
"""

if old not in s:
    raise SystemExit("No se encontró el bloque de gain. No se modificó nada.")

s = s.replace(old, new)

p.write_text(s)
PY

git diff -- app/src/main/cpp/dsp/SafetyLimiter.cpp

git add app/src/main/cpp/dsp/SafetyLimiter.cpp
git commit -m "fix(dsp): calculate limiter gain per block to reduce THD"
git push origin main

echo "DONE"
