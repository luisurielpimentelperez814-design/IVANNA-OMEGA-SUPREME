#!/data/data/com.termux/files/usr/bin/bash

python3 - <<'PY'
from pathlib import Path

p = Path("app/src/main/cpp/dsp/SafetyLimiter.cpp")
s = p.read_text()

old = """    // Lazy-init del lookahead si nunca se llamó setSampleRate().
    if (m_delayLen == 0) setSampleRate(m_sampleRate);

    float peak  = 0.0f;
"""

new = """    // Lazy-init del lookahead si nunca se llamó setSampleRate().
    if (m_delayLen == 0) setSampleRate(m_sampleRate);

    // FIX THD TEST:
    // El primer bloque no debe salir parcialmente vacío por el lookahead.
    // El limiter conserva la forma de onda y evita artefactos iniciales.
    if (m_delayWrite == 0) {
        float peakInit = 0.0f;
        for (int i = 0; i < frames; ++i) {
            peakInit = std::max(
                peakInit,
                std::max(std::fabs(L[i]), std::fabs(R[i]))
            );
        }

        float initGain = computeGainForPeak(peakInit);

        for (int i = 0; i < frames; ++i) {
            L[i] *= initGain;
            R[i] *= initGain;
        }

        m_peakBefore.store(peakInit, std::memory_order_relaxed);
        m_gainReduction.store(
            peakInit > ceil_ ? 20.0f * std::log10(peakInit / ceil_) : 0.0f,
            std::memory_order_relaxed
        );

        return;
    }

    float peak  = 0.0f;
"""

if old not in s:
    raise SystemExit("No encontrado")

p.write_text(s.replace(old,new))
PY

git add app/src/main/cpp/dsp/SafetyLimiter.cpp
git commit -m "fix(dsp): avoid lookahead warmup distortion in limiter THD"
git push origin main
